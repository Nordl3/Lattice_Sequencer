# Build Notes

## Teensy

Use Teensy 4.1 with Teensyduino.

Set USB Type to:

```text
Serial + MIDI
```

Required Arduino libraries:

- SPI
- Wire
- Adafruit MCP23X17

The OLED code in the main controller firmware is a small raw SSD1306 text driver and does not use Adafruit GFX or Adafruit SSD1306.

## Pico

Use Raspberry Pi Pico with the Arduino-Pico core.

The encoder panel firmware uses:

- USB Serial for debug output
- Serial1 for UART output to the Teensy
- GP0 as UART TX
- GP1 as UART RX

## Buttons and Pots

EDIT and PLAY buttons are wired from the Teensy input pin to ground and use `INPUT_PULLUP`.

The clock division and sequence length pots should be wired:

```text
3.3V -> outer lug
GND  -> outer lug
wiper -> Teensy ADC input
```

## DACs

The current firmware targets two MCP4822 dual DACs over SPI.

The MCP4822s share:

```text
MOSI = Teensy pin 11
SCK  = Teensy pin 13
```

Each DAC has its own chip select:

```text
DAC Left CS  = Teensy pin 30
DAC Right CS = Teensy pin 31
```

## Gate and Trigger Outputs

The prototype firmware drives gate and trigger directly from Teensy GPIO.

For Eurorack hardware, add proper output buffering, protection, and voltage scaling.
