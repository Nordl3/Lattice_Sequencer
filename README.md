# Lattice Sequencer

Lattice Sequencer is a Eurorack/MIDI hardware sequencer built around a Teensy 4.1 main controller, Raspberry Pi Pico encoder panels, a 4×4 encoder-grid front-panel concept, four analogue CV lanes, gate and trigger outputs, MIDI sync, OLED feedback, and direct per-step editing.

The long-term front panel is centred on a 4×4 grid of rotary encoders with pushbuttons. The current breadboard firmware is an 8-step working prototype using one Pico encoder panel while the main-brain hardware and panel layout are developed.

## Current Prototype

Current Teensy firmware version:

```text
0.3.7
```

The present breadboard build includes:

- Teensy 4.1 main controller
- one Raspberry Pi Pico 8-encoder panel
- two DAC8562 dual 16-bit DACs for four CV outputs
- two 74AHCT125N buffers translating Teensy 3.3 V control signals to 5 V DAC logic
- DAC8562 analogue and logic supply from the 5 V rail
- shared `/LDAC` update line for simultaneous four-channel CV changes
- SSD1306 OLED feedback display
- MCP23017-driven playhead and enabled-step LEDs
- USB MIDI clock and transport sync
- internal clock
- external clock and reset test inputs
- EDIT button
- RUN/STOP button
- global menu encoder and button
- dedicated clock-division pot
- dedicated active-step-count pot

The ±12 V Eurorack conditioning breadboards are temporarily disconnected while main-brain firmware and front-panel hardware planning continue.

## Current Controls

| Control | Function |
|---|---|
| Step encoder turn | Edit the selected parameter for that step |
| Step encoder click | Enable or disable the step |
| EDIT button | Cycle the selected edit lane |
| RUN/STOP button | Start or stop the sequencer |
| Global encoder turn | Adjust the selected global-menu setting |
| Global encoder button | Reveal or advance the global menu |
| Clock-division pot | Select clock division |
| Number-of-steps pot | Select active sequence length |

Global encoder menu:

```text
BPM
CLOCK
PLAY
KEY
SCALE
QUANT
```

The first global-button press after the OLED returns home reveals the current menu item without advancing. Further presses advance while the menu remains visible.

## Edit Lanes

```text
PITCH
VELOCITY
GATE
CV3
CV4
```

## Playback Modes

```text
FWD
REV
PING
RAND
```

## Outputs

| Output | Function |
|---|---|
| CV1 | Pitch / primary CV |
| CV2 | Velocity / secondary CV |
| CV3 | Modulation CV |
| CV4 | Modulation CV |
| Gate | Per-step gate output |
| Trigger | Per-step trigger pulse |

CV1 is rendered as a basic 1 V/oct pitch output over approximately 0–4 V. CV2, CV3, and CV4 use the DAC8562 nominal 0–5 V span. Final Eurorack scaling, calibration, and protection remain part of the analogue-conditioning stage.

## DAC Hardware

The prototype CV section uses:

```text
Teensy 4.1 SPI
    -> 74AHCT125N level shifting
    -> 2 x DAC8562 dual 16-bit DACs
    -> 4 simultaneous CV outputs
```

The DAC8562 internal 2.5 V references are enabled during startup. The DACs use ×2 gain for a nominal 0–5 V raw output range. Each DAC has its own `/SYNC` line. Both DACs share `DIN`, `SCLK`, and `/LDAC`.

The firmware writes all four DAC input buffers and then pulses the shared `/LDAC` line so the four analogue channels update together.

## MIDI

USB MIDI is used for development and Ableton Live sync.

Supported USB MIDI behaviour:

| MIDI message | Behaviour |
|---|---|
| Start | Reset playhead and begin playback |
| Stop | Stop playback and turn off gate and trigger |
| Continue | Resume playback |
| Clock | Advance according to the selected clock division |
| Note On / Off | Temporary pitch transpose |

DIN MIDI remains part of the hardware direction for standalone use.

## Hardware Architecture

```text
Pico encoder panels
    -> scan encoders and buttons
    -> debounce and decode
    -> send UART event packets

Teensy 4.1 main controller
    -> owns sequencer state
    -> handles MIDI and clocks
    -> updates DAC outputs
    -> controls gate and trigger timing
    -> updates OLED and LEDs
```

The Pico panels report hardware events only. Musical state remains on the Teensy.

## OLED Policy

The OLED is a feedback display rather than the primary control surface.

Readability takes priority over information density. The firmware should use the largest practical text and show fewer items at once rather than shrinking important status text.

## Repository Files

| File | Purpose |
|---|---|
| `teensy-main-controller.ino` | Teensy 4.1 main-controller firmware |
| `pico-encoder-panel.ino` | Raspberry Pi Pico encoder-panel firmware |
| `pin-map.md` | Current pin assignments and DAC wiring |
| `uart-protocol.md` | Pico-to-Teensy event protocol |
| `build-notes.md` | Current hardware notes and next tasks |
| `CHANGELOG.md` | Version history |

## Arduino IDE

For the Teensy firmware, select:

```text
Board: Teensy 4.1
USB Type: Serial + MIDI
```

The Pico panel uses UART:

```text
TX: GP0
RX: GP1
Baud: 115200
```

## Versioning

Repository filenames remain stable so Git history stays readable:

```text
teensy-main-controller.ino
pico-encoder-panel.ino
```

Downloadable Arduino-ready ZIP files use versioned folder and sketch names:

```text
Lattice_Sequencer_Teensy_v0_3_7/
└── Lattice_Sequencer_Teensy_v0_3_7.ino
```

Release notes use dotted semantic versions:

```text
v0.3.7
```

Arduino download filenames use underscores:

```text
v0_3_7
```
