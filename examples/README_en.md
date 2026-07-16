# ESP32 Phase-Shift PWM Examples

This directory contains two primary example projects that can be developed utilizing the `ps_pwm` library. You can find detailed API guides and operating principles in the `README_en.md` (or `README.md`) file within each example's folder.

---

## 1. ps_pwm_basic
**Basic Phase-Shift PWM Output and State Control Example**

An example covering the most fundamental methods for outputting PS-PWM signals.

* **Key Features**:
  * Initialization and dead-time configuration using `pspwm_init_symmetrical()`
  * Safe shutdown and recovery through a hardware fault detection (GPIO) and latching system
  * Output On/Off toggle functionality using soft faults
  * Smooth Phase-Shift Duty transitions via Soft-Start (`pspwm_set_duty_soft()`)
  * Dynamic frequency changes during runtime (`pspwm_set_frequency()`)

👉 **Detailed Guide**: [ps_pwm_basic/README_en.md](ps_pwm_basic/README_en.md)

---

## 2. zvs_resonant_tracking
**ZVS Resonant Frequency Auto-Tracking (PI Control) Example**

An advanced example that automatically tracks the output frequency to match the resonant tank's characteristics, maintaining Zero Voltage Switching (ZVS) in LLC resonant converters and similar applications.

* **Key Features**:
  * Delay time measurement between the switching start point (Start) and the zero-crossing point (ZC) using `pspwm_enable_tracking_capture()`
  * **Incremental PI Control** loop based on the error between the measured delay time and the target delay time
  * **PLL Lock (Dead-band)** application to prevent system oscillation
  * Frequency Clamping (Min/Max limits) and anti-windup for safety
  * **Multi-task architecture** separating main control, tracking, capture monitoring, and terminal input

👉 **Detailed Guide**: [zvs_resonant_tracking/README_en.md](zvs_resonant_tracking/README_en.md)
