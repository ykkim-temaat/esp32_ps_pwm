# Phase-Shift PWM Waveform Generator Component (ps_pwm)

This is the `ps_pwm` component driver for generating a Phase-Shift-PWM (PS-PWM) waveform using the MCPWM hardware module on Espressif ESP32 and ESP32-S3 SoCs.

## Key Features & Compatibility
* **ESP-IDF Compatibility**: Rewritten to support **ESP-IDF v5.3.4** and newer.
* **Driver Architecture**: Uses the object-oriented, handle-based MCPWM APIs (`driver/mcpwm_prelude.h`) instead of legacy raw register manipulation (`mcpwm_dev_t`).
* **Synchronization**: Synchronizes Timer 0 (LEAD leg) and Timer 1 (LAG leg) at hardware level using a sync event source to achieve precise phase-shifted timing.
* **Complementary Output & Dead-Time**: Generates complementary high/low-side drive signals for each leg, utilizing the generator-level dead-time configuration (RED/FED) to prevent shoot-through.
* **Hardware Protection (Trip-Zone)**: Connects a GPIO fault detector to trigger a One-Shot (OST) brake on the operators, latching all PWM outputs Low within nanoseconds of a fault event. Contains auto-recovery routines.

---

## Supported Targets
* **ESP32** (e.g. ESP32-WROOM)
* **ESP32-S3** (e.g. ESP32-S3-WROOM)
* *Not compatible with ESP32-S2 or other chips lacking MCPWM hardware peripherals.*

---

## Schematic Representation

```
                            VDD
                      .---------------.
                      |               |
                   ||-+               +-||
       To  GPIO 26 ||<-               ->||To  GPIO 33
       ------------||-+               +-||-----------
                      |               |
                      |     LOAD      |
                      |      ___      |
   LEAD half-bridge   o-----|___|-----o   LAG half-bridge
                      |               |
                      |               |
                      |               |
                   ||-+               +-||
       To  GPIO 27 ||<-               ->||To  GPIO 25
       ------------||-+               +-||-----------
                      |               |
                      '---------------'
                             GND
```