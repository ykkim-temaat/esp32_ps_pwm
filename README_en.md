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

The `examples/` directory of this repository contains two practical examples demonstrating how to use the library. Detailed principles and API usage are explained in the `README_en.md` (or `README.md`) files within each example's folder.

1. **`ps_pwm_basic`**: A foundational example demonstrating how to configure and output PS-PWM signals. It intuitively shows initialization using `pspwm_init_symmetrical()`, hardware fault detection and blocking logic, soft-start On/Off control via a user button, and dynamic runtime changes to frequency and duty cycle.
2. **`zvs_resonant_tracking`**: An advanced example for automatically tracking the resonant frequency to maintain Zero-Voltage-Switching (ZVS) in converters like LLC resonant converters. It implements a multi-task architecture that measures delay time using the Capture module and adjusts frequency in real-time through an incremental PI control loop and PLL Lock.

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

> **Note**: The pin numbers in parentheses `()` in the diagram and the pin configuration code below are examples mapped for the **ESP32-S3**. The pin numbers outside the parentheses are for the ESP32.

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

### GPIO Pin Definition Code

An example of the GPIO pin configuration code corresponding to the diagram above is as follows:

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
