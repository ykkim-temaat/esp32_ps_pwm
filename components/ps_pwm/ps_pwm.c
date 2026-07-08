#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdlib.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ps_pwm.h"

static const char *TAG = "ps_pwm.c";

// Internal driver state to keep track of allocated handle objects
typedef struct {
    mcpwm_timer_handle_t timers[2];       // [0]: lead (timer0), [1]: lag (timer1)
    mcpwm_oper_handle_t operators[2];     // [0]: lead, [1]: lag
    mcpwm_gen_handle_t generators[2][2];  // [op][gen] -> [0][0]: lead_a, [0][1]: lead_b, [1][0]: lag_a, [1][1]: lag_b
    mcpwm_cmpr_handle_t comparators[2];   // [0]: lead, [1]: lag
    mcpwm_sync_handle_t sync_src;         // Timer 0 sync output
    mcpwm_fault_handle_t fault_detect;    // GPIO Fault detector
    mcpwm_fault_handle_t soft_fault[2];   // Software Fault for disable_output (one per operator)
    int gpio_fault_shutdown;
    mcpwm_fault_input_level_t fault_pin_active_level;
    bool is_initialized;
    bool is_software_disabled;
} pspwm_state_t;

// Setpoint values globally shared for frequency, phase and dead-time
static pspwm_setpoint_t* s_setpoints[2] = {NULL, NULL};
static pspwm_setpoint_limits_t* s_setpoint_limits[2] = {NULL, NULL};

// Set to true by the operator OST callback when fault is triggered
static volatile bool ost_fault_event_occurred[2] = {false, false};

// Thread-safe state tracker for each MCPWM unit (group)
static pspwm_state_t s_states[2] = {
    { .is_initialized = false, .gpio_fault_shutdown = -1 },
    { .is_initialized = false, .gpio_fault_shutdown = -1 }
};

// Clock settings (base and timer resolution are set to 80 MHz)
static pspwm_clk_conf_t s_clk_conf = {
    .base_clk_prescale = 1,
    .timer_clk_prescale = 1,
    .base_clk = 80000000.0f,
    .timer_clk = 80000000.0f
};

// Interrupt callback triggered when One-Shot (OST) brake event occurs
static bool IRAM_ATTR pspwm_brake_ost_callback(mcpwm_oper_handle_t oper, const mcpwm_brake_event_data_t *edata, void *user_data) {
    int mcpwm_num = (int)(intptr_t)user_data;
    if (!s_states[mcpwm_num].is_software_disabled) {
        ost_fault_event_occurred[mcpwm_num] = true;
    }
    return false; // return false to not request task yield
}

/***************************** START API SECTION ******************************/


