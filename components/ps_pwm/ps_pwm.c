/*
 * PSPWM API Layer (v5.x Refactored)
 *
 * This file implements the public API (ps_pwm.h).
 * It manages state (setpoints, limits) and calls the
 * internal Hardware Abstraction Layer (HAL) functions (ps_pwm_hal.h)
 * to perform hardware operations.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // For portMUX_TYPE
#include "ps_pwm.h"
#include "ps_pwm_hal.h" // Include the new internal HAL header
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "ps_pwm.c";

// --- Global State Management ---
// We keep the state management logic from v4.x

// Array of pointers to setpoint structures, one for each MCPWM group (0 and 1)
static pspwm_setpoint_t* s_setpoints[2] = {NULL, NULL};
// Array of pointers to setpoint limit structures
static pspwm_setpoint_limits_t* s_setpoint_limits[2] = {NULL, NULL};
// Array of pointers to HAL context (handles), one for each group
static pspwm_hal_context_t* s_hal_context[2] = {NULL, NULL};

// Timer clock settings (shared)
static pspwm_clk_conf_t s_clk_conf = {
    .base_clk_prescale = BASE_CLK_PRESCALE_DEFAULT,
    .timer_clk_prescale = TIMER_CLK_PRESCALE_DEFAULT,
    .base_clk = (float) MCPWM_INPUT_CLK / BASE_CLK_PRESCALE_DEFAULT,
    .timer_clk = (float) MCPWM_INPUT_CLK / (
            BASE_CLK_PRESCALE_DEFAULT * TIMER_CLK_PRESCALE_DEFAULT)
};

// Global flag for hardware fault (set by ISR)
static volatile bool ost_fault_event_occurred[2] = {false, false};

// Spinlock for thread-safe access to shared setpoints
static portMUX_TYPE mcpwm_spinlock = portMUX_INITIALIZER_UNLOCKED;

// --- Internal ISR Handler ---
// This will be registered by the HAL
static void IRAM_ATTR pspwm_isr_handler(void* arg)
{
    int group_id = (int)arg;
    
    // Set the flag for the corresponding group
    if (group_id == 0 || group_id == 1) {
        ost_fault_event_occurred[group_id] = true;
    }
    
    // TODO: Add logic to read ISR status from v5 registers if needed
    // For now, we just set the flag.
}

/*
 *******************************************************************************
 * PUBLIC API IMPLEMENTATION (PASS-THROUGH TO HAL)
 *******************************************************************************
 */

