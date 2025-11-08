# 컴포넌트 등록
idf_component_register(
    SRCS "ps_pwm.c"  # 공통 API 레이어
    INCLUDE_DIRS "include"
    PRIV_REQUIRES "driver" "freertos" "hal")

# --- 멀티-타겟(Multi-Target) HAL 선택 ---

# 1. ESP32 또는 ESP32-S3 타겟인 경우
if(IDF_TARGET STREQUAL "esp32" OR IDF_TARGET STREQUAL "esp32s3")
    idf_component_get_property(srcs ${COMPONENT_NAME} SRCS)
    list(APPEND srcs "ps_pwm_hal_legacy.c") # 레거시(S3/ESP32) HAL 구현 추가
    idf_component_set_property(${COMPONENT_NAME} SRCS "${srcs}")
    
    message(STATUS "[ps_pwm] Compiling for ESP32/S3 (Legacy HAL)")

# 2. ESP32-P4 타겟인 경우
elseif(IDF_TARGET STREQUAL "esp32p4")
    idf_component_get_property(srcs ${COMPONENT_NAME} SRCS)
    list(APPEND srcs "ps_pwm_hal_p4.c") # P4 HAL 구현 추가
    idf_component_set_property(${COMPONENT_NAME} SRCS "${srcs}")

    message(STATUS "[ps_pwm] Compiling for ESP32-P4 (P4 HAL)")

# 3. 지원하지 않는 타겟인 경우
else()
    message(FATAL_ERROR "[ps_pwm] Unsupported IDF_TARGET: ${IDF_TARGET}")
endif()