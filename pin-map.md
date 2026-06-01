# Pin Map

## Teensy 4.1 Main Controller

| Function | Teensy 4.1 pin |
|---|---:|
| Gate Out | 2 |
| Trigger Out | 3 |
| EDIT button | 4 |
| PLAY button | 5 |
| SPI MOSI | 11 |
| SPI SCK | 13 |
| Pico UART RX5 | 21 |
| Pico UART TX5 | 20 |
| Pico RUN | 22 |
| Wire2 SCL | 24 |
| Wire2 SDA | 25 |
| DAC Left CS | 30 |
| DAC Right CS | 31 |
| External Clock In | 32 |
| Global encoder pushbutton | 33 |
| Reserved Serial8 RX | 34 |
| Reserved Serial8 TX | 35 |
| Global encoder A | 36 |
| Global encoder B | 37 |
| Reset In | 38 |

Pins 34 and 35 are deliberately left unused for a possible Serial8 UART connection.

The global encoder pushbutton is connected between pin 33 and ground. The Teensy firmware enables the internal pull-up resistor and handles debounce in software.

External Clock In and Reset In are logic-level test inputs in the firmware. Eurorack signals must be conditioned and protected before connection to the Teensy.

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
| VSYS / VBUS | Teensy USB 5V / VIN |

## MCP4822 DAC Outputs

| Output | DAC channel |
|---|---|
| CV1 / Pitch | DAC Left A |
| CV2 / Velocity | DAC Left B |
| CV3 | DAC Right A |
| CV4 | DAC Right B |

## MCP23017 LED Outputs

| Function | MCP23017 pins |
|---|---|
| Active playhead LEDs, steps 0-7 | GPA0-GPA7 |
| Step enabled LEDs, steps 0-7 | GPB7-GPB0 |

The enabled-state LED order is reversed so step 0 maps to GPB7 and step 7 maps to GPB0.

## I2C Devices

| Device | Address |
|---|---|
| MCP23017 | `0x20` |
| SSD1306 OLED | `0x3C` |
