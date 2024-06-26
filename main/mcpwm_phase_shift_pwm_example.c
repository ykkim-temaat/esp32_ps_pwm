/** @brief ESP32 Phae-Shift-PWM Example
 * 
 * Uses the driver for the MCPWM hardware modules on the Espressif ESP32
 * or ESP32-S3 SoC for generating a Phase-Shift-PWM waveform between
 * two pairs of hardware pins. (Not compatible with ESP32-S2)
 * 
 * Application in power electronics, e.g. Zero-Voltage-Switching (ZVS)
 * Full-Bridge-, Dual-Active-Bridge- and LLC converters.
 *
 * 2021-05-21 Ulrich Lukas
 */

 /* Modified to support ESP32S3, tested with ESP32-S3-DevKitC-1
  *
  * @note This depends on the ESP-IDF SDK v4.4.7 version.
  *
  * 2024-05-24 Yoonki Kim
  */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"

#include "driver/mcpwm.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "soc/mcpwm_periph.h"

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
    float init_ps_duty = 0.45f;
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
    // Enable fault shutdown input, low level disables output.
    // Must then be reset manually by removing fault condition and then calling:
    // "pspwm_clear_hw_fault_shutdown_occurred(mcpwm_unit_t mcpwm_num);"
    // followed by:
    // "pspwm_resync_enable_output(mcpwm_unit_t mcpwm_num);"
    errors |= pspwm_enable_hw_fault_shutdown(mcpwm_num,
                                             gpio_fault_shutdown,
                                             fault_pin_active_level);
    // Pull-up enabled for avoiding shutdown on start
    errors |= gpio_pullup_en(gpio_fault_shutdown);

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
    gpio_pad_select_gpio(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
#elif CONFIG_IDF_TARGET_ESP32S3
    #define LED_PIN GPIO_NUM_48
    led_strip_t *pStrip_a;
    pStrip_a = led_strip_init(0, LED_PIN, 1);
    pStrip_a->clear(pStrip_a, 50);
#endif

    while (1) {

        pspwm_clear_hw_fault_shutdown_occurred(MCPWM_UNIT_0);
        pspwm_resync_enable_output(MCPWM_UNIT_0);

        // Switch frequency from time to time just for demonstration
        // vTaskDelay(3*configTICK_RATE_HZ);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
#ifdef CONFIG_IDF_TARGET_ESP32
        gpio_set_level(LED_PIN, 1);
#elif CONFIG_IDF_TARGET_ESP32S3         
        pStrip_a->set_pixel(pStrip_a, 0, 16, 16, 16);            
        pStrip_a->refresh(pStrip_a, 100);
#endif
        printf("pspwm_set_frequency 100kHz... \n");
        pspwm_set_frequency(MCPWM_UNIT_0, 100e3); // 100 kHz

        // vTaskDelay(3*configTICK_RATE_HZ);        
        vTaskDelay(3000 / portTICK_PERIOD_MS);
#ifdef CONFIG_IDF_TARGET_ESP32
        gpio_set_level(LED_PIN, 0);
#elif CONFIG_IDF_TARGET_ESP32S3
            pStrip_a->clear(pStrip_a, 50);
#endif
        printf("pspwm_set_frequency 200kHz... \n");
        pspwm_set_frequency(MCPWM_UNIT_0, 200e3); // 200 kHz
    }
}

void app_main(void)
{
    printf("Observer output pins using oscilloscope.......\n");
    xTaskCreate(mcpwm_example_ps_pwm, "mcpwm_example_ps_pwm", 4096, NULL, 5, NULL);
}
