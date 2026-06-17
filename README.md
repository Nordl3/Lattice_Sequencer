# Lattice Sequencer

Lattice Sequencer is a 16-step Eurorack/MIDI hardware sequencer built around a 4x4 encoder grid, a Teensy 4.1 main controller, two Raspberry Pi Pico encoder panels, four DAC8562 CV outputs, gate/trigger outputs, OLED status feedback, and MCP23017 LED feedback.

This repository contains the current prototype firmware and wiring notes for the working 16-step breadboard build.

## Current Status

The current main-controller firmware is `v0.3.14`.

Working in the current build:

- 16-step grid control from two Pico encoder panels
- per-step encoder editing
- per-step enable/disable from encoder clicks
- CV1 pitch lane
- CV2 velocity lane
- CV3 and CV4 modulation lanes
- gate and trigger outputs from Teensy GPIO
- internal clock
- USB MIDI clock and transport
- forward, reverse, ping-pong, and random play modes
- SSD1309 OLED status display on its own I2C bus
- two MCP23017 LED expanders for step state and playhead
- DAC8562 four-channel CV output path
- global menu encoder
- front-panel pots for sequence length and clock division

Not yet fully validated in this hardware pass:

- external clock input
- reset input in a full patching setup
- final Eurorack output/input conditioning
- connection to a synth voice for final musical output checks

## Hardware Architecture

Lattice uses a split input architecture.

```text
Pico encoder panel A -> Teensy Serial5 RX -> physical encoders 1-8
Pico encoder panel B -> Teensy Serial8 RX -> physical encoders 9-16

Teensy 4.1:
  sequencer engine
  MIDI / clock handling
  DAC8562 CV output
  gate / trigger timing
  OLED feedback
  MCP23017 LED feedback
```

The Pico boards only scan encoders/buttons and send UART events. The Teensy owns all sequencer state.

## 4x4 Step Grid

User-facing step numbers are 1-16:

```text
1   2   3   4
5   6   7   8
9   10  11  12
13  14  15  16
```

The current firmware includes a top-level encoder-to-step map so the physical encoder board can be corrected without rewriting the sequencer engine.

Current baked mapping:

```text
physical 5   6   7   8    -> steps 1   2   3   4
physical 1   2   3   4    -> steps 5   6   7   8
physical 13  14  15  16   -> steps 9   10  11  12
physical 9   10  11  12   -> steps 13  14  15  16
```

## Per-Step Data

Each step stores:

| Lane | Purpose |
|---|---|
| Pitch | CV1 pitch value |
| Velocity | CV2 secondary CV value |
| Gate | per-step gate length |
| CV3 | general modulation CV |
| CV4 | general modulation CV |
| Enabled | step on/off state |

The LANE button cycles the edited lane. The 16 grid encoders edit the current lane for their assigned step. Encoder clicks toggle the step on/off.

## Front-Panel Controls

| Control | Function |
|---|---|
| 16 grid encoders | edit current lane for steps 1-16 |
| 16 encoder pushbuttons | toggle step on/off |
| LANE button | cycle PITCH, VEL, GATE, CV3, CV4 |
| RUN button | start/stop internal transport |
| Global encoder | adjust selected global menu item |
| Global encoder push | show/cycle global menu item |
| A0 pot | sequence length |
| A1 pot | clock division |

Global menu items:

```text
BPM
CLOCK
PLAY
KEY
SCALE
QUANT
```

Length and clock division are intentionally controlled by pots and are not in the global menu.

## Clocking

Clock source options:

```text
INT
MIDI
EXT
```

The default clock source is `INT`.

MIDI clock and transport are available over USB MIDI. External clock support is present in firmware but still needs final hardware validation and input conditioning.

## DAC Outputs

The current CV hardware is based on two DAC8562 dual 16-bit SPI DACs.

| Output | DAC8562 channel |
|---|---|
| CV1 / Pitch | DAC1 VOUTA |
| CV2 / Velocity | DAC1 VOUTB |
| CV3 | DAC2 VOUTA |
| CV4 | DAC2 VOUTB |

The firmware enables the DAC8562 internal 2.5 V reference and uses x2 gain. `DAC_OUTPUT_MAX_CODE` is currently set to `0xE000` to leave headroom on the present prototype supply.

The DAC8562 backend uses:

```text
SPI_MODE1
30 MHz SPI
24-bit command frames
shared LDAC pulse after all four CV values are written
```

## Gate and Trigger Outputs

Gate and trigger are generated from Teensy digital outputs.

| Output | Teensy pin |
|---|---:|
| Gate | 2 |
| Trigger | 3 |

These pins are firmware outputs. Final Eurorack hardware should buffer and condition them before patching into a modular system.

## OLED

The current display is a 128x64 SSD1309 I2C OLED at address `0x3C`.

It is connected to the default Teensy I2C bus:

```text
SDA = pin 18
SCL = pin 19
```

The OLED shows status and step on/off grid state. It does not animate the playhead during playback; the hardware LED expanders provide live playhead feedback. This keeps transport timing ahead of display work.

## LED Feedback

Two MCP23017 expanders are used on `Wire2`:

```text
0x20 = left LED expander, steps 1-8
0x21 = right LED expander, steps 9-16
```

Each expander uses one port for playhead LEDs and one port for step enabled/on-off LEDs.

## Firmware Files

| File | Target |
|---|---|
| `teensy-main-controller.ino` | Teensy 4.1 main sequencer firmware |
| `pico-encoder-panel.ino` | Raspberry Pi Pico encoder panel firmware |

The same Pico firmware is used on both encoder panels. The Teensy identifies the panel by UART port, not by the `PANEL` value in the packet.
