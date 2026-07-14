#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "ps_pwm.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include "led_strip.h"
static led_strip_handle_t s_led_strip = NULL;
#endif

#include <stdarg.h>

// --- Global Variables for Terminal Control ---
volatile float target_freq = 20000.0f; // 20kHz default
volatile float target_duty = 0.75f;    // 75% default
volatile uint32_t soft_start_ms = 2000; // 2s default
volatile uint32_t simulated_pulse_width_us = 2; // 2us default ZC pulse width
volatile bool is_pwm_on = false;
volatile bool is_capture_ready = false;

// --- Auto Tracking Variables ---
volatile bool auto_track_en = false;
volatile float target_zvs_delay = 5.0f;
volatile float track_kp = 50.0f;
volatile float track_ki = 5.0f;

// --- Terminal Input Buffer & Mutex ---
static char rx_buffer[128] = {0};
static int rx_idx = 0;
static SemaphoreHandle_t print_mux = NULL;

// --- Custom Printf to Prevent Terminal Interference ---
void sim_printf(const char *fmt, ...) {
    if (print_mux == NULL) return;
    xSemaphoreTake(print_mux, portMAX_DELAY);
    
    // 1. 커서를 맨 앞으로 이동(\r)시키고 현재 줄을 모두 지움(ANSI Escape: \x1b[2K)
    printf("\r\x1b[2K");
    
    // 2. 요청된 로그 출력
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    
    // 3. 프롬프트와 현재까지 타이핑한 명령어 다시 그리기
    printf("esp32> %s", rx_buffer);
    fflush(stdout);
    
    xSemaphoreGive(print_mux);
}

extern void initialize_phase_shift_pwm(); // From mcpwm_phase_shift_pwm_example.c


// --- 1. Main PS-PWM Control Task ---
void ps_pwm_main_task(void *arg) {
    sim_printf("[Main] Waiting for capture channel setup...\n");
    while (!is_capture_ready) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    sim_printf("[Main] Initializing PS-PWM...\n");
    initialize_phase_shift_pwm();

#ifdef CONFIG_IDF_TARGET_ESP32S3
    // Initialize RGB LED
    led_strip_config_t strip_config = {
        .strip_gpio_num = GPIO_NUM_48,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip));
    led_strip_clear(s_led_strip);
