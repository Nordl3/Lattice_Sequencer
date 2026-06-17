# Pin Map

## Teensy 4.1 Main Controller

| Function | Teensy pin |
|---|---:|
| Gate Out | 2 |
| Trigger Out | 3 |
| LANE button | 4 |
| RUN button | 5 |
| SPI MOSI | 11 |
| SPI SCK | 13 |
| OLED SDA | 18 |
| OLED SCL | 19 |
| Serial5 TX, reserved | 20 |
| Pico top panel UART RX5 | 21 |
| Wire2 SCL | 24 |
| Wire2 SDA | 25 |
| DAC1 /SYNC | 30 |
| DAC2 /SYNC | 31 |
| DAC8562 /LDAC | 32 |
| Global encoder pushbutton | 33 |
| Pico bottom panel UART RX8 | 34 |
| Serial8 TX, reserved | 35 |
| Global encoder A | 36 |
| Global encoder B | 37 |
| Reset In | 38 |
| External Clock In | 39 |
| Sequence Length pot | A0 |
| Clock Division pot | A1 |

The Pico panels are TX-only in this build.

```text
Top 4x2 Pico TX    -> Teensy RX5 pin 21
Bottom 4x2 Pico TX -> Teensy RX8 pin 34
```

Pico RX and Pico RUN are not connected in this build.

The global encoder pushbutton, LANE button, and RUN button connect between their Teensy pin and ground. The Teensy firmware enables internal pull-ups and handles debounce in software.

External Clock In and Reset In are logic-level firmware inputs. Eurorack signals must be conditioned and protected before connection to the Teensy.

## Raspberry Pi Pico Encoder Panel

Both Pico boards use the same encoder-panel firmware.

| Encoder | A | B | Pushbutton |
|---:|---:|---:|---:|
| ENC0 | GP27 | GP26 | GP28 |
| ENC1 | GP21 | GP20 | GP22 |
| ENC2 | GP18 | GP17 | GP19 |
| ENC3 | GP15 | GP14 | GP16 |
| ENC4 | GP2 | GP3 | GP4 |
| ENC5 | GP5 | GP6 | GP7 |
| ENC6 | GP8 | GP9 | GP10 |
| ENC7 | GP11 | GP12 | GP13 |

All eight Pico encoder directions are currently swapped in the Pico firmware.

The Teensy firmware also has a top-level grid encoder direction setting:

```cpp
const int GRID_ENCODER_DIRECTION = -1;
```

## Pico-to-Teensy Mapping

| Pico board | Teensy UART | Physical encoder range |
|---|---|---:|
| Top 4x2 grid | Serial5 RX, pin 21 | 1-8 |
| Bottom 4x2 grid | Serial8 RX, pin 34 | 9-16 |

The current Teensy encoder-to-step map corrects the physical row order of the current 4x4 board.

## DAC8562 Wiring

| Signal | Teensy pin | Destination |
|---|---:|---|
| MOSI | 11 | level shifter -> both DAC DIN |
| SCK | 13 | level shifter -> both DAC SCLK |
| DAC1 /SYNC | 30 | level shifter -> DAC1 /SYNC |
| DAC2 /SYNC | 31 | level shifter -> DAC2 /SYNC |
| DAC /LDAC | 32 | level shifter -> both DAC /LDAC |

DAC8562 firmware configuration:

```text
SPI_MODE1
30 MHz SPI
internal 2.5 V reference enabled
x2 output gain
external shared LDAC pulse
```

DAC output map:

| Output | DAC8562 channel |
|---|---|
| CV1 / Pitch | DAC1 VOUTA |
| CV2 / Velocity | DAC1 VOUTB |
| CV3 | DAC2 VOUTA |
| CV4 | DAC2 VOUTB |

## MCP23017 LED Outputs

The two MCP23017 devices are on `Wire2`.

| Device | Address | Steps |
|---|---|---:|
| Left MCP23017 | `0x20` | 1-8 |
| Right MCP23017 | `0x21` | 9-16 |

### Left MCP23017, address `0x20`

| Function | MCP23017 pins |
|---|---|
| Playhead LEDs, steps 1-8 | GPA0-GPA7 |
| Step enabled LEDs, steps 1-8 | GPB7-GPB0 |

### Right MCP23017, address `0x21`

| Function | MCP23017 pins |
|---|---|
| Playhead LEDs, steps 9-16 | GPA0-GPA7 |
| Step enabled LEDs, steps 9-16 | GPB7-GPB0 |

The enabled-state LED order is reversed so step 1 maps to GPB7 and step 8 maps to GPB0 on the left chip; step 9 maps to GPB7 and step 16 maps to GPB0 on the right chip.

## I2C Devices

| Bus | Pins | Device | Address |
|---|---|---|---|
| Wire | SDA 18, SCL 19 | SSD1309 OLED | `0x3C` |
| Wire2 | SDA 25, SCL 24 | MCP23017 left | `0x20` |
| Wire2 | SDA 25, SCL 24 | MCP23017 right | `0x21` |
