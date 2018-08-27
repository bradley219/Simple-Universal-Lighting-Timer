# Simple Universal Lighting Timer

## Features
- Smooth on/off transitions via PWM dimming.
- Simple interface: only one button and one switch.
- Customizable daily recurring on and off timers.
- Backup power continues to keep track of time during a loss of power.
- Minimal, low-cost components.

## Technical Specifications
- Control module is designed to run on a 5V power supply. Lighting should use a separate supply so as to not consume the backup power.
- Optional backup power provided by a super capacitor.
- 5V TTL signal controls the switching of the lighting, typically through an appropriate type of transistor.
- Real time clock driven by a 32.768kHz quartz crystal.
- ATTiny85 MCU running very lightweight firmware that is able to execute with a 32.768kHz system clock.

## Firmware
- Make edits to configuration in [led\_timer.c](led_timer.c) if desired.
- Update avrdude config values in the Makefile if necessary (i.e. `AVRDUDE\_PROGRAMMER`).
- Run `make` to compile the firmware. Build products will be found in the `obj` directory.
- Connect the programmer to the ATtiny85 with 32.768kHz crystal installed.
- Run `make burn-fuses` to burn the proper fuses for the project. This only needs to be done the first time.
- Run `make program` to upload the firmware to the device. Note that the upload speed will be quite slow 
  compared to what you may be used to, due to the very slow system clock speed of 32.768kHz.

## User Manual
User manual can be found in [MANUAL.md](MANUAL.md).
