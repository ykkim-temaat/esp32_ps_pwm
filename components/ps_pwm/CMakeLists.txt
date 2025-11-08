# --- 멀티-타겟(Multi-Target) HAL 선택 ---
set(HAL_SRC "")

# 1. ESP32 또는 ESP32-S3 타겟인 경우
if(IDF_TARGET STREQUAL "esp32" OR IDF_TARGET STREQUAL "esp32s3")
    set(HAL_SRC "ps_pwm_hal_legacy.c")
    message(STATUS "[ps_pwm] Compiling for ESP32/S3 (Legacy HAL)")

# 2. ESP32-P4 타겟인 경우
elseif(IDF_TARGET STREQUAL "esp32p4")
    set(HAL_SRC "ps_pwm_hal_p4.c")
    message(STATUS "[ps_pwm] Compiling for ESP32-P4 (P4 HAL)")

# 3. 지원하지 않는 타겟인 경우
else()
    message(FATAL_ERROR "[ps_pwm] Unsupported IDF_TARGET: ${IDF_TARGET}")
endif()


# 컴포넌트 등록
idf_component_register(
    SRCS "ps_pwm.c" ${HAL_SRC}  # <-- API 레이어 + 선택된 HAL 소스
    INCLUDE_DIRS "include"
    # v5.x의 모듈화된 드라이버 컴포넌트를 명시적으로 요구합니다.
    REQUIRES "esp_driver_mcpwm" "esp_driver_gpio" "freertos" "hal")