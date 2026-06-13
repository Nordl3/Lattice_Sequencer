# Changelog

## v0.3.7

Main-brain breadboard update.

### Hardware

- replaced the two MCP4822 dual 12-bit DACs with two DAC8562 dual 16-bit DACs
- added two 74AHCT125N logic buffers for Teensy 3.3 V to DAC 5 V translation
- moved the shared DAC8562 `/LDAC` signal to Teensy pin 29
- retained dedicated DAC `/SYNC` lines on Teensy pins 30 and 31
- retained shared SPI MOSI and SCK on Teensy pins 11 and 13
- powered the DAC8562 devices and level shifters from the 5 V rail
- added clock-division pot on Teensy pin 14 / A0
- added active-step-count pot on Teensy pin 15 / A1

### Firmware

- added DAC8562 24-bit SPI writes using `SPI_MODE1`
- enabled each DAC8562 internal 2.5 V reference at startup
- buffered four DAC channel writes and committed them simultaneously with shared `/LDAC`
- changed the physical pin-5 button to RUN / STOP
- moved playback-direction selection into the global encoder menu
- removed clock division and sequence length from the global encoder menu because dedicated pots now control them
- changed the global-menu button behaviour so the first press after returning home reveals the current item without advancing
- increased OLED emphasis on large, readable status text
- retained MIDI, internal-clock, external-clock, Pico UART, OLED, LED, gate, trigger, and reset handling

## Earlier prototype state

The previous working breadboard firmware used two MCP4822 dual 12-bit DACs and global-menu selection for clock division and sequence length.