esp_err_t pspwm_init(int group_id,
                     int gpio_lead_a, int gpio_lead_b,
                     int gpio_lag_a, int gpio_lag_b,
                     float frequency, float ps_duty,
                     float lead_red, float lead_fed,
                     float lag_red, float lag_fed,
                     bool output_enabled,
                     mcpwm_generator_action_t disable_action_lead_leg,
                     mcpwm_generator_action_t disable_action_lag_leg)
{
    ESP_LOGD(TAG, "Call pspwm_init for group %d", group_id);
    ESP_RETURN_ON_FALSE(group_id == 0 || group_id == 1, ESP_ERR_INVALID_ARG, TAG, "group_id must be 0 or 1");

    // 1. Allocate memory for state, limits, and HAL context
    portENTER_CRITICAL(&mcpwm_spinlock);
    if (!s_setpoints[group_id]) {
        s_setpoints[group_id] = calloc(1, sizeof(pspwm_setpoint_t));
    }
    if (!s_setpoint_limits[group_id]) {
        s_setpoint_limits[group_id] = calloc(1, sizeof(pspwm_setpoint_limits_t));
    }
    if (!s_hal_context[group_id]) {
        s_hal_context[group_id] = calloc(1, sizeof(pspwm_hal_context_t));
    }
    portEXIT_CRITICAL(&mcpwm_spinlock);
    
    ESP_RETURN_ON_FALSE(s_setpoints[group_id] && s_setpoint_limits[group_id] && s_hal_context[group_id], 
                        ESP_ERR_NO_MEM, TAG, "Failed to allocate memory");

    // 2. Calculate and check limits (v4.x logic)
    s_setpoint_limits[group_id]->frequency_min = s_clk_conf.timer_clk / (float)UINT16_MAX;
    s_setpoint_limits[group_id]->frequency_max = s_clk_conf.timer_clk / period_min;
    // TODO: This max dead time calculation might need review for v5.x
    if ((float)UINT16_MAX / s_clk_conf.base_clk > 1.0f / frequency) {
        s_setpoint_limits[group_id]->dt_sum_max = 1.0f / frequency;
    } else {
        s_setpoint_limits[group_id]->dt_sum_max = (float)UINT16_MAX / s_clk_conf.base_clk;
    }
    
    ESP_RETURN_ON_FALSE(frequency > s_setpoint_limits[group_id]->frequency_min && frequency <= s_setpoint_limits[group_id]->frequency_max,
                        ESP_ERR_INVALID_ARG, TAG, "Frequency setpoint out of range");
    ESP_RETURN_ON_FALSE(ps_duty >= 0.0f && ps_duty <= 1.0f, ESP_ERR_INVALID_ARG, TAG, "Invalid setpoint value for ps_duty");
    ESP_RETURN_ON_FALSE(!(lead_red < 0.0f || lead_fed < 0.0f || lag_red < 0.0f || lag_fed < 0.0f
                          || lead_red + lead_fed >= s_setpoint_limits[group_id]->dt_sum_max
                          || lag_red + lag_fed >= s_setpoint_limits[group_id]->dt_sum_max),
                        ESP_ERR_INVALID_ARG, TAG, "Dead time setpoint out of range");

    // 3. Store initial setpoints
    s_setpoints[group_id]->frequency = frequency;
    s_setpoints[group_id]->ps_duty = ps_duty;
    s_setpoints[group_id]->lead_red = lead_red;
    s_setpoints[group_id]->lead_fed = lead_fed;
    s_setpoints[group_id]->lag_red = lag_red;
    s_setpoints[group_id]->lag_fed = lag_fed;
    s_setpoints[group_id]->output_enabled = output_enabled;

    // 4. Call HAL to initialize hardware
    ESP_RETURN_ON_ERROR(hal_pspwm_init(group_id, s_setpoints[group_id], s_hal_context[group_id],
                                       gpio_lead_a, gpio_lead_b, gpio_lag_a, gpio_lag_b,
                                       disable_action_lead_leg, disable_action_lag_leg),
                        TAG, "HAL init failed");

    // 5. Set initial values
    ESP_RETURN_ON_ERROR(pspwm_set_frequency(group_id, frequency), TAG, "HAL set frequency failed");
    ESP_RETURN_ON_ERROR(pspwm_set_deadtimes(group_id, lead_red, lead_fed, lag_red, lag_fed), TAG, "HAL set deadtimes failed");
    ESP_RETURN_ON_ERROR(pspwm_set_ps_duty(group_id, ps_duty), TAG, "HAL set ps_duty failed");

    // 6. Enable output if requested
    if (output_enabled) {
        ESP_RETURN_ON_ERROR(pspwm_resync_enable_output(group_id), TAG, "HAL enable output failed");
    } else {
        ESP_RETURN_ON_ERROR(pspwm_disable_output(group_id), TAG, "HAL disable output failed");
    }

    ESP_LOGD(TAG, "pspwm_init OK for group %d", group_id);
    return ESP_OK;
}

esp_err_t pspwm_init_symmetrical(int group_id,
                                 int gpio_lead_a, int gpio_lead_b,
                                 int gpio_lag_a, int gpio_lag_b,
                                 float frequency, float ps_duty,
                                 float lead_dt, float lag_dt,
                                 bool output_enabled,
                                 mcpwm_generator_action_t disable_action_lead_leg,
                                 mcpwm_generator_action_t disable_action_lag_leg)
{
    // This is just a helper, call the main init function
    return pspwm_init(group_id,
                      gpio_lead_a, gpio_lead_b,
                      gpio_lag_a, gpio_lag_b,
                      frequency, ps_duty,
                      lead_dt, lead_dt, // Symmetrical
                      lag_dt, lag_dt, // Symmetrical
                      output_enabled,
                      disable_action_lead_leg,
                      disable_action_lag_leg);
}

