# Build Notes

## Current Firmware

Current Teensy main-controller firmware:

```text
v0.3.7
```

Firmware targets:

| File | Target |
|---|---|
| `teensy-main-controller.ino` | Teensy 4.1 main controller |
| `pico-encoder-panel.ino` | Raspberry Pi Pico encoder panel |

The Pico firmware scans the encoder panel and sends UART event packets.

The Teensy firmware runs the sequencer engine, receives Pico events, handles USB MIDI, writes four DAC CV outputs, controls gate and trigger timing, updates the OLED, and updates MCP23017 LED outputs.

## Teensy 4.1 Arduino IDE Settings

```text
Board: Teensy 4.1
USB Type: Serial + MIDI
```

## Pico Arduino IDE Settings

Upload `pico-encoder-panel.ino` to the Raspberry Pi Pico using the installed Pico Arduino core.

```text
TX: GP0
RX: GP1
Baud: 115200
```

## Timing Structure

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
pot reads
MCP23017 LED writes
OLED redraws
```

OLED updates are deferred and transferred in small I2C chunks. Serial debug output is disabled by default.

## Clock Inputs

The firmware supports:

```text
MIDI clock
internal clock
external pulse clock
```

External Clock In and Reset In are logic-level test inputs.

Do not patch Eurorack signals directly into the Teensy. Add appropriate input conditioning and protection first.

## Global Controls

Global encoder:

```text
A: pin 36
B: pin 37
pushbutton: pin 33
```

Dedicated pots:

```text
clock division: pin 14 / A0
number of active steps: pin 15 / A1
```

Buttons:

```text
EDIT: pin 4
RUN / STOP: pin 5
```

## MCP23017 LEDs

```text
GPA0–GPA7: active playhead LEDs
GPB7–GPB0: step-enabled LEDs
```

The enabled-state LED order is intentionally reversed to match the physical LED wiring.

## DAC Section

The current prototype uses:

```text
2 x DAC8562 dual 16-bit DACs
2 x 74AHCT125N quad buffers
5 V DAC and shifter rail
```

Four CV lanes:

```text
DAC1 VOUTA: CV1 / pitch
DAC1 VOUTB: CV2 / velocity
DAC2 VOUTA: CV3
DAC2 VOUTB: CV4
```

Shared DAC signals:

```text
DIN
SCLK
/LDAC
```

Dedicated DAC signals:

```text
DAC1 /SYNC
DAC2 /SYNC
```

The Teensy writes all four input buffers before pulsing shared `/LDAC`, giving simultaneous analogue updates.

## Breadboard Status

The ±12 V analogue-conditioning breadboards have been disconnected temporarily and set aside. Current work is focused on the main brain, front-panel control logic, I/O planning, module width, and PCB stack structure.

Likely panel architecture:

```text
right side:
  4x4 encoder grid

left side:
  OLED
  controls
  jacks and other panel I/O
  Teensy main-brain PCB layer
  Eurorack analogue-conditioning PCB layer
  front-panel hardware PCB layer
  PCB front panel
```

## Next Firmware Task

Add non-volatile state persistence:

```text
save last state
restore last state on power-up
hold a button during power-up to restore defaults
```

## Hardware Review Task

Review the 3.3 V-to-5 V logic translation stage before PCB layout.

The current two-chip 74AHCT125N implementation works, but a cleaner part with a better channel-count fit and fewer configuration connections may simplify the final PCB.