#endif
    
    pspwm_set_frequency(MCPWM_UNIT_0, target_freq);
    pspwm_set_ps_duty(MCPWM_UNIT_0, 0.0f);
    pspwm_disable_output(MCPWM_UNIT_0);

    // Configure GPIO 10 as ZC Pulse Output
    gpio_config_t zc_out_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_10),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&zc_out_conf);
    gpio_set_level(GPIO_NUM_10, 0);

    // Configure GPIO 0 Button
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);

    bool last_on_state = false;
    int last_btn_state = 1;
    float current_freq = target_freq;
    float current_duty = target_duty;

    uint32_t last_led_blink = 0;
    bool led_state = false;

    while(1) {
        // Poll button for toggle and fault clear
        int btn_state = gpio_get_level(GPIO_NUM_0);
        
        // 1. Check if a hardware fault occurred
        if (pspwm_get_hw_fault_shutdown_occurred(MCPWM_UNIT_0)) {
            static uint32_t last_print = 0;
            if (xTaskGetTickCount() - last_print > pdMS_TO_TICKS(1000)) {
                sim_printf("[CRITICAL] Hardware Fault (OCP) Detected on GPIO 8! Outputs are locked LOW. Press BOOT button (GPIO 0) to clear.\n");
                last_print = xTaskGetTickCount();
            }

#ifdef CONFIG_IDF_TARGET_ESP32S3
            // Blink Red LED
            if (xTaskGetTickCount() - last_led_blink > pdMS_TO_TICKS(500)) {
                if (led_state) {
                    led_strip_set_pixel(s_led_strip, 0, 32, 0, 0); // Red
                } else {
                    led_strip_set_pixel(s_led_strip, 0, 0, 0, 0); // Off
                }
                led_strip_refresh(s_led_strip);
                led_state = !led_state;
                last_led_blink = xTaskGetTickCount();
            }
#endif
            
            if (btn_state == 0 && last_btn_state == 1) { // Button pressed
                if (pspwm_get_hw_fault_shutdown_present(MCPWM_UNIT_0)) {
                    sim_printf("[Main] Cannot clear fault: GPIO 8 is still physically LOW!\n");
                } else {
                    sim_printf("[Main] Fault cleared by user. Returning to Disabled state.\n");
                    pspwm_disable_output(MCPWM_UNIT_0);
                    pspwm_clear_hw_fault_shutdown_occurred(MCPWM_UNIT_0);
                    is_pwm_on = false;
                    last_on_state = false;
#ifdef CONFIG_IDF_TARGET_ESP32S3
                    led_strip_set_pixel(s_led_strip, 0, 0, 0, 0); // Off
                    led_strip_refresh(s_led_strip);
#endif
                }
                vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
            }
            last_btn_state = btn_state;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue; // Skip the rest of the control loop
        }

        if (btn_state == 0 && last_btn_state == 1) { // Button pressed
            is_pwm_on = !is_pwm_on;
            sim_printf("[Main] Button Pressed! is_pwm_on = %d\n", is_pwm_on);
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
        }
        last_btn_state = btn_state;

        // ON/OFF State Change
        if (is_pwm_on && !last_on_state) {
            uint32_t duration = (uint32_t)(target_duty * soft_start_ms); // 0 to target_duty
            sim_printf("[Main] PWM Turning ON. Soft-start to %.2f over %lu ms\n", target_duty, duration);
            current_freq = target_freq;
            pspwm_set_frequency(MCPWM_UNIT_0, current_freq);
            pspwm_set_ps_duty(MCPWM_UNIT_0, 0.0f);
            pspwm_resync_enable_output(MCPWM_UNIT_0);
            pspwm_set_duty_soft(MCPWM_UNIT_0, target_duty, duration);
            last_on_state = true;
            current_duty = target_duty;
        } else if (!is_pwm_on && last_on_state) {
            sim_printf("[Main] PWM Turning OFF. Normal stop.\n");
            pspwm_set_ps_duty(MCPWM_UNIT_0, 0.0f);
            vTaskDelay(pdMS_TO_TICKS(5));
            pspwm_disable_output(MCPWM_UNIT_0);
            pspwm_set_duty_soft(MCPWM_UNIT_0, 0.0f, 0);
            last_on_state = false;
        }
        
        // Update LED Status
#ifdef CONFIG_IDF_TARGET_ESP32S3
        if (is_pwm_on) {
            if (auto_track_en) {
                led_strip_set_pixel(s_led_strip, 0, 0, 0, 32); // Blue (Auto-tracking active)
            } else {
                led_strip_set_pixel(s_led_strip, 0, 0, 32, 0); // Green (PWM running)
            }
        } else {
            led_strip_set_pixel(s_led_strip, 0, 0, 0, 0); // Off (Idle)
        }
        led_strip_refresh(s_led_strip);
#endif

        // Frequency Update while ON
        if (is_pwm_on && current_freq != target_freq) {
            pspwm_set_frequency(MCPWM_UNIT_0, target_freq);
            current_freq = target_freq;
            if (!auto_track_en) {
                sim_printf("[Main] Frequency updated to %.0f Hz\n", current_freq);
            }
        }

        // Duty Update while ON
        if (is_pwm_on && current_duty != target_duty) {
            pspwm_setpoint_t* sp;
            pspwm_get_setpoint_ptr(MCPWM_UNIT_0, &sp);
            float actual_duty = sp->ps_duty;
            float diff = target_duty - actual_duty;
            if (diff < 0) diff = -diff;
            uint32_t duration = (uint32_t)(diff * soft_start_ms);
            
            pspwm_set_duty_soft(MCPWM_UNIT_0, target_duty, duration);
            current_duty = target_duty;
            sim_printf("[Main] Duty soft-updating to %.2f over %lu ms\n", target_duty, duration);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// --- 2. ZC Pulse Simulator Callback (Registered with Library) ---
static void IRAM_ATTR start_capture_sim_cb(uint32_t cap_val, void* arg) {
    // ZC Pulse Simulator: Triggered by LEAD Leg Capture callback from library
    if (simulated_pulse_width_us > 0) {
        esp_rom_delay_us(simulated_pulse_width_us);
    }
    gpio_set_level(GPIO_NUM_10, 1);
    esp_rom_delay_us(1); // 1us pulse width
    gpio_set_level(GPIO_NUM_10, 0);
}

// --- 2. Auto-Tracking PI Control Task ---
void freq_tracking_task(void *arg) {
    float last_error = 0.0f;
    
    while(1) {
        if (auto_track_en && is_pwm_on) {
            float current_delay = pspwm_get_measured_delay_us(MCPWM_UNIT_0);
            
            if (current_delay >= 0.0f) {
                float error = current_delay - target_zvs_delay;
                
                // --- PLL LOCK: Dead-band ---
                // 만약 오차가 ±0.1us 이내라면 완벽히 동기화(Lock) 된 것으로 간주하고 주파수를 고정합니다.
                if (error > -0.1f && error < 0.1f) {
                    error = 0.0f; 
                }
                
                // --- Incremental PI Control (속도형 PI 제어) ---
                // 현재 주파수에서 오차의 '변화량'만큼만 주파수를 미세 조정합니다.
                float delta_p = track_kp * (error - last_error);
                float delta_i = track_ki * error * 0.05f; // dt = 50ms
                
                // Calculate new frequency (Delay > Target -> Decrease Freq)
                float new_freq = target_freq - (delta_p + delta_i);
                last_error = error;
                
                // Safety Clamping
                if (new_freq > 25000.0f) new_freq = 25000.0f;
                if (new_freq < 15000.0f) new_freq = 15000.0f;
                
                target_freq = new_freq;
            }
        } else {
            last_error = 0.0f; // Reset when off
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // Run every 50ms
    }
}

// --- 3. Capture Monitoring Task (Read & Print) ---
void cap_monitor_task(void *arg) {
    // Enable resonant tracking capture inside the library (Start: GPIO 4, ZC: GPIO 9)
    ESP_ERROR_CHECK(pspwm_enable_tracking_capture(MCPWM_UNIT_0, GPIO_NUM_4, GPIO_NUM_9));
    
    // Register the start capture callback to simulate ZC pulses on GPIO 10
    pspwm_register_start_capture_callback(MCPWM_UNIT_0, start_capture_sim_cb, NULL);

    // Signal to main task that capture is ready, so it can initialize PWM generators next
    is_capture_ready = true;

    while(1) {
        if (is_pwm_on) {
            float delay_us = pspwm_get_measured_delay_us(MCPWM_UNIT_0);
            if (delay_us >= 0.0f) {
                if (auto_track_en) {
                    sim_printf("[Auto-Track] Freq: %.0f Hz | Meas Delay: %.2f us (Target Delay: %.2f us)\n", target_freq, delay_us, target_zvs_delay);
                } else {
                    sim_printf("[Capture] Freq: %.0f Hz | Sim ZC Width: %lu us | Measured Delay: %.2f us\n", target_freq, simulated_pulse_width_us, delay_us);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void print_help(void) {
    sim_printf("\n==========================================\n");
    sim_printf("Terminal Command Simulator Help:\n");
    sim_printf("  help or h or ? - Show this help menu\n");
    sim_printf("  on             - Turn PWM ON (with soft start)\n");
    sim_printf("  off            - Turn PWM OFF\n");
    sim_printf("  freq [val]     - Set Frequency (e.g. freq 20000)\n");
    sim_printf("  duty [val]     - Set target duty (e.g. duty 0.75)\n");
    sim_printf("  soft [val]     - 소프트 스타트 진입 시간 설정 ms (예: soft 2000)\n");
    sim_printf("  track on/off   - 자동 주파수 트래킹(PI 제어) 켜기/끄기\n");
    sim_printf("  width [val]    - 시뮬레이션용 ZC 펄스의 너비를 설정 (측정값에는 영향 없음) (예: width 5)\n");
    sim_printf("  target [val]   - 목표 ZC 딜레이(us) 설정. (현재 측정값보다 높으면 Freq UP, 낮으면 DOWN) (예: target 5.0)\n");
    sim_printf("  kp [val]       - 비례 제어 이득. 높이면 반응은 빠르나 널뛰기(발진) 위험 (예: kp 50)\n");
    sim_printf("  ki [val]       - 적분 제어 이득. 높이면 목표 도달 속도가 빨라짐 (예: ki 5)\n");
    sim_printf("==========================================\n\n");
}

// --- 3. Terminal Command Task ---
void terminal_cmd_task(void *arg) {
    // Set stdin to non-blocking to prevent USB-JTAG CDC lockups
    int flags = fcntl(fileno(stdin), F_GETFL);
    fcntl(fileno(stdin), F_SETFL, flags | O_NONBLOCK);

    print_help();

    while (1) {
        int c = getchar();
        if (c != EOF) {
            xSemaphoreTake(print_mux, portMAX_DELAY);
            // 터미널 에코(Echo) 및 백스페이스 처리
            if (c == '\b' || c == 127) {
                if (rx_idx > 0) {
                    rx_idx--;
                    rx_buffer[rx_idx] = '\0';
                    printf("\b \b"); // 화면에서 글자 지우기
                    fflush(stdout);
                }
            } else if (c == '\n' || c == '\r') {
                printf("\r\n"); // 줄바꿈 에코
                if (rx_idx > 0) {
                    char cmd[128];
                    strcpy(cmd, rx_buffer);
                    rx_buffer[0] = '\0';
                    rx_idx = 0;
                    
                    // 명령어 처리를 위해 락 해제
                    xSemaphoreGive(print_mux);
                    
                    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0 || strcmp(cmd, "?") == 0) {
                        print_help();
                    } else if (strcmp(cmd, "on") == 0) {
                        is_pwm_on = true;
                        sim_printf("Cmd: Turn ON\n");
                    } else if (strcmp(cmd, "off") == 0) {
                        is_pwm_on = false;
                        sim_printf("Cmd: Turn OFF\n");
                    } else if (strncmp(cmd, "freq ", 5) == 0) {
                        sscanf(cmd + 5, "%f", &target_freq);
                        if (target_freq < 15000) target_freq = 15000;
                        if (target_freq > 25000) target_freq = 25000;
                        sim_printf("Cmd: target_freq set to %.0f\n", target_freq);
                    } else if (strncmp(cmd, "duty ", 5) == 0) {
                        sscanf(cmd + 5, "%f", &target_duty);
                        sim_printf("Cmd: target_duty set to %.2f\n", target_duty);
                    } else if (strncmp(cmd, "width ", 6) == 0) {
                        sscanf(cmd + 6, "%lu", &simulated_pulse_width_us);
                        sim_printf("Cmd: simulated_pulse_width_us set to %lu us\n", simulated_pulse_width_us);
                    } else if (strncmp(cmd, "soft ", 5) == 0) {
                        sscanf(cmd + 5, "%lu", &soft_start_ms);
                        sim_printf("Cmd: soft_start_ms set to %lu ms\n", soft_start_ms);
                    } else if (strcmp(cmd, "track on") == 0) {
                        auto_track_en = true;
                        sim_printf("Cmd: Auto-Tracking ON\n");
                    } else if (strcmp(cmd, "track off") == 0) {
                        auto_track_en = false;
                        sim_printf("Cmd: Auto-Tracking OFF\n");
                    } else if (strncmp(cmd, "target ", 7) == 0) {
                        sscanf(cmd + 7, "%f", &target_zvs_delay);
                        sim_printf("Cmd: target_zvs_delay set to %.2f us\n", target_zvs_delay);
                    } else if (strncmp(cmd, "kp ", 3) == 0) {
                        sscanf(cmd + 3, "%f", &track_kp);
                        sim_printf("Cmd: track_kp set to %.1f\n", track_kp);
                    } else if (strncmp(cmd, "ki ", 3) == 0) {
                        sscanf(cmd + 3, "%f", &track_ki);
                        sim_printf("Cmd: track_ki set to %.1f\n", track_ki);
                    } else {
                        sim_printf("Unknown Command: %s\n", cmd);
                    }
                    continue; // 락이 이미 반환되었으므로 다시 시작
                }
            } else if (rx_idx < sizeof(rx_buffer) - 1) {
                putchar(c); // 입력한 글자 화면에 출력
                fflush(stdout);
                rx_buffer[rx_idx++] = (char)c;
                rx_buffer[rx_idx] = '\0';
            }
            xSemaphoreGive(print_mux);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// Global start function to be called from app_main
void start_simulation_tasks(void) {
    if (print_mux == NULL) {
        print_mux = xSemaphoreCreateMutex();
    }
    xTaskCreate(ps_pwm_main_task, "ps_pwm_main", 4096, NULL, 5, NULL);
    xTaskCreate(freq_tracking_task, "freq_track", 4096, NULL, 5, NULL);
    xTaskCreate(cap_monitor_task, "cap_monitor", 4096, NULL, 4, NULL);
    xTaskCreate(terminal_cmd_task, "terminal_cmd", 4096, NULL, 3, NULL);
}
