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

## User Manual
User manual can be found in [MANUAL.md](MANUAL.md).
