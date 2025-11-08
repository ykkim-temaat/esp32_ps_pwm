/*
 * v5.x Refactored Header
 * - Replaced v4.x types (mcpwm_unit_t, etc.) with v5.x types or simple types (int)
 * - Included new v5.x modular headers
 */
#ifndef PS_PWM_H__
#define PS_PWM_H__

#include <stdbool.h> 
#include "esp_err.h"

// Include v5.x modular headers
#include "driver/mcpwm_timer.h"
#include "driver/mcpwm_oper.h"
#include "driver/mcpwm_cmpr.h"
#include "driver/mcpwm_gen.h"
#include "driver/mcpwm_fault.h"
#include "driver/mcpwm_sync.h"

// Set log level to ESP_LOG_INFO for production!
#define PS_PWM_LOG_LEVEL ESP_LOG_INFO

// Unscaled input clock frequency (This depends on the chip, but 160MHz is common for S3)
// Note: In v5.x, this is handled by the driver, but we keep it for calculations.
#define MCPWM_INPUT_CLK 160000000 // 160 MHz
#define BASE_CLK_PRESCALE_DEFAULT 1
#define TIMER_CLK_PRESCALE_DEFAULT 1
static const uint16_t period_min = 4;

#ifdef __cplusplus
extern "C" {
#endif

// pspwm_setpoint_t, pspwm_clk_conf_t, pspwm_setpoint_limits_t
// 이 구조체들은 라이브러리 내부용이므로 그대로 둡니다.
typedef struct {
    float frequency;
    float ps_duty;
    float lead_red;
    float lead_fed;
    float lag_red;
    float lag_fed;
    bool output_enabled;
} pspwm_setpoint_t;

typedef struct {
    uint8_t base_clk_prescale;
    uint8_t timer_clk_prescale;
    float base_clk;
    float timer_clk;
} pspwm_clk_conf_t;

typedef struct {
    float frequency_min;
    float frequency_max;
    float dt_sum_max;
} pspwm_setpoint_limits_t;


/*
 * API FUNCTION SIGNATURES REFACTORED FOR V5.X
 */

esp_err_t pspwm_init(int group_id, // <-- mcpwm_unit_t (v4) -> int (v5)
                     int gpio_lead_a,
                     int gpio_lead_b,
                     int gpio_lag_a,
                     int gpio_lag_b,
                     float frequency,
                     float ps_duty,
                     float lead_red, float lead_fed,
                     float lag_red, float lag_fed,
                     bool output_enabled,
                     mcpwm_generator_action_t disable_action_lead_leg, // <-- v5.x type
                     mcpwm_generator_action_t disable_action_lag_leg); // <-- v5.x type

esp_err_t pspwm_init_symmetrical(int group_id, // <-- mcpwm_unit_t (v4) -> int (v5)
                                 int gpio_lead_a,
                                 int gpio_lead_b,
                                 int gpio_lag_a,
                                 int gpio_lag_b,
                                 float frequency,
                                 float ps_duty,
                                 float lead_dt,
                                 float lag_dt,
                                 bool output_enabled,
                                 mcpwm_generator_action_t disable_action_lead_leg, // <-- v5.x type
                                 mcpwm_generator_action_t disable_action_lag_leg); // <-- v5.x type

esp_err_t pspwm_set_frequency(int group_id, float frequency); // <-- v5: int group_id

esp_err_t pspwm_set_deadtimes(int group_id, // <-- v5: int group_id
                              float lead_red,
                              float lead_fed,
                              float lag_red,
                              float lag_fed);

esp_err_t pspwm_set_deadtimes_symmetrical(int group_id, float lead_dt, float lag_dt); // <-- v5

esp_err_t pspwm_set_ps_duty(int group_id, float ps_duty); // <-- v5


/* COMMON SETUP */
bool pspwm_get_hw_fault_shutdown_present(int group_id); // <-- v5
bool pspwm_get_hw_fault_shutdown_occurred(int group_id); // <-- v5
void pspwm_clear_hw_fault_shutdown_occurred(int group_id); // <-- v5
esp_err_t pspwm_disable_output(int group_id); // <-- v5
esp_err_t pspwm_resync_enable_output(int group_id); // <-- v5

esp_err_t pspwm_enable_hw_fault_shutdown(int group_id, // <-- v5
                                         int gpio_fault_shutdown,
                                         int fault_pin_active_level); // <-- v4 type -> simple int

esp_err_t pspwm_disable_hw_fault_shutdown(int group_id, int gpio_fault_shutdown); // <-- v5

esp_err_t pspwm_get_setpoint_ptr(int group_id, pspwm_setpoint_t** setpoint); // <-- v5
esp_err_t pspwm_get_setpoint_limits_ptr(int group_id, pspwm_setpoint_limits_t** setpoint_limits); // <-- v5
esp_err_t pspwm_get_clk_conf_ptr(int group_id, pspwm_clk_conf_t** clk_conf); // <-- v5


// UP-DOWN COUNTER MODE API는 일단 무시 (ifdef로 비활성화 되어 있음)


#ifdef __cplusplus
}
#endif

#endif  /* PS_PWM_H__ */