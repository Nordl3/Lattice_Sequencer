# Pin Map

## Teensy 4.1

| Function | Pin |
|---|---:|
| Gate Out | 2 |
| Trigger Out | 3 |
| EDIT button | 4 |
| PLAY button | 5 |
| Clock Division pot | A0 |
| Sequence Length pot | A1 |
| Pico RX5 | 21 |
| Pico TX5 | 20 |
| Pico RUN | 22 |
| DAC MOSI | 11 |
| DAC SCK | 13 |
| DAC Left CS | 30 |
| DAC Right CS | 31 |
| I2C SCL2 | 24 |
| I2C SDA2 | 25 |

## Pico Encoder Panel

| Encoder | A | B | Button |
|---:|---:|---:|---:|
| ENC0 | GP27 | GP26 | GP28 |
| ENC1 | GP21 | GP20 | GP22 |
| ENC2 | GP18 | GP17 | GP19 |
| ENC3 | GP15 | GP14 | GP16 |
| ENC4 | GP2 | GP3 | GP4 |
| ENC5 | GP5 | GP6 | GP7 |
| ENC6 | GP8 | GP9 | GP10 |
| ENC7 | GP11 | GP12 | GP13 |

## Pico to Teensy

| Pico | Teensy 4.1 |
|---|---|
| GP0 / UART TX | pin 21 / RX5 |
| GP1 / UART RX | pin 20 / TX5 |
| RUN | pin 22 |
| GND | GND |
| VSYS / VBUS | Teensy USB 5V / VIN |

## I2C Devices

| Device | Address |
|---|---|
| OLED | `0x3C` |
| MCP23017 | `0x20` |
