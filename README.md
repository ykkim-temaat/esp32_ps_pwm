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

1. **`ps_pwm_basic`**: A foundational example demonstrating how to configure and output PS-PWM signals. It cycles between 100 kHz and 200 kHz frequencies, while sweeping the duty cycle from 25% to 75% using soft-start. Status is indicated via the onboard WS2812 LED.
2. **`zvs_resonant_tracking`**: An advanced example utilizing Hardware Capture Timers and a PI Control loop to automatically track the resonant frequency (PLL Lock) for Zero-Voltage-Switching.

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
