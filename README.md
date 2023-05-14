# RGB LED Controller

A simple RGB LED controller designed for any generic LED strips which allows for flexible, user-specified patterns.
The pattern is accepted via serial and can be saved to EEPROM for use on every board boot. Each pattern can contain
up to 50 different steps.

## Hardware
This project was designed for use with an Atmel ATMEGA-328P microcontroller (ie. what is used for an Arduino Uno)
running at 16MHz and driving the LED strip via MOSFETs on each channel. The software was tested on a board designed
specifically for this application instead of an Uno board, so while it should work correctly, your mileage may vary.

If you change the clock frequency of the ATMEGA, the timing math will be incorrect. At the moment, there is no
configuration option for the frequency to adjust the math, so if you use a different crystal you will need to update
the math constants yourself.

## Software Dependencies
This project depends on the "MD_PWM" library which is used to drive the LED strip channels using "timer 2" in the
ATMEGA instead of the default, as "timer 1" is being used for the pattern timings.

## Configuration
Some simple configuration options are provided at the top of the program:
- R_PIN, G_PIN, B_PIN: The pins being used for the LED strip. You can change the pins being used for to any pin as
  needed, as the PWM is being driven in software.
- CHAR_SEP: The pattern deliminator (comma by default).
- SERIAL_BAUD: The baud rate to be used for the serial interface.
- PWM_FREQ: The frequency (in Hz) of the PWM used for the LED pins. 250Hz is the default which provides a mostly
  flicker-free experience and a good balance of performance. You can adjust this to reduce flicker or improve
  responsiveness depending on your application.

## Usage Instructions
### Modes
The following modes are supported:
- Static (mode "S"): show a single colour which does not change
- Jump (mode "J"): show multiple colours, changing between each (no fading)
- Fade (mode "F"): show multiple colours, smoothly fading between each
- Demo (mode "D"): show a demo pattern, see list below

### Pattern Line Format
User-specified patterns are provided in the following format:
`<MODE>,<HEX COLOUR>,<DURATION IN MS>,<HEX COLOUR>,<DURATION IN MS>,...`

For example, a pattern that shows red, then green, then blue, could be specified as:
`J,FF0000,2000,00FF00,2000,0000FF,2000`

You can specify up to 50 "steps" in your pattern, where each step is a combination of a colour and a duration.
Standard RGB hex colour strings are used

### Demo Patterns
To use a demo pattern, input the command "D#", where the "#" symbol is replaced by the number of the demo pattern you
wish to display. The list of supported patterns is:

0. 2 seconds each of all red, all green, all blue in a jump mode. Good for testing an LED strip.

### Saving Patterns
Once you have a pattern that you like, you can save (or "commit") it for use every time your board powers-on. To do this,
simply execute the "C" command. If you wish to clear this saved pattern in the future, send the "R" command.