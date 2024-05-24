| Supported Targets | ESP32 | ESP32-S3 |
| ----------------- | ----- | -------- |

This depends on the ESP-IDF SDK source files.

 * Currently will be working with IDF v4.4 master branch. You can change the tags with git checkout.
 * Tested up to ESP-IDF version 4.3-beta3 on v1.0.0 tag
 * Tested up to ESP-IDF version 4.4.7 on v1.1.x tag

# MCPWM Phase-Shift PWM example

This example will show you how to use MCPWM module to generate a Phase-Shift PWM signal between
two pairs of hardware pins, e.g. for controlling a phase-shifted full-bridge power converter.
 
This example sets frequency, duty cycle and dead-time values for leading and
lagging half-bridge leg to fixed values.

MCPWM unit can be [0,1]
 

## Preparation
* Copy this examples folder to an empty working directory
* Copy (or clone) esp32_ps_pwm into the components/ subfolder

* Checkout your esp-idf to v4.4.7 branch and submodule update

  $ ```git checkout v4.4.7 && git submodule update --init --recursive```
* Set target using for ESP32-S3

  $ ```idf.py set-target esp32s3```

## Pin assignment for ESP32 
* PWM outputs for ESP32  
  GPIO_NUM_27 // PWM0A output for LEAD leg, Low Side  
  GPIO_NUM_26 // PWM0B output for LEAD leg, High Side  
  GPIO_NUM_25 // PWM1A output for LAG leg, Low Side  
  GPIO_NUM_33 // PWM1B output for LAG leg, High Side  
* Optional, shutdown/fault input for PWM outputs: disables output when pulled low  
  GPIO_NUM_4 // hardware shutdown input signal for PWM output  
  GPIO_NUM_2 // blink onBoard LED

## Pin assignment for ESP32-S3
* PWM outputs for ESP32-S3   
  GPIO_NUM_5 // PWM0A output for LEAD leg, Low Side  
  GPIO_NUM_4 // PWM0B output for LEAD leg, High Side  
  GPIO_NUM_7 // PWM1A output for LAG leg, Low Side  
  GPIO_NUM_6 // PWM1B output for LAG leg, High Side  
* Optional, shutdown/fault input for PWM outputs: disables output when pulled low  
  GPIO_NUM_8  // hardware shutdown input signal for PWM output  
  GPIO_NUM_48 // blink onBoard LED (led_strip-RGB)

## Operation
* On the four output pins, you will see two complementary, phase-shifted PWM waveforms
* When PWM outputs are connected to a full-bridge circuit using an appropriate driver,  
  using a differential voltage probe, you will see a phase-shifted PWM waveform.
* GPIO numbers in () correspond to ESP32-S3

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