esp_err_t pspwm_set_frequency(int group_id, float frequency)
{
    ESP_RETURN_ON_FALSE(group_id == 0 || group_id == 1, ESP_ERR_INVALID_ARG, TAG, "group_id must be 0 or 1");
    ESP_RETURN_ON_FALSE(s_setpoints[group_id], ESP_ERR_INVALID_STATE, TAG, "Not initialized");
    
    // Check limits
    ESP_RETURN_ON_FALSE(frequency > s_setpoint_limits[group_id]->frequency_min && frequency <= s_setpoint_limits[group_id]->frequency_max,
                        ESP_ERR_INVALID_ARG, TAG, "Frequency setpoint out of range");

    // Update setpoint
    portENTER_CRITICAL(&mcpwm_spinlock);
    s_setpoints[group_id]->frequency = frequency;
    portEXIT_CRITICAL(&mcpwm_spinlock);
    
    // Call HAL
    return hal_pspwm_set_frequency(s_setpoints[group_id], s_hal_context[group_id]);
}

esp_err_t pspwm_set_deadtimes(int group_id,
                              float lead_red, float lead_fed,
                              float lag_red, float lag_fed)
{
    ESP_RETURN_ON_FALSE(group_id == 0 || group_id == 1, ESP_ERR_INVALID_ARG, TAG, "group_id must be 0 or 1");
    ESP_RETURN_ON_FALSE(s_setpoints[group_id], ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    // Check limits
    // TODO: We need to read dt_sum_max from limits, which depends on frequency.
    // This check logic might need to be inside the HAL function.
    // For now, we trust the user.

    // Update setpoints
    portENTER_CRITICAL(&mcpwm_spinlock);
    s_setpoints[group_id]->lead_red = lead_red;
    s_setpoints[group_id]->lead_fed = lead_fed;
    s_setpoints[group_id]->lag_red = lag_red;
    s_setpoints[group_id]->lag_fed = lag_fed;
    portEXIT_CRITICAL(&mcpwm_spinlock);

    // Call HAL
    return hal_pspwm_set_deadtimes(s_setpoints[group_id], s_hal_context[group_id]);
}

esp_err_t pspwm_set_deadtimes_symmetrical(int group_id, float lead_dt, float lag_dt)
{
    return pspwm_set_deadtimes(group_id, lead_dt, lead_dt, lag_dt, lag_dt);
}

esp_err_t pspwm_set_ps_duty(int group_id, float ps_duty)
{
    ESP_RETURN_ON_FALSE(group_id == 0 || group_id == 1, ESP_ERR_INVALID_ARG, TAG, "group_id must be 0 or 1");
    ESP_RETURN_ON_FALSE(s_setpoints[group_id], ESP_ERR_INVALID_STATE, TAG, "Not initialized");
    ESP_RETURN_ON_FALSE(ps_duty >= 0.0f && ps_duty <= 1.0f, ESP_ERR_INVALID_ARG, TAG, "Invalid setpoint value for ps_duty");

    // Update setpoint
    portENTER_CRITICAL(&mcpwm_spinlock);
    s_setpoints[group_id]->ps_duty = ps_duty;
    portEXIT_CRITICAL(&mcpwm_spinlock);

    // Call HAL
    return hal_pspwm_set_ps_duty(s_setpoints[group_id], s_hal_context[group_id]);
}

/*
 *******************************************************************************
 * COMMON API IMPLEMENTATION (PASS-THROUGH TO HAL)
 *******************************************************************************
 */

bool pspwm_get_hw_fault_shutdown_present(int group_id) 
{
    if (group_id < 0 || group_id > 1 || !s_hal_context[group_id]) {
        return false;
    }
    // Call HAL
    return hal_pspwm_get_fault_status(s_hal_context[group_id]);
}

bool pspwm_get_hw_fault_shutdown_occurred(int group_id) 
{
    if (group_id < 0 || group_id > 1) {
        return false;
    }
    return ost_fault_event_occurred[group_id];
}

void pspwm_clear_hw_fault_shutdown_occurred(int group_id) 
{
    if (group_id == 0 || group_id == 1) {
        ost_fault_event_occurred[group_id] = false;
    }
}

esp_err_t pspwm_disable_output(int group_id)
{
    ESP_RETURN_ON_FALSE(group_id == 0 || group_id == 1, ESP_ERR_INVALID_ARG, TAG, "group_id must be 0 or 1");
    ESP_RETURN_ON_FALSE(s_hal_context[group_id], ESP_ERR_INVALID_STATE, TAG, "Not initialized");
    
    s_setpoints[group_id]->output_enabled = false;
    // Call HAL
    return hal_pspwm_disable_output(s_hal_context[group_id]);
}

esp_err_t pspwm_resync_enable_output(int group_id)
{
    ESP_RETURN_ON_FALSE(group_id == 0 || group_id == 1, ESP_ERR_INVALID_ARG, TAG, "group_id must be 0 or 1");
    ESP_RETURN_ON_FALSE(s_hal_context[group_id], ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    if (ost_fault_event_occurred[group_id]) {
        ESP_LOGE(TAG, "Shutdown flag must be cleared first before re-enabling the output!");
        return ESP_FAIL;
    }
    
    s_setpoints[group_id]->output_enabled = true;
    // Call HAL
    return hal_pspwm_resync_enable_output(s_setpoints[group_id], s_hal_context[group_id]);
}

esp_err_t pspwm_enable_hw_fault_shutdown(int group_id,
                                         int gpio_fault_shutdown,
                                         int fault_pin_active_level) 
{
    ESP_RETURN_ON_FALSE(group_id == 0 || group_id == 1, ESP_ERR_INVALID_ARG, TAG, "group_id must be 0 or 1");
    ESP_RETURN_ON_FALSE(s_hal_context[group_id], ESP_ERR_INVALID_STATE, TAG, "Not initialized");
    
    // Call HAL
    return hal_pspwm_enable_hw_fault(s_hal_context[group_id], gpio_fault_shutdown, fault_pin_active_level);
}

esp_err_t pspwm_disable_hw_fault_shutdown(int group_id, int gpio_fault_shutdown) 
{
    ESP_RETURN_ON_FALSE(group_id == 0 || group_id == 1, ESP_ERR_INVALID_ARG, TAG, "group_id must be 0 or 1");
    ESP_RETURN_ON_FALSE(s_hal_context[group_id], ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    // Call HAL
    return hal_pspwm_disable_hw_fault(s_hal_context[group_id], gpio_fault_shutdown);
}


esp_err_t pspwm_get_setpoint_ptr(int group_id, pspwm_setpoint_t** setpoints) 
{
    ESP_RETURN_ON_FALSE(group_id == 0 || group_id == 1, ESP_ERR_INVALID_ARG, TAG, "group_id must be 0 or 1");
    if (!s_setpoints[group_id]) {
        ESP_LOGE(TAG, "ERROR: The PMW unit must be initialised first!");
        return ESP_FAIL;
    }
    *setpoints = s_setpoints[group_id];
    return ESP_OK;
}

esp_err_t pspwm_get_setpoint_limits_ptr(int group_id, pspwm_setpoint_limits_t** setpoint_limits) 
{
    ESP_RETURN_ON_FALSE(group_id == 0 || group_id == 1, ESP_ERR_INVALID_ARG, TAG, "group_id must be 0 or 1");
    if (!s_setpoint_limits[group_id]) {
        ESP_LOGE(TAG, "ERROR: The PMW unit must be initialised first!");
        return ESP_FAIL;
    }
    *setpoint_limits = s_setpoint_limits[group_id];
    return ESP_OK;
}

esp_err_t pspwm_get_clk_conf_ptr(int group_id, pspwm_clk_conf_t** clk_conf) 
{
    // clk_conf is shared
    *clk_conf = &s_clk_conf;
    return ESP_OK;
}

// UP_DOWN_CTR_MODE is not implemented in this refactor
#ifdef PSPWM_USE_UP_DOWN_CTR_MODE_API
#error "UP_DOWN_CTR_MODE is not supported in v5.x refactor"
#endif //PSPWM_USE_UP_DOWN_CTR_MODE_API