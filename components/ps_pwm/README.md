# Phase-Shift PWM Waveform Generator using the ESP32 or ESP32-S3
Driver for the MCPWM hardware modules on the Espressif ESP32
or ESP32-S3 SoC for generating a Phase-Shift-PWM waveform between
two pairs of hardware pins.

Application in power electronics, e.g.
* Zero-Voltage-Switching (ZVS) Phase-Shift-PWM converters
* Full-Bridge or Dual-Active-Bridge
* Frequency-modulated LLC converters.

This depends on the ESP-IDF SDK source files.

 * Currently will be working with IDF v4.4 master branch. You can change the tags with git checkout.
 * Tested up to ESP-IDF version 4.3-beta3 on v1.0.0 tag
 * Tested up to ESP-IDF version 4.4.7 on v1.1.0 tag
 * Not compatible with ESP32-S2 as that chip has no MCPWM hardware module

 ## See example in examples subfolder

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