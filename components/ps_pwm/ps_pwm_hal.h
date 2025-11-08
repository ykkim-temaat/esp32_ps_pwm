/*
 * PSPWM Hardware Abstraction Layer (Internal Header)
 *
 * This header defines the internal functions that the API layer (ps_pwm.c)
 * will call. The actual implementation (e.g., ps_pwm_hal_legacy.c)
 * will provide these functions using chip-specific v5.x APIs.
 */
#ifndef PS_PWM_HAL_H__
#define PS_PWM_HAL_H__

#include "ps_pwm.h" // Public header for types

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HAL Context
 * This struct will hold all v5.x driver handles
 */
typedef struct {
    mcpwm_timer_handle_t timer_lead; // Timer for Lead Leg
    mcpwm_timer_handle_t timer_lag;  // Timer for Lag Leg
    mcpwm_oper_handle_t oper_lead;  // Operator for Lead Leg
    mcpwm_oper_handle_t oper_lag;   // Operator for Lag Leg
    mcpwm_cmpr_handle_t cmpr_lead;  // Comparator for Lead Leg
    mcpwm_cmpr_handle_t cmpr_lag;   // Comparator for Lag Leg
    mcpwm_gen_handle_t gen_lead_a; // Generator A (High side)
    mcpwm_gen_handle_t gen_lead_b; // Generator B (Low side)
    mcpwm_gen_handle_t gen_lag_a;  // Generator A (High side)
    mcpwm_gen_handle_t gen_lag_b;  // Generator B (Low side)
    mcpwm_fault_handle_t fault_hw; // Hardware fault handle
    mcpwm_sync_handle_t sync_src_lead; // Sync source (from lead timer)
} pspwm_hal_context_t;


// HAL functions to be implemented by ps_pwm_hal_legacy.c
esp_err_t hal_pspwm_init(int group_id, pspwm_setpoint_t *setpoints, pspwm_hal_context_t *hal_ctx,
                         int gpio_lead_a, int gpio_lead_b, int gpio_lag_a, int gpio_lag_b,
                         mcpwm_generator_action_t disable_action_lead,
                         mcpwm_generator_action_t disable_action_lag);

esp_err_t hal_pspwm_set_frequency(pspwm_setpoint_t *setpoints, pspwm_hal_context_t *hal_ctx);
esp_err_t hal_pspwm_set_deadtimes(pspwm_setpoint_t *setpoints, pspwm_hal_context_t *hal_ctx);
esp_err_t hal_pspwm_set_ps_duty(pspwm_setpoint_t *setpoints, pspwm_hal_context_t *hal_ctx);

esp_err_t hal_pspwm_disable_output(pspwm_hal_context_t *hal_ctx);
esp_err_t hal_pspwm_resync_enable_output(pspwm_setpoint_t *setpoints, pspwm_hal_context_t *hal_ctx);

esp_err_t hal_pspwm_enable_hw_fault(pspwm_hal_context_t *hal_ctx, int gpio_fault, int active_level);
esp_err_t hal_pspwm_disable_hw_fault(pspwm_hal_context_t *hal_ctx, int gpio_fault);

bool hal_pspwm_get_fault_status(pspwm_hal_context_t *hal_ctx);

#ifdef __cplusplus
}
#endif

#endif // PS_PWM_HAL_H__