# Pin Map

## Teensy 4.1 Main Controller

| Function | Teensy 4.1 pin |
|---|---:|
| Gate Out | 2 |
| Trigger Out | 3 |
| EDIT button | 4 |
| RUN / STOP button | 5 |
| SPI MOSI | 11 |
| SPI SCK | 13 |
| Clock-division pot / A0 | 14 |
| Number-of-steps pot / A1 | 15 |
| Pico UART TX5 | 20 |
| Pico UART RX5 | 21 |
| Pico RUN | 22 |
| Wire2 SCL | 24 |
| Wire2 SDA | 25 |
| DAC shared `/LDAC` | 29 |
| DAC Left `/SYNC` | 30 |
| DAC Right `/SYNC` | 31 |
| External Clock In | 32 |
| Global encoder pushbutton | 33 |
| Reserved Serial8 RX | 34 |
| Reserved Serial8 TX | 35 |
| Global encoder A | 36 |
| Global encoder B | 37 |
| Reset In | 38 |

Pins 34 and 35 are deliberately left unused for a possible Serial8 UART connection.

The global encoder pushbutton connects between pin 33 and ground. The firmware enables the Teensy internal pull-up and handles debounce in software.

External Clock In and Reset In are logic-level test inputs. Eurorack signals must be conditioned and protected before connection to the Teensy.

## 74AHCT125N Level Shifters

The Teensy control lines are 3.3 V logic. The DAC8562 devices and 74AHCT125N buffers run from the 5 V rail.

### IC1 — left 74AHCT125N

| Teensy signal | IC1 input | IC1 output | DAC destination |
|---|---:|---:|---|
| pin 13 SPI SCK | pin 12 `4A` | pin 11 `4Y` | both DACs pin 7 `SCLK` |
| pin 30 DAC Left `/SYNC` | pin 2 `1A` | pin 3 `1Y` | DAC1 pin 6 `/SYNC` |
| pin 31 DAC Right `/SYNC` | pin 5 `2A` | pin 6 `2Y` | DAC2 pin 6 `/SYNC` |

### IC2 — right 74AHCT125N

| Teensy signal | IC2 input | IC2 output | DAC destination |
|---|---:|---:|---|
| pin 11 SPI MOSI | pin 2 `1A` | pin 3 `1Y` | both DACs pin 8 `DIN` |
| pin 29 shared `/LDAC` | pin 5 `2A` | pin 6 `2Y` | both DACs pin 4 `/LDAC` |

Power each 74AHCT125N from 5 V and add a local 100 nF bypass capacitor. Hold enabled `/OE` inputs low. Hold unused logic inputs at defined levels rather than leaving them floating.

## DAC8562 Outputs

| Output | DAC channel |
|---|---|
| CV1 / Pitch | DAC1 `VOUTA`, pin 1 |
| CV2 / Velocity | DAC1 `VOUTB`, pin 2 |
| CV3 | DAC2 `VOUTA`, pin 1 |
| CV4 | DAC2 `VOUTB`, pin 2 |

## DAC8562 Local Wiring

For each DAC8562 adapter board:

| DAC8562 pin | Signal | Connection |
|---:|---|---|
| 1 | `VOUTA` | analogue CV output |
| 2 | `VOUTB` | analogue CV output |
| 3 | `GND` | ground |
| 4 | `/LDAC` | shared buffered `/LDAC` |
| 5 | `/CLR` | 10 kΩ pull-up to 5 V |
| 6 | `/SYNC` | dedicated buffered chip-select line |
| 7 | `SCLK` | shared buffered SPI clock |
| 8 | `DIN` | shared buffered SPI data |
| 9 | `AVDD` | 5 V rail |
| 10 | `VREFIN/VREFOUT` | 470 nF capacitor to ground |

Add a local 100 nF bypass capacitor between `AVDD` and ground at each DAC adapter board.

## Pico Encoder Panel

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

## Pico to Teensy UART

| Pico | Teensy 4.1 |
|---|---|
| GP0 / UART TX | pin 21 / RX5 |
| GP1 / UART RX | pin 20 / TX5 |
| RUN | pin 22 |
| GND | GND |
| VSYS / VBUS | Teensy USB 5 V / VIN |

## MCP23017 LED Outputs

| Function | MCP23017 pins |
|---|---|
| Active playhead LEDs, steps 0–7 | GPA0–GPA7 |
| Step-enabled LEDs, steps 0–7 | GPB7–GPB0 |

The enabled-state LED order is reversed so step 0 maps to GPB7 and step 7 maps to GPB0.

## I2C Devices

| Device | Address |
|---|---|
| MCP23017 | `0x20` |
| SSD1306 OLED | `0x3C` |