esp_err_t pspwm_init(mcpwm_unit_t mcpwm_num,
                     int gpio_lead_a,
                     int gpio_lead_b,
                     int gpio_lag_a,
                     int gpio_lag_b,
                     float frequency,
                     float ps_duty,
                     float lead_red,
                     float lead_fed,
                     float lag_red,
                     float lag_fed,
                     bool output_enabled,
                     mcpwm_action_on_pwmxa_t disable_action_lead_leg,
                     mcpwm_action_on_pwmxa_t disable_action_lag_leg)
{
    ESP_LOGD(TAG, "Call pspwm_init");
    if (mcpwm_num != MCPWM_UNIT_0 && mcpwm_num != MCPWM_UNIT_1) {
        ESP_LOGE(TAG, "mcpwm_num must be MCPWM_UNIT_0 or MCPWM_UNIT_1!");
        return ESP_FAIL;
    }

    // Allocate memory for setpoints if not already allocated
    if (!s_setpoints[mcpwm_num]) {
        s_setpoints[mcpwm_num] = malloc(sizeof(pspwm_setpoint_t));
        if (!s_setpoints[mcpwm_num]) {
            ESP_LOGE(TAG, "Malloc failure for setpoints!");
            return ESP_FAIL;
        }
    }
    if (!s_setpoint_limits[mcpwm_num]) {
        s_setpoint_limits[mcpwm_num] = malloc(sizeof(pspwm_setpoint_limits_t));
        if (!s_setpoint_limits[mcpwm_num]) {
            ESP_LOGE(TAG, "Malloc failure for limits!");
            return ESP_FAIL;
        }
    }

    // Calculate limit bounds
    s_setpoint_limits[mcpwm_num]->frequency_min = s_clk_conf.timer_clk / (float)UINT16_MAX;
    s_setpoint_limits[mcpwm_num]->frequency_max = s_clk_conf.timer_clk / period_min;

    if ((float)UINT16_MAX / s_clk_conf.base_clk > 1.0f / frequency) {
        s_setpoint_limits[mcpwm_num]->dt_sum_max = 1.0f / frequency;
    } else {
        s_setpoint_limits[mcpwm_num]->dt_sum_max = (float)UINT16_MAX / s_clk_conf.base_clk;
    }

    ESP_LOGD(TAG, "frequency_min: %g, frequency_max: %g, dt_sum_max: %g",
             s_setpoint_limits[mcpwm_num]->frequency_min,
             s_setpoint_limits[mcpwm_num]->frequency_max,
             s_setpoint_limits[mcpwm_num]->dt_sum_max);

    // Range checks
    if (frequency <= s_setpoint_limits[mcpwm_num]->frequency_min || frequency > s_setpoint_limits[mcpwm_num]->frequency_max) {
        ESP_LOGE(TAG, "Frequency setpoint out of range!");
        return ESP_FAIL;
    }
    if (ps_duty < 0.0f || ps_duty > 1.0f) {
        ESP_LOGE(TAG, "Invalid setpoint value for ps_duty");
        return ESP_FAIL;
    }
    if (lead_red < 0.0f || lead_fed < 0.0f || lag_red < 0.0f || lag_fed < 0.0f
            || lead_red + lead_fed >= s_setpoint_limits[mcpwm_num]->dt_sum_max
            || lag_red + lag_fed >= s_setpoint_limits[mcpwm_num]->dt_sum_max) {
        ESP_LOGE(TAG, "Dead time setpoint out of range");
        return ESP_FAIL;
    }

    // Store settings in global struct
    s_setpoints[mcpwm_num]->frequency = frequency;
    s_setpoints[mcpwm_num]->ps_duty = ps_duty;
    s_setpoints[mcpwm_num]->lead_red = lead_red;
    s_setpoints[mcpwm_num]->lead_fed = lead_fed;
    s_setpoints[mcpwm_num]->lag_red = lag_red;
    s_setpoints[mcpwm_num]->lag_fed = lag_fed;
    s_setpoints[mcpwm_num]->output_enabled = output_enabled;

    // Clean up if already initialized to prevent resource leak
    if (s_states[mcpwm_num].is_initialized) {
        mcpwm_del_generator(s_states[mcpwm_num].generators[0][0]);
        mcpwm_del_generator(s_states[mcpwm_num].generators[0][1]);
        mcpwm_del_generator(s_states[mcpwm_num].generators[1][0]);
        mcpwm_del_generator(s_states[mcpwm_num].generators[1][1]);
        mcpwm_del_comparator(s_states[mcpwm_num].comparators[0]);
        mcpwm_del_comparator(s_states[mcpwm_num].comparators[1]);
        mcpwm_del_sync_src(s_states[mcpwm_num].sync_src);
        mcpwm_del_operator(s_states[mcpwm_num].operators[0]);
        mcpwm_del_operator(s_states[mcpwm_num].operators[1]);
        mcpwm_del_timer(s_states[mcpwm_num].timers[0]);
        mcpwm_del_timer(s_states[mcpwm_num].timers[1]);
        if (s_states[mcpwm_num].fault_detect) {
            mcpwm_del_fault(s_states[mcpwm_num].fault_detect);
            s_states[mcpwm_num].fault_detect = NULL;
        }
        s_states[mcpwm_num].is_initialized = false;
    }

    esp_err_t err = ESP_OK;
    float half_period = 0.5f * s_clk_conf.timer_clk / frequency;
    uint32_t period_ticks = (uint32_t)(2.0f * half_period);

    // 1. Configure Timers
    mcpwm_timer_config_t timer_cfg = {
        .group_id = mcpwm_num,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = (uint32_t)s_clk_conf.timer_clk,
        .period_ticks = period_ticks,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    err |= mcpwm_new_timer(&timer_cfg, &s_states[mcpwm_num].timers[0]);
    err |= mcpwm_new_timer(&timer_cfg, &s_states[mcpwm_num].timers[1]);

    // 2. Setup Synchronization
    // Timer 0 creates a sync output when counting reaches zero (empty)
    mcpwm_timer_sync_src_config_t sync_src_cfg = {
        .timer_event = MCPWM_TIMER_EVENT_EMPTY,
        .flags.propagate_input_sync = false,
    };
    err |= mcpwm_new_timer_sync_src(s_states[mcpwm_num].timers[0], &sync_src_cfg, &s_states[mcpwm_num].sync_src);

    // Timer 1 syncs with Timer 0 sync output with phase ticks loaded
    uint32_t phase_ticks = (uint32_t)(half_period * ps_duty);
    mcpwm_timer_sync_phase_config_t sync_phase_cfg = {
        .sync_src = s_states[mcpwm_num].sync_src,
        .count_value = phase_ticks,
        .direction = MCPWM_TIMER_DIRECTION_UP,
    };
    err |= mcpwm_timer_set_phase_on_sync(s_states[mcpwm_num].timers[1], &sync_phase_cfg);

    // 3. Configure Operators
    mcpwm_operator_config_t oper_cfg = {
        .group_id = mcpwm_num,
    };
    err |= mcpwm_new_operator(&oper_cfg, &s_states[mcpwm_num].operators[0]);
    err |= mcpwm_new_operator(&oper_cfg, &s_states[mcpwm_num].operators[1]);

    err |= mcpwm_operator_connect_timer(s_states[mcpwm_num].operators[0], s_states[mcpwm_num].timers[0]);
    err |= mcpwm_operator_connect_timer(s_states[mcpwm_num].operators[1], s_states[mcpwm_num].timers[1]);

    // 4. Configure Comparators
    mcpwm_comparator_config_t cmpr_cfg = {
        .flags.update_cmp_on_tez = true,
    };
    err |= mcpwm_new_comparator(s_states[mcpwm_num].operators[0], &cmpr_cfg, &s_states[mcpwm_num].comparators[0]);
    err |= mcpwm_new_comparator(s_states[mcpwm_num].operators[1], &cmpr_cfg, &s_states[mcpwm_num].comparators[1]);

    uint32_t cmpr_0_a = (uint32_t)(half_period + 0.5f * (s_clk_conf.timer_clk * (lead_red - lead_fed)));
    uint32_t cmpr_1_a = (uint32_t)(half_period + 0.5f * (s_clk_conf.timer_clk * (lag_red - lag_fed)));
    err |= mcpwm_comparator_set_compare_value(s_states[mcpwm_num].comparators[0], cmpr_0_a);
    err |= mcpwm_comparator_set_compare_value(s_states[mcpwm_num].comparators[1], cmpr_1_a);

    // 5. Configure Generators
    mcpwm_generator_config_t gen_cfg = {
        .gen_gpio_num = gpio_lead_a,
    };
    err |= mcpwm_new_generator(s_states[mcpwm_num].operators[0], &gen_cfg, &s_states[mcpwm_num].generators[0][0]);
    gen_cfg.gen_gpio_num = gpio_lead_b;
    err |= mcpwm_new_generator(s_states[mcpwm_num].operators[0], &gen_cfg, &s_states[mcpwm_num].generators[0][1]);

    gen_cfg.gen_gpio_num = gpio_lag_a;
    err |= mcpwm_new_generator(s_states[mcpwm_num].operators[1], &gen_cfg, &s_states[mcpwm_num].generators[1][0]);
    gen_cfg.gen_gpio_num = gpio_lag_b;
    err |= mcpwm_new_generator(s_states[mcpwm_num].operators[1], &gen_cfg, &s_states[mcpwm_num].generators[1][1]);

    // 6. Setup Generator Actions
    // For lead low side (generators[0][0]): Go HIGH on TEZ, Go LOW on Comparator 0 match
    err |= mcpwm_generator_set_action_on_timer_event(s_states[mcpwm_num].generators[0][0],
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
    err |= mcpwm_generator_set_action_on_compare_event(s_states[mcpwm_num].generators[0][0],
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_states[mcpwm_num].comparators[0], MCPWM_GEN_ACTION_LOW));

    // For lag low side (generators[1][0]): Go HIGH on TEZ, Go LOW on Comparator 1 match
    err |= mcpwm_generator_set_action_on_timer_event(s_states[mcpwm_num].generators[1][0],
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
    err |= mcpwm_generator_set_action_on_compare_event(s_states[mcpwm_num].generators[1][0],
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_states[mcpwm_num].comparators[1], MCPWM_GEN_ACTION_LOW));

    // 7. Setup Dead Time (makes Generator B complementary to Generator A with delay)
    mcpwm_dead_time_config_t dt_a = {
        .posedge_delay_ticks = (uint32_t)(lead_red * s_clk_conf.base_clk),
        .negedge_delay_ticks = 0,
        .flags.invert_output = false,
    };
    mcpwm_dead_time_config_t dt_b = {
        .posedge_delay_ticks = 0,
        .negedge_delay_ticks = (uint32_t)(lead_fed * s_clk_conf.base_clk),
        .flags.invert_output = true,
    };
    err |= mcpwm_generator_set_dead_time(s_states[mcpwm_num].generators[0][0], s_states[mcpwm_num].generators[0][0], &dt_a);
    err |= mcpwm_generator_set_dead_time(s_states[mcpwm_num].generators[0][0], s_states[mcpwm_num].generators[0][1], &dt_b);

    dt_a.posedge_delay_ticks = (uint32_t)(lag_red * s_clk_conf.base_clk);
    dt_b.negedge_delay_ticks = (uint32_t)(lag_fed * s_clk_conf.base_clk);
    err |= mcpwm_generator_set_dead_time(s_states[mcpwm_num].generators[1][0], s_states[mcpwm_num].generators[1][0], &dt_a);
    err |= mcpwm_generator_set_dead_time(s_states[mcpwm_num].generators[1][0], s_states[mcpwm_num].generators[1][1], &dt_b);

    // 8. Create a Soft Fault for securely disabling the PWM output (bypasses Dead-Time inversion)
    mcpwm_soft_fault_config_t soft_fault_config = {};
    err |= mcpwm_new_soft_fault(&soft_fault_config, &s_states[mcpwm_num].soft_fault[0]);
    err |= mcpwm_new_soft_fault(&soft_fault_config, &s_states[mcpwm_num].soft_fault[1]);
    mcpwm_brake_config_t soft_brake_config_0 = {
        .fault = s_states[mcpwm_num].soft_fault[0],
        .brake_mode = MCPWM_OPER_BRAKE_MODE_OST,
    };
    mcpwm_brake_config_t soft_brake_config_1 = {
        .fault = s_states[mcpwm_num].soft_fault[1],
        .brake_mode = MCPWM_OPER_BRAKE_MODE_OST,
    };
    err |= mcpwm_operator_set_brake_on_fault(s_states[mcpwm_num].operators[0], &soft_brake_config_0);
    err |= mcpwm_operator_set_brake_on_fault(s_states[mcpwm_num].operators[1], &soft_brake_config_1);

    // Set action for OST brake (which applies to both Soft Fault and future HW Fault)
    mcpwm_gen_brake_event_action_t brake_action = {
        .direction = MCPWM_TIMER_DIRECTION_UP,
        .brake_mode = MCPWM_OPER_BRAKE_MODE_OST,
        .action = MCPWM_GEN_ACTION_LOW,
    };
    err |= mcpwm_generator_set_action_on_brake_event(s_states[mcpwm_num].generators[0][0], brake_action);
    err |= mcpwm_generator_set_action_on_brake_event(s_states[mcpwm_num].generators[0][1], brake_action);
    err |= mcpwm_generator_set_action_on_brake_event(s_states[mcpwm_num].generators[1][0], brake_action);
    err |= mcpwm_generator_set_action_on_brake_event(s_states[mcpwm_num].generators[1][1], brake_action);

    // Disable output initially by triggering the soft fault to be safe
    err |= pspwm_disable_output(mcpwm_num);

    // Enable and Start Timers
    err |= mcpwm_timer_enable(s_states[mcpwm_num].timers[0]);
    err |= mcpwm_timer_enable(s_states[mcpwm_num].timers[1]);
    err |= mcpwm_timer_start_stop(s_states[mcpwm_num].timers[0], MCPWM_TIMER_START_NO_STOP);
    err |= mcpwm_timer_start_stop(s_states[mcpwm_num].timers[1], MCPWM_TIMER_START_NO_STOP);

    if (output_enabled) {
        err |= pspwm_resync_enable_output(mcpwm_num);
    }

    if (err == ESP_OK) {
        s_states[mcpwm_num].is_initialized = true;
        ESP_LOGI(TAG, "pspwm_init success!");
    } else {
        ESP_LOGE(TAG, "pspwm_init failed during setup!");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t pspwm_init_symmetrical(mcpwm_unit_t mcpwm_num,
                                 int gpio_lead_a,
                                 int gpio_lead_b,
                                 int gpio_lag_a,
                                 int gpio_lag_b,
                                 float frequency,
                                 float ps_duty,
                                 float lead_dt,
                                 float lag_dt,
                                 bool output_enabled,
                                 mcpwm_action_on_pwmxa_t disable_action_lead_leg,
                                 mcpwm_action_on_pwmxa_t disable_action_lag_leg)
{
    return pspwm_init(mcpwm_num,
                      gpio_lead_a, gpio_lead_b,
                      gpio_lag_a, gpio_lag_b,
                      frequency,
                      ps_duty,
                      lead_dt, lead_dt,
                      lag_dt, lag_dt,
                      output_enabled,
                      disable_action_lead_leg,
                      disable_action_lag_leg);
}

esp_err_t pspwm_set_frequency(mcpwm_unit_t mcpwm_num, float frequency)
{
    ESP_LOGD(TAG, "Call pspwm_set_frequency");
    pspwm_setpoint_t* setpoints = s_setpoints[mcpwm_num];
    assert(setpoints != NULL);

    if (frequency <= s_setpoint_limits[mcpwm_num]->frequency_min || frequency > s_setpoint_limits[mcpwm_num]->frequency_max) {
        ESP_LOGE(TAG, "Frequency setpoint out of range!");
        return ESP_FAIL;
    }

    setpoints->frequency = frequency;
    if ((float)UINT16_MAX / s_clk_conf.base_clk > 1.0f / frequency) {
        s_setpoint_limits[mcpwm_num]->dt_sum_max = 1.0f / frequency;
    } else {
        s_setpoint_limits[mcpwm_num]->dt_sum_max = (float)UINT16_MAX / s_clk_conf.base_clk;
    }

    float half_period = 0.5f * s_clk_conf.timer_clk / frequency;
    uint32_t period_ticks = (uint32_t)(2.0f * half_period);

    // Update timer periods
    mcpwm_timer_set_period(s_states[mcpwm_num].timers[0], period_ticks);
    mcpwm_timer_set_period(s_states[mcpwm_num].timers[1], period_ticks);

    // Update comparator values
    uint32_t cmpr_0_a = (uint32_t)(half_period + 0.5f * (s_clk_conf.timer_clk * (setpoints->lead_red - setpoints->lead_fed)));
    uint32_t cmpr_1_a = (uint32_t)(half_period + 0.5f * (s_clk_conf.timer_clk * (setpoints->lag_red - setpoints->lag_fed)));
    mcpwm_comparator_set_compare_value(s_states[mcpwm_num].comparators[0], cmpr_0_a);
    mcpwm_comparator_set_compare_value(s_states[mcpwm_num].comparators[1], cmpr_1_a);

    // Update sync phase Configuration
    uint32_t phase_ticks = (uint32_t)(half_period * setpoints->ps_duty);
    if (phase_ticks > cmpr_1_a) {
        phase_ticks = cmpr_1_a;
    }
    mcpwm_timer_sync_phase_config_t sync_phase_cfg = {
        .sync_src = s_states[mcpwm_num].sync_src,
        .count_value = phase_ticks,
        .direction = MCPWM_TIMER_DIRECTION_UP,
    };
    mcpwm_timer_set_phase_on_sync(s_states[mcpwm_num].timers[1], &sync_phase_cfg);

    return ESP_OK;
}

esp_err_t pspwm_set_deadtimes(mcpwm_unit_t mcpwm_num,
                              float lead_red,
                              float lead_fed,
                              float lag_red,
                              float lag_fed)
{
    ESP_LOGD(TAG, "Call pspwm_set_deadtimes()");
    pspwm_setpoint_t* setpoints = s_setpoints[mcpwm_num];
    assert(setpoints != NULL);

    if (lead_red < 0.0f || lead_fed < 0.0f || lag_red < 0.0f || lag_fed < 0.0f
            || lead_red + lead_fed >= s_setpoint_limits[mcpwm_num]->dt_sum_max
            || lag_red + lag_fed >= s_setpoint_limits[mcpwm_num]->dt_sum_max) {
        ESP_LOGE(TAG, "Dead time setpoint out of range");
        return ESP_FAIL;
    }

    setpoints->lead_red = lead_red;
    setpoints->lead_fed = lead_fed;
    setpoints->lag_red = lag_red;
    setpoints->lag_fed = lag_fed;

    mcpwm_dead_time_config_t dt_a = {
        .posedge_delay_ticks = (uint32_t)(lead_red * s_clk_conf.base_clk),
        .negedge_delay_ticks = 0,
        .flags.invert_output = false,
    };
    mcpwm_dead_time_config_t dt_b = {
        .posedge_delay_ticks = 0,
        .negedge_delay_ticks = (uint32_t)(lead_fed * s_clk_conf.base_clk),
        .flags.invert_output = true,
    };
    mcpwm_generator_set_dead_time(s_states[mcpwm_num].generators[0][0], s_states[mcpwm_num].generators[0][0], &dt_a);
    mcpwm_generator_set_dead_time(s_states[mcpwm_num].generators[0][0], s_states[mcpwm_num].generators[0][1], &dt_b);

    dt_a.posedge_delay_ticks = (uint32_t)(lag_red * s_clk_conf.base_clk);
    dt_b.negedge_delay_ticks = (uint32_t)(lag_fed * s_clk_conf.base_clk);
    mcpwm_generator_set_dead_time(s_states[mcpwm_num].generators[1][0], s_states[mcpwm_num].generators[1][0], &dt_a);
    mcpwm_generator_set_dead_time(s_states[mcpwm_num].generators[1][0], s_states[mcpwm_num].generators[1][1], &dt_b);

    // Recalculate comparators & sync phase
    float half_period = 0.5f * s_clk_conf.timer_clk / setpoints->frequency;
    uint32_t cmpr_0_a = (uint32_t)(half_period + 0.5f * (s_clk_conf.timer_clk * (lead_red - lead_fed)));
    uint32_t cmpr_1_a = (uint32_t)(half_period + 0.5f * (s_clk_conf.timer_clk * (lag_red - lag_fed)));
    mcpwm_comparator_set_compare_value(s_states[mcpwm_num].comparators[0], cmpr_0_a);
    mcpwm_comparator_set_compare_value(s_states[mcpwm_num].comparators[1], cmpr_1_a);

    uint32_t phase_ticks = (uint32_t)(half_period * setpoints->ps_duty);
    if (phase_ticks > cmpr_1_a) {
        phase_ticks = cmpr_1_a;
    }
    mcpwm_timer_sync_phase_config_t sync_phase_cfg = {
        .sync_src = s_states[mcpwm_num].sync_src,
        .count_value = phase_ticks,
        .direction = MCPWM_TIMER_DIRECTION_UP,
    };
    mcpwm_timer_set_phase_on_sync(s_states[mcpwm_num].timers[1], &sync_phase_cfg);

    return ESP_OK;
}

esp_err_t pspwm_set_deadtimes_symmetrical(mcpwm_unit_t mcpwm_num, float lead_dt, float lag_dt) {
    return pspwm_set_deadtimes(mcpwm_num, lead_dt, lead_dt, lag_dt, lag_dt);
}

esp_err_t pspwm_set_ps_duty(mcpwm_unit_t mcpwm_num, float ps_duty)
{
    ESP_LOGD(TAG, "Call pspwm_set_ps_duty");
    if (ps_duty < 0.0f || ps_duty > 1.0f) {
        ESP_LOGE(TAG, "Invalid setpoint value for ps_duty");
        return ESP_FAIL;
    }

    pspwm_setpoint_t* setpoints = s_setpoints[mcpwm_num];
    assert(setpoints != NULL);
    setpoints->ps_duty = ps_duty;

    float half_period = 0.5f * s_clk_conf.timer_clk / setpoints->frequency;
    uint32_t phase_ticks = (uint32_t)(half_period * ps_duty);

    uint32_t cmpr_1_a = (uint32_t)(half_period + 0.5f * (s_clk_conf.timer_clk * (setpoints->lag_red - setpoints->lag_fed)));
    if (phase_ticks > cmpr_1_a) {
        phase_ticks = cmpr_1_a;
    }

    mcpwm_timer_sync_phase_config_t sync_phase_cfg = {
        .sync_src = s_states[mcpwm_num].sync_src,
        .count_value = phase_ticks,
        .direction = MCPWM_TIMER_DIRECTION_UP,
    };
    mcpwm_timer_set_phase_on_sync(s_states[mcpwm_num].timers[1], &sync_phase_cfg);

    return ESP_OK;
}

typedef struct {
    mcpwm_unit_t mcpwm_num;
    float current_duty;
    float target_duty;
    uint32_t duration_ms;
    TaskHandle_t task_handle;
} pspwm_soft_start_ctx_t;

static pspwm_soft_start_ctx_t s_soft_start_ctx[2] = {
    {.task_handle = NULL},
    {.task_handle = NULL}
};

static void pspwm_soft_start_task(void *arg) {
    pspwm_soft_start_ctx_t *ctx = (pspwm_soft_start_ctx_t *)arg;
    uint32_t step_delay_ms = 10; // 10 ms per step
    uint32_t num_steps = ctx->duration_ms / step_delay_ms;
    if (num_steps == 0) num_steps = 1;
    
    float duty_step = (ctx->target_duty - ctx->current_duty) / (float)num_steps;
    
    for (uint32_t i = 0; i < num_steps; i++) {
        ctx->current_duty += duty_step;
        if (duty_step > 0 && ctx->current_duty > ctx->target_duty) ctx->current_duty = ctx->target_duty;
        if (duty_step < 0 && ctx->current_duty < ctx->target_duty) ctx->current_duty = ctx->target_duty;
        
        pspwm_set_ps_duty(ctx->mcpwm_num, ctx->current_duty);
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }
    
    // Ensure exact final target is set
    pspwm_set_ps_duty(ctx->mcpwm_num, ctx->target_duty);
    
    ctx->task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t pspwm_set_duty_soft(mcpwm_unit_t mcpwm_num, float target_duty, uint32_t duration_ms) {
    ESP_LOGD(TAG, "Call pspwm_set_duty_soft");
    if (target_duty < 0.0f || target_duty > 1.0f) {
        ESP_LOGE(TAG, "Invalid setpoint value for target_duty");
        return ESP_FAIL;
    }

    pspwm_setpoint_t* setpoints = s_setpoints[mcpwm_num];
    if (setpoints == NULL) {
        ESP_LOGE(TAG, "pspwm not initialized");
        return ESP_FAIL;
    }

    if (duration_ms == 0) {
        return pspwm_set_ps_duty(mcpwm_num, target_duty);
    }

    pspwm_soft_start_ctx_t *ctx = &s_soft_start_ctx[mcpwm_num];
    
    // If a soft start is already running for this unit, abort it
    if (ctx->task_handle != NULL) {
        vTaskDelete(ctx->task_handle);
        ctx->task_handle = NULL;
    }

    ctx->mcpwm_num = mcpwm_num;
    ctx->current_duty = setpoints->ps_duty;
    ctx->target_duty = target_duty;
    ctx->duration_ms = duration_ms;

    if (xTaskCreate(pspwm_soft_start_task, "pspwm_soft_start", 2048, ctx, 5, &ctx->task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create soft start task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool pspwm_get_hw_fault_shutdown_present(mcpwm_unit_t mcpwm_num) {
    if (!s_states[mcpwm_num].is_initialized || s_states[mcpwm_num].gpio_fault_shutdown < 0) {
        return false;
    }
    int level = gpio_get_level(s_states[mcpwm_num].gpio_fault_shutdown);
    if (s_states[mcpwm_num].fault_pin_active_level == MCPWM_LOW_LEVEL_TGR) {
        return (level == 0);
    } else {
        return (level == 1);
    }
}

bool pspwm_get_hw_fault_shutdown_occurred(mcpwm_unit_t mcpwm_num) {
    return ost_fault_event_occurred[mcpwm_num];
}

void pspwm_clear_hw_fault_shutdown_occurred(mcpwm_unit_t mcpwm_num) {
    ost_fault_event_occurred[mcpwm_num] = false;
    // Note: We DO NOT physically unlatch the OST brake here!
    // Unlatching the brake here would cause a glitch or continuous output
    // because the PWM would instantly resume.
    // The brake will be properly unlatched inside pspwm_resync_enable_output()
    // when the user actually requests the output to turn on.
}

esp_err_t pspwm_disable_output(mcpwm_unit_t mcpwm_num)
{
    ESP_LOGD(TAG, "Disabling output!");
    esp_err_t err = ESP_OK;
    s_states[mcpwm_num].is_software_disabled = true;
    if (s_states[mcpwm_num].soft_fault[0]) {
        err |= mcpwm_soft_fault_activate(s_states[mcpwm_num].soft_fault[0]);
    }
    if (s_states[mcpwm_num].soft_fault[1]) {
        err |= mcpwm_soft_fault_activate(s_states[mcpwm_num].soft_fault[1]);
    }
    pspwm_setpoint_t* setpoints = s_setpoints[mcpwm_num];
    assert(setpoints != NULL);
    setpoints->output_enabled = false;
    return err;
}

esp_err_t pspwm_resync_enable_output(mcpwm_unit_t mcpwm_num)
{
    ESP_LOGD(TAG, "Enabling output!");
    if (ost_fault_event_occurred[mcpwm_num]) {
        ESP_LOGE(TAG, "Shutdown flag must be cleared first before re-enabling the output!");
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    s_states[mcpwm_num].is_software_disabled = false;
    
    // Clear both the hardware fault latch and soft fault latches
    if (s_states[mcpwm_num].fault_detect) {
        err |= mcpwm_operator_recover_from_fault(s_states[mcpwm_num].operators[0], s_states[mcpwm_num].fault_detect);
        err |= mcpwm_operator_recover_from_fault(s_states[mcpwm_num].operators[1], s_states[mcpwm_num].fault_detect);
    }
    if (s_states[mcpwm_num].soft_fault[0]) {
        err |= mcpwm_operator_recover_from_fault(s_states[mcpwm_num].operators[0], s_states[mcpwm_num].soft_fault[0]);
    }
    if (s_states[mcpwm_num].soft_fault[1]) {
        err |= mcpwm_operator_recover_from_fault(s_states[mcpwm_num].operators[1], s_states[mcpwm_num].soft_fault[1]);
    }

    pspwm_setpoint_t* setpoints = s_setpoints[mcpwm_num];
    assert(setpoints != NULL);
    setpoints->output_enabled = true;
    return err;
}

esp_err_t pspwm_enable_hw_fault_shutdown(mcpwm_unit_t mcpwm_num,
                                         int gpio_fault_shutdown,
                                         mcpwm_fault_input_level_t fault_pin_active_level)
{
    ESP_LOGD(TAG, "Enabling hardware fault shutdown on GPIO: %d", gpio_fault_shutdown);
    s_states[mcpwm_num].gpio_fault_shutdown = gpio_fault_shutdown;
    s_states[mcpwm_num].fault_pin_active_level = fault_pin_active_level;

    // Create GPIO fault detector object
    mcpwm_gpio_fault_config_t fault_config = {
        .group_id = mcpwm_num,
        .gpio_num = gpio_fault_shutdown,
        .flags = {
            .active_level = (fault_pin_active_level == MCPWM_LOW_LEVEL_TGR) ? 0 : 1,
            .pull_up = true,
        }
    };
    esp_err_t err = mcpwm_new_gpio_fault(&fault_config, &s_states[mcpwm_num].fault_detect);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GPIO fault object!");
        return err;
    }

    // Connect brake to Operators
    mcpwm_brake_config_t brake_config = {
        .fault = s_states[mcpwm_num].fault_detect,
        .brake_mode = MCPWM_OPER_BRAKE_MODE_OST,
    };
    err |= mcpwm_operator_set_brake_on_fault(s_states[mcpwm_num].operators[0], &brake_config);
    err |= mcpwm_operator_set_brake_on_fault(s_states[mcpwm_num].operators[1], &brake_config);

    // Configure Generator actions on brake
    mcpwm_gen_brake_event_action_t brake_action = {
        .direction = MCPWM_TIMER_DIRECTION_UP,
        .brake_mode = MCPWM_OPER_BRAKE_MODE_OST,
        .action = MCPWM_GEN_ACTION_LOW,
    };
    err |= mcpwm_generator_set_action_on_brake_event(s_states[mcpwm_num].generators[0][0], brake_action);
    err |= mcpwm_generator_set_action_on_brake_event(s_states[mcpwm_num].generators[0][1], brake_action);
    err |= mcpwm_generator_set_action_on_brake_event(s_states[mcpwm_num].generators[1][0], brake_action);
    err |= mcpwm_generator_set_action_on_brake_event(s_states[mcpwm_num].generators[1][1], brake_action);

    // Register brake interrupts
    mcpwm_operator_event_callbacks_t cb = {
        .on_brake_ost = pspwm_brake_ost_callback,
    };
    err |= mcpwm_operator_register_event_callbacks(s_states[mcpwm_num].operators[0], &cb, (void*)(intptr_t)mcpwm_num);
    err |= mcpwm_operator_register_event_callbacks(s_states[mcpwm_num].operators[1], &cb, (void*)(intptr_t)mcpwm_num);

    return err;
}

esp_err_t pspwm_disable_hw_fault_shutdown(mcpwm_unit_t mcpwm_num,
                                          int gpio_fault_shutdown) {
    ESP_LOGD(TAG, "Resetting GPIO to default state: %d", gpio_fault_shutdown);
    esp_err_t err = ESP_OK;
    if (s_states[mcpwm_num].fault_detect) {
        // Disabling brake is handled by deleting the fault source handle
        err = mcpwm_del_fault(s_states[mcpwm_num].fault_detect);
        s_states[mcpwm_num].fault_detect = NULL;
    }
    s_states[mcpwm_num].gpio_fault_shutdown = -1;
    err |= gpio_reset_pin(gpio_fault_shutdown);
    return err;
}

esp_err_t pspwm_get_setpoint_ptr(mcpwm_unit_t mcpwm_num,
                                 pspwm_setpoint_t** setpoint) {
    if (!s_setpoints[mcpwm_num]) {
        ESP_LOGE(TAG, "ERROR: The PMW unit must be initialised first!");
        return ESP_FAIL;
    }
    *setpoint = s_setpoints[mcpwm_num];
    return ESP_OK;
}

esp_err_t pspwm_get_setpoint_limits_ptr(mcpwm_unit_t mcpwm_num,
                                        pspwm_setpoint_limits_t** setpoint_limits) {
    if (!s_setpoint_limits[mcpwm_num]) {
        ESP_LOGE(TAG, "ERROR: The PMW unit must be initialised first!");
        return ESP_FAIL;
    }
    *setpoint_limits = s_setpoint_limits[mcpwm_num];
    return ESP_OK;
}

esp_err_t pspwm_get_clk_conf_ptr(mcpwm_unit_t mcpwm_num,
                                 pspwm_clk_conf_t** clk_conf) {
    *clk_conf = &s_clk_conf;
    return ESP_OK;
}