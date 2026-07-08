/** @brief ESP32 Phase-Shift-PWM Example (Migrated to ESP-IDF v5.3.4)
 * 
 * Uses the driver for the MCPWM hardware modules on the Espressif ESP32
 * or ESP32-S3 SoC for generating a Phase-Shift-PWM waveform between
 * two pairs of hardware pins. (Not compatible with ESP32-S2)
 * 
 * Application in power electronics, e.g. Zero-Voltage-Switching (ZVS)
 * Full-Bridge-, Dual-Active-Bridge- and LLC converters.
 *
 * 2021-05-21 Ulrich Lukas (Original v4.x author)
 * 
 * @note Modified to support ESP32-S3, tested with ESP32-S3-DevKitC-1
 * @note Migrated from ESP-IDF SDK v4.4.7 to v5.3.4
 * @note Added robust Hardware Fault (OST) latching and manual recovery via GPIO 0
 * @note Added interactive GPIO 0 button for safe PWM Enable/Disable using Soft Faults
 * @note Added WS2812 LED status indicator (Idle/Running/Fault)
 * @note Added Resonant Frequency Auto-Tracking (PI Control with PLL Lock) via Capture Timer
 *
 * 2024-05-24 Yoonki Kim (Initial v4.x mod)
 * 2026-07-02 Yoonki Kim (v5.3.4 Migration & Safety features)
 * 2026-07-08 Yoonki Kim (v1.1.0 - Auto-Tracking PI Control Loop with PLL Lock)
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"

#include "driver/gpio.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include "led_strip.h"
#endif

#include "ps_pwm.h"

void initialize_phase_shift_pwm()
{
    ///////////////////////////// Configuration ////////////////////////////////
    // MCPWM unit can be [0,1]
    mcpwm_unit_t mcpwm_num = MCPWM_UNIT_0;

#ifdef CONFIG_IDF_TARGET_ESP32
    // GPIO config for PWM output
    gpio_num_t gpio_pwm0a_out = GPIO_NUM_27; // PWM0A := LEAD leg, Low Side
    gpio_num_t gpio_pwm0b_out = GPIO_NUM_26; // PWM0B := LEAD leg, High Side
    gpio_num_t gpio_pwm1a_out = GPIO_NUM_25; // PWM1A := LAG leg, Low Side
    gpio_num_t gpio_pwm1b_out = GPIO_NUM_33; // PWM1B := LAG leg, High Side
    // Shutdown/fault input for PWM outputs
    gpio_num_t gpio_fault_shutdown = GPIO_NUM_4;
#elif CONFIG_IDF_TARGET_ESP32S3
    // GPIO config for PWM output
    gpio_num_t gpio_pwm0a_out = GPIO_NUM_5; // DRV_B, PWM0A := LEAD leg, Low Side
    gpio_num_t gpio_pwm0b_out = GPIO_NUM_4; // DRV_A, PWM0B := LEAD leg, High Side
    gpio_num_t gpio_pwm1a_out = GPIO_NUM_7; // DRV_D, PWM1A := LAG leg, Low Side
    gpio_num_t gpio_pwm1b_out = GPIO_NUM_6; // DRV_C, PWM1B := LAG leg, High Side
    // Shutdown/fault input for PWM outputs
    gpio_num_t gpio_fault_shutdown = GPIO_NUM_8;    // OCP_PULSE, Pulse by Pulse protect
#endif

    // Active low / active high selection for fault input pin
    mcpwm_fault_input_level_t fault_pin_active_level = MCPWM_LOW_LEVEL_TGR;
    // Define here if the output pins shall be forced low or high
    // or high-impedance when a fault condition is triggered.
    // PWMxA and PWMxB have the same type of action, see declaration in mcpwm.h
    mcpwm_action_on_pwmxa_t disable_action_lag_leg = MCPWM_FORCE_MCPWMXA_LOW;
    // Lead leg might have a different configuration, e.g. stay at last output level
    mcpwm_action_on_pwmxa_t disable_action_lead_leg = MCPWM_FORCE_MCPWMXA_LOW;

    float init_frequency = 100e3f;
    // Initial phase-shift setpoint
    float init_ps_duty = 0.0f; // Started from 0.0 for soft start test
    // Initial leading leg dead-time value in ns
    float init_lead_dt = 125e-9f;
    // Initial lagging leg dead-time value in ns
    float init_lag_dt = 125e-9f;
    // Initial output state is "true" representing "on"
    bool init_power_pwm_active = true;
    ////////////////////////////////////////////////////////////////////////////

    printf("Configuring Phase-Shift-PWM...\n");
    esp_err_t errors = pspwm_init_symmetrical(mcpwm_num,
                                              gpio_pwm0a_out,
                                              gpio_pwm0b_out,
                                              gpio_pwm1a_out,
                                              gpio_pwm1b_out,
                                              init_frequency,
                                              init_ps_duty,
                                              init_lead_dt,
                                              init_lag_dt,
                                              init_power_pwm_active,
                                              disable_action_lead_leg,
                                              disable_action_lag_leg);
    // Pull-up enabled for avoiding shutdown on start
    gpio_pullup_en(gpio_fault_shutdown);
    // Give the pull-up some time to pull the pin high
    vTaskDelay(pdMS_TO_TICKS(10));

    // Enable fault shutdown input, low level disables output.
    errors |= pspwm_enable_hw_fault_shutdown(mcpwm_num,
                                             gpio_fault_shutdown,
                                             fault_pin_active_level);

    // Clear any fault that might have been triggered during initialization
    pspwm_clear_hw_fault_shutdown_occurred(mcpwm_num);
    
    // Explicitly enable output
    pspwm_resync_enable_output(mcpwm_num);

    if (errors != ESP_OK) {
        printf("Error initializing the PS-PWM module!\n");
        abort();
    }
}


/**
 * @brief Configure MCPWM module for generating a phase-shifted PWM waveform
 */
