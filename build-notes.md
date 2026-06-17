# Build Notes

## Firmware Targets

| File | Target |
|---|---|
| `teensy-main-controller.ino` | Teensy 4.1 main-controller firmware |
| `pico-encoder-panel.ino` | Raspberry Pi Pico encoder-panel firmware |

## Teensy Arduino IDE Settings

Use Teensyduino and select:

```text
Board: Teensy 4.1
USB Type: Serial + MIDI
```

Required library:

```text
U8g2
```

The firmware checks that the selected Teensy USB type includes MIDI.

## Pico Arduino IDE Settings

Upload `pico-encoder-panel.ino` to both Raspberry Pi Pico encoder panels using the installed Pico Arduino core.

Pico UART settings:

```text
TX: GP0
RX: GP1, unused in the current Teensy build
Baud: 115200
```

Both Pico boards use the same firmware. The Teensy identifies the top/bottom panel by the UART port receiving the event:

```text
Serial5 RX pin 21 = top 4x2 grid, physical encoders 1-8
Serial8 RX pin 34 = bottom 4x2 grid, physical encoders 9-16
```

## Current Main Firmware Version

```text
v0.3.14
```

Major current build points:

- 16-step two-Pico input
- DAC8562 CV output backend
- SSD1309 OLED on Wire pins 18/19
- MCP23017 LED expanders on Wire2 pins 24/25
- internal clock default
- length and clock-division pots on A0 and A1
- no Serial debug output in the main sequencer firmware
- OLED redraws kept low priority
- no OLED playhead animation during playback

## Timing Structure

Transport and musical output are the highest priority.

High-priority work:

```text
USB MIDI clock and transport
internal clock
external clock
DAC8562 CV writes
gate timing
trigger timing
```

Medium-priority work:

```text
Pico UART input
global encoder
local buttons
front-panel pots
```

Low-priority work:

```text
MCP23017 LED writes
OLED redraws
```

The OLED does not redraw on every sequencer step. The MCP23017 LEDs provide live playhead indication.

## DAC8562 Notes

The firmware uses the DAC8562 command format proven by the sine bring-up test:

```text
(command & 0x07) << 19
(address & 0x07) << 16
data
```

The DACs are configured in setup:

```text
enable internal 2.5 V reference
configure both channels to respond to external shared LDAC
write all four CV inputs
pulse LDAC once
```

`DAC_OUTPUT_MAX_CODE` is currently:

```cpp
const uint16_t DAC_OUTPUT_MAX_CODE = 0xE000U;
```

This intentionally leaves some output headroom for the current prototype supply.

## Analogue Pots

A0 and A1 are sampled at a limited rate with hysteresis.

Top-level controls in `teensy-main-controller.ino`:

```cpp
const unsigned long POT_READ_INTERVAL_MS = 50;
const int POT_HYSTERESIS_ADC_COUNTS = 8;
```

Current assignments:

```text
A0 = sequence length
A1 = clock division
```

## Encoder Mapping

The main firmware has the physical encoder-to-step map near the top of the file.

Current mapping:

```text
physical 5   6   7   8    -> steps 1   2   3   4
physical 1   2   3   4    -> steps 5   6   7   8
physical 13  14  15  16   -> steps 9   10  11  12
physical 9   10  11  12   -> steps 13  14  15  16
```

Grid encoder direction can be flipped with:

```cpp
const int GRID_ENCODER_DIRECTION = -1;
```

The global encoder direction can be flipped separately with:

```cpp
const int GLOBAL_ENCODER_DIRECTION = 1;
```

## Clock Inputs

The firmware supports:

```text
MIDI clock
internal clock
external pulse clock
```

External Clock In and Reset In are logic-level test inputs. Do not patch Eurorack signals directly into the Teensy. Add appropriate input conditioning and protection first.

External clock is currently assigned to pin 39 because pin 32 is used for DAC8562 /LDAC.

## Output Conditioning

Current firmware outputs are microcontroller/DAC-side signals. Final Eurorack hardware should add the appropriate analogue and digital conditioning:

```text
CV output scaling/buffering
gate/trigger output buffering
clock/reset input conditioning
input protection
```
