# Build Notes

## Firmware

This repository contains two firmware targets:

| File | Target |
|---|---|
| `pico-encoder-panel.ino` | Raspberry Pi Pico encoder-panel firmware |
| `teensy-main-controller.ino` | Teensy 4.1 main-controller firmware |

The Pico firmware scans the encoder panel and sends UART event packets.

The Teensy firmware runs the sequencer engine, receives Pico events, handles USB MIDI, writes DAC CV outputs, controls gate and trigger timing, updates the OLED, and updates MCP23017 LED outputs.

## Teensy 4.1 Arduino IDE Settings

Use Teensyduino and select:

```text
Board: Teensy 4.1
USB Type: Serial + MIDI
```

The Teensy firmware checks that the selected USB type includes MIDI.

## Pico Arduino IDE Settings

Upload `pico-encoder-panel.ino` to the Raspberry Pi Pico using the installed Pico Arduino core.

The encoder-panel UART settings are:

```text
TX: GP0
RX: GP1
Baud: 115200
```

## Timing Structure

The Teensy firmware separates timing-critical work from low-priority UI work.

Timing-critical work:

```text
USB MIDI clock and transport
internal clock
external clock
DAC CV writes
gate timing
trigger timing
```

Lower-priority work:

```text
Pico UART parsing
local controls
MCP23017 LED writes
OLED redraws
```

OLED updates are deferred and transferred in small I2C chunks.

Serial debug output is disabled by default.

## Clock Inputs

The firmware supports:

```text
MIDI clock
internal clock
external pulse clock
```

External Clock In and Reset In are logic-level test inputs. Do not patch Eurorack signals directly into the Teensy. Add appropriate input conditioning and protection first.

## Global Encoder

The global encoder uses:

```text
A: pin 36
B: pin 37
pushbutton: pin 33
```

The encoder A/B lines use table-based quadrature decoding with short interrupt handlers.

The pushbutton connects between pin 33 and ground. It uses the Teensy internal pull-up and software debounce.

## MCP23017 LEDs

```text
GPA0-GPA7: active playhead LEDs
GPB7-GPB0: step enabled LEDs
```

The enabled-state LED order is intentionally reversed to match the physical LED wiring.

## DACs

The firmware currently targets two MCP4822 dual DACs:

```text
DAC Left A: CV1 / Pitch
DAC Left B: CV2 / Velocity
DAC Right A: CV3
DAC Right B: CV4
```

The DAC write path remains inside the high-priority sequencer render path.