void mcpwm_example_ps_pwm(void *arg)
{
    initialize_phase_shift_pwm();

#ifdef CONFIG_IDF_TARGET_ESP32
    #define LED_PIN GPIO_NUM_2
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
#elif CONFIG_IDF_TARGET_ESP32S3
    #define LED_PIN GPIO_NUM_48
    led_strip_handle_t led_strip;
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_PIN,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
#endif

    #define BUTTON_PIN GPIO_NUM_0
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);

    bool is_output_enabled = true;
    int last_btn_state = 1;
    uint32_t loop_counter = 0;
    bool freq_100k = true;

    while (1) {
        // 1. Check if a hardware fault occurred
        if (pspwm_get_hw_fault_shutdown_occurred(MCPWM_UNIT_0)) {
            printf("\n=======================================================\n");
            printf("CRITICAL ERROR: Hardware Fault Detected on GPIO 8!\n");
            printf("Outputs are securely latched LOW.\n");
            printf("Please inspect hardware and press BOOT button (GPIO 0) to clear the fault.\n");
            printf("=======================================================\n\n");
            
            // Latch until button is pressed AND fault condition is physically cleared
            bool fault_cleared = false;
            uint32_t fault_loop_cnt = 0;
            while (!fault_cleared) {
                // Blink Red LED for warning
                if (fault_loop_cnt % 50 == 0) {
#ifdef CONFIG_IDF_TARGET_ESP32S3
                    if ((fault_loop_cnt / 50) % 2 == 0) {
                        led_strip_set_pixel(led_strip, 0, 32, 0, 0); // Red
                    } else {
                        led_strip_set_pixel(led_strip, 0, 0, 0, 0); // Off
                    }
                    led_strip_refresh(led_strip);
#endif
                }
                
                int btn_state = gpio_get_level(BUTTON_PIN);
                if (btn_state == 0 && last_btn_state == 1) { // Button pressed
                    if (pspwm_get_hw_fault_shutdown_present(MCPWM_UNIT_0)) {
                        printf("Cannot clear fault: Hardware fault condition (GPIO 8 LOW) is still physically present!\n");
                    } else {
                        printf("Fault cleared by user. Returning to Disabled state.\n");
                        // 1. Activate the soft fault FIRST to ensure the outputs remain securely OFF
                        pspwm_disable_output(MCPWM_UNIT_0);
                        // 2. Then clear the hardware fault latch. The outputs will stay OFF because soft fault is active.
                        pspwm_clear_hw_fault_shutdown_occurred(MCPWM_UNIT_0);
                        is_output_enabled = false;
                        fault_cleared = true;
                    }
                    vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
                }
                last_btn_state = btn_state;

                fault_loop_cnt++;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            continue; // Go back to start of main loop
        }

        // 2. Poll button for toggle
        int btn_state = gpio_get_level(BUTTON_PIN);
        if (btn_state == 0 && last_btn_state == 1) { // Button pressed
            if (is_output_enabled) {
                printf("BUTTON PRESSED: Disabling PWM Output\n");
                pspwm_disable_output(MCPWM_UNIT_0);
                pspwm_set_duty_soft(MCPWM_UNIT_0, 0.0f, 0); // Stop soft-start task & reset duty to 0%
                is_output_enabled = false;
            } else {
                printf("BUTTON PRESSED: Enabling PWM Output\n");
                pspwm_set_ps_duty(MCPWM_UNIT_0, 0.0f);      // Ensure it starts exactly from 0%
                pspwm_resync_enable_output(MCPWM_UNIT_0);
                pspwm_set_duty_soft(MCPWM_UNIT_0, 1.0f, 10000); // Trigger 10s soft-start to 100%
                is_output_enabled = true;
            }
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
        }
        last_btn_state = btn_state;

        // 3. Switch Frequency every ~3 seconds (300 * 10ms)
        if (loop_counter % 300 == 0) {
            if (is_output_enabled) {
                if (freq_100k) {
                    printf("STATUS: Phase-Shift PWM Active | Frequency: 100 kHz | Status LED: GREEN\n");
#ifdef CONFIG_IDF_TARGET_ESP32
                    gpio_set_level(LED_PIN, 1);
#elif CONFIG_IDF_TARGET_ESP32S3
                    led_strip_set_pixel(led_strip, 0, 0, 32, 0); // Green
                    led_strip_refresh(led_strip);
#endif
                    pspwm_set_frequency(MCPWM_UNIT_0, 100e3); // 100 kHz
                    freq_100k = false;
                } else {
                    printf("STATUS: Phase-Shift PWM Active | Frequency: 200 kHz | Status LED: BLUE\n");
#ifdef CONFIG_IDF_TARGET_ESP32
                    gpio_set_level(LED_PIN, 0);
#elif CONFIG_IDF_TARGET_ESP32S3
                    led_strip_set_pixel(led_strip, 0, 0, 0, 32); // Blue
                    led_strip_refresh(led_strip);
#endif
                    pspwm_set_frequency(MCPWM_UNIT_0, 200e3); // 200 kHz
                    freq_100k = true;
                }
            } else {
                // If output is disabled, ensure the LED is off and print idle status once every 3 sec
                printf("STATUS: PWM Disabled | System Idle | Status LED: OFF\n");
#ifdef CONFIG_IDF_TARGET_ESP32
                gpio_set_level(LED_PIN, 0);
#elif CONFIG_IDF_TARGET_ESP32S3
                led_strip_set_pixel(led_strip, 0, 0, 0, 0); // Off
                led_strip_refresh(led_strip);
#endif
            }
        }
        
        loop_counter++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern void start_simulation_tasks(void);

void app_main(void)
{
    printf("Observer output pins using oscilloscope.......\n");
    // xTaskCreate(mcpwm_example_ps_pwm, "mcpwm_example_ps_pwm", 4096, NULL, 5, NULL);
    
    // Start Interactive Simulation Mode
    start_simulation_tasks();
}
