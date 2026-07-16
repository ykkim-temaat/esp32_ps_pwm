| Supported Targets | ESP32 | ESP32-S3 |
| ----------------- | ----- | -------- |

# Phase-Shift PWM (PS-PWM) & PLL ZVS Tracking Component

This repository contains a Phase-Shift PWM (PS-PWM) driver component for the Espressif ESP32 and ESP32-S3 SoCs, migrated to support **ESP-IDF v5.3.4**. It is structured as an ESP-IDF Standalone Component.

It uses the modern object-oriented MCPWM driver APIs (`driver/mcpwm_prelude.h`) to generate complementary phase-shifted PWM signals across two half-bridge legs (LEAD leg and LAG leg). This is highly useful in power electronics applications such as:
* Zero-Voltage-Switching (ZVS) Full-Bridge / Dual-Active-Bridge (DAB) converters
* LLC resonant converters

## Development Status & History
* **Version**: **v1.2.0**
* **ESP-IDF Compatibility**: Developed and verified on **ESP-IDF v5.3.4**.

* 2024-05-24 Yoonki Kim (Migration esp-idf v4.4.7 from v4.3-beta3)
* 2026-07-02 Yoonki Kim (v1.0.0 - esp-idf v5.3.4 Migration & Safety features)
* 2026-07-08 Yoonki Kim (v1.1.0 - Auto-Tracking PI Control Loop with PLL Lock)
* 2026-07-14 Yoonki Kim (v1.2.0 - Hardware prescaler integration and CLI UX updates)

## Repository Structure

This repository acts as a standard ESP-IDF Component. The component source and headers are located at the root.
Test and application codes have been separated into the `examples/` directory.

### Examples

이 리포지토리의 `examples/` 디렉토리에는 라이브러리 활용법을 보여주는 두 가지 실전 예제가 포함되어 있습니다. 각 예제의 폴더 내 `README.md` 문서에 상세한 원리와 API 사용법이 설명되어 있습니다.

1. **`ps_pwm_basic`**: PS-PWM 신호 출력의 기초를 다루는 예제입니다. `pspwm_init_symmetrical()`을 이용한 초기화, 하드웨어 Fault 감지 및 차단 로직, 사용자 버튼을 통한 Soft-Start On/Off 제어, 그리고 런타임 중 주파수 및 듀티 변경 방법을 직관적으로 보여줍니다.
2. **`zvs_resonant_tracking`**: LLC 공진형 컨버터 등의 영전압 스위칭(ZVS) 유지를 위해 공진 주파수를 자동으로 추종하는 심화 예제입니다. 캡처(Capture) 모듈로 지연 시간을 측정하고 속도형 PI 제어 및 PLL Lock을 통해 주파수를 실시간으로 조절하는 멀티 태스크 아키텍처를 구현하고 있습니다.

## How to use as a Component

Add this repository as a git submodule to your ESP-IDF project's `components/` directory:
```bash
cd your_project_root
mkdir -p components
git submodule add https://github.com/ykkim-temaat/esp32_ps_pwm.git components/ps_pwm
```

## Building the Examples

1. Set up your ESP-IDF v5.3.4 environment.
2. Navigate to one of the example directories:
   ```bash
   cd examples/ps_pwm_basic
   # or
   cd examples/zvs_resonant_tracking
   ```
3. Set the target chip (ESP32-S3 or ESP32):
   ```bash
   idf.py set-target esp32s3
   ```
4. Build and flash to your board:
   ```bash
   idf.py flash monitor
   ```

## Schematic Representation

> **Note**: 다이어그램 괄호 `()` 안의 핀 번호와 아래 핀 설정 코드는 **ESP32-S3** 기준 맵핑의 예시입니다. 괄호 밖의 핀 번호는 ESP32 기준입니다.

```text
                            VDD
                      .---------------.
                      |               |
                   ||-+               +-||
   To  GPIO 26 (4) ||<-               ->|| To  GPIO 33 (6)
       ------------||-+               +-||-----------
                      |               |
                      |     LOAD      |
                      |      ___      |
   LEAD half-bridge   o-----|___|-----o   LAG half-bridge
                      |               |
                      |               |
                      |               |
                   ||-+               +-||
   To  GPIO 27 (5) ||<-               ->|| To  GPIO 25 (7)
       ------------||-+               +-||-----------
                      |               |
                      '---------------'
                             GND
```

### GPIO 핀 정의 코드

위 다이어그램에 대응하는 GPIO 핀 설정 코드의 예시는 다음과 같습니다:

```c
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
```
