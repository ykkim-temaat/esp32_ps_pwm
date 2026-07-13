| Supported Targets | ESP32 | ESP32-S3 |
| ----------------- | ----- | -------- |

# MCPWM Phase-Shift PWM Example

This repository contains a Phase-Shift PWM (PS-PWM) driver component and example application for the Espressif ESP32 and ESP32-S3 SoCs, migrated to support **ESP-IDF v5.3.4**.

It uses the modern object-oriented MCPWM driver APIs (`driver/mcpwm_prelude.h`) to generate complementary phase-shifted PWM signals across two half-bridge legs (LEAD leg and LAG leg). This is highly useful in power electronics applications such as:
* Zero-Voltage-Switching (ZVS) Full-Bridge / Dual-Active-Bridge (DAB) converters
* LLC resonant converters

## Development Status
* **ESP-IDF Compatibility**: Developed and verified on **ESP-IDF v5.3.4** (branch: `esp-idf-v5.3`).
* **Legacy Support**: Previous tags (`v1.1.1` and earlier) support ESP-IDF v4.4.7 and legacy direct-register (`mcpwm_dev_t`) APIs.

---

## Preparation & Build

1. Set up your ESP-IDF v5.3.4 environment (e.g., `source $HOME/esp/esp-idf/export.sh` or run your `get_idf` alias).
2. Set the target chip (ESP32-S3 or ESP32):
   ```bash
   idf.py set-target esp32s3
   ```
3. Build the application:
   ```bash
   idf.py build
   ```
4. Flash to your board:
   ```bash
   idf.py flash
   ```
5. View status logs:
   ```bash
   idf.py monitor
   ```

---

## Pin Assignment

### For ESP32-S3 (Tested on ESP32-S3-DevKitC-1)
* **PWM Outputs**:
  * `GPIO 5` ➡️ LEAD Leg Low-Side (PWM0A)
  * `GPIO 4` ➡️ LEAD Leg High-Side (PWM0B)
  * `GPIO 7` ➡️ LAG Leg Low-Side (PWM1A)
  * `GPIO 6` ➡️ LAG Leg High-Side (PWM1B)
* **Hardware Fault/Shutdown Input**:
  * `GPIO 8` ➡️ Disables all outputs immediately on low-level trigger (Hardware OST brake).
* **Manual Reset & Output Control**:
  * `GPIO 0` (BOOT button) ➡️ Manually clears Hardware Faults and toggles PWM Output ON/OFF safely via Software Faults.
* **Onboard LED**:
  * `GPIO 48` ➡️ WS2812 smart RGB LED for status monitoring.
* **ZC Capture & Auto-Tracking (Simulation)**:
  * `GPIO 4` ➡️ Captured internally using GPIO Matrix loopback (NO physical jumper required).
  * `GPIO 10` ➡️ ZC (Zero Crossing) Simulation Output.
  * `GPIO 9` ➡️ Capture Channel for ZC input (User must jumper `GPIO 10` ➡️ `GPIO 9` to run the tracking simulation).

### For ESP32
* **PWM Outputs**:
  * `GPIO 27` ➡️ LEAD Leg Low-Side (PWM0A)
  * `GPIO 26` ➡️ LEAD Leg High-Side (PWM0B)
  * `GPIO 25` ➡️ LAG Leg Low-Side (PWM1A)
  * `GPIO 33` ➡️ LAG Leg High-Side (PWM1B)
* **Hardware Fault/Shutdown Input**:
  * `GPIO 4` ➡️ Disables all outputs immediately on low-level trigger.
* **Onboard LED**:
  * `GPIO 2` ➡️ Standard monochrome status LED.

---

## Real-Time Monitoring & Status LED

Since outputting high frequency PWM signals (100 kHz ~ 200 kHz) is hard to observe without an oscilloscope, this project includes an active serial monitor and LED color-coding status scheme:

* 🟢 **Green LED (WS2812)** / **LED ON (ESP32)**:
  * Indicates that PS-PWM is active and running in **100 kHz Mode** (Duty: 45%).
* 🔵 **Blue LED (WS2812)** / **LED OFF (ESP32)**:
  * Indicates that PS-PWM is active and running in **200 kHz Mode** (Duty: 45%).
* 🔴 **Red LED (WS2812)** (Blinking):
  * Indicates that a **Hardware Fault/Shutdown** occurred (GPIO 8/4 triggered low). Outputs are safely latched Low (0V).
  * Requires **MANUAL INTERVENTION**: The user must remove the physical fault condition and press the **BOOT button (GPIO 0)** to clear the fault latch.
* ⚪ **LED OFF (WS2812)**:
  * Indicates that the PWM output is currently **Disabled** (System Idle). Press the BOOT button to enable.
---

## Schematic Representation

```
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
