# Lattice Sequencer

Lattice Sequencer is a 16-step Eurorack/MIDI hardware sequencer built around a 4x4 encoder grid, four control-voltage outputs, gate and trigger outputs, MIDI sync, OLED feedback, and direct per-step editing.

Each encoder/button represents one step or grid cell. Each step stores pitch, velocity, gate length, and additional CV values. The playhead moves through the grid and renders the active step to the CV, gate, trigger, LED, and display outputs.

## Overview

Lattice is a grid-based step sequencer for controlling Eurorack voices and MIDI-synchronised hardware. The front panel is centred on a 4x4 grid of rotary encoders with push buttons. The grid can be used as a conventional 16-step sequencer or as a spatial control surface for non-linear playhead movement.

The sequencer is designed around direct per-step access. Pitch, velocity, gate length, and modulation values are stored per step. Editing is performed from the encoder assigned to the relevant step or cell.

## 4x4 Grid

The grid layout is:

```text
0   1   2   3
4   5   6   7
8   9   10  11
12  13  14  15
```

The same grid can be addressed in several ways:

- linear step order
- reverse step order
- ping-pong movement
- random enabled-cell selection
- snake/grid paths
- Cartesian X/Y movement
- Euclidean gate patterns
- split-sequencer operation
- manual cell selection
- held/frozen cell behaviour

## Step Data

Each step can store:

| Parameter | Description |
|---|---|
| Pitch | Chromatic note value for CV1 |
| Velocity | Secondary CV value for CV2 |
| Gate length | Per-step gate duration |
| CV3 | Additional modulation CV |
| CV4 | Additional modulation CV |
| Enabled state | Step active/muted state |

The first CV output is intended for pitch or a primary control voltage. The other CV outputs are general-purpose lanes that can be patched to velocity, level, filter cutoff, resonance, modulation depth, waveshape, or any other Eurorack destination.

## Outputs

| Output | Function |
|---|---|
| CV1 | Pitch / primary CV |
| CV2 | Velocity / secondary CV |
| CV3 | Modulation CV |
| CV4 | Modulation CV |
| Gate | Per-step gate output |
| Trigger | Per-step trigger pulse |

Digital gate and trigger outputs are generated separately from the DAC outputs. DAC channels are used only for analogue CV lanes.

## MIDI

Lattice supports MIDI transport and clock sync.

Supported MIDI transport behaviour:

| MIDI message | Behaviour |
|---|---|
| Start | Reset playhead and begin playback |
| Stop | Stop playback and turn off gate/trigger |
| Continue | Resume playback |
| Clock | Advance according to selected clock division |

USB MIDI is used for development and direct DAW sync. DIN MIDI input/output is part of the hardware direction for standalone use with external MIDI devices.

## Clock and Timing

The sequencer advances from MIDI clock, internal timing, or Eurorack clock input depending on the hardware/firmware configuration.

Timing controls include:

- clock division
- sequence length
- gate length
- trigger length
- playback direction
- reset behaviour
- start/stop handling

Useful clock divisions include straight and dotted values that map cleanly to MIDI clock timing.

## Playback Modes

### Forward

Steps advance from low to high.

```text
0 -> 1 -> 2 -> 3 -> ...
```

### Reverse

Steps advance from high to low.

```text
15 -> 14 -> 13 -> 12 -> ...
```

### Ping-pong

The playhead moves forward and then backward through the active range.

```text
0 -> 1 -> 2 -> 3 -> 2 -> 1 -> ...
```

### Random

Random mode selects from enabled steps inside the active range. Disabled steps are excluded from random selection.

### Snake and Grid Paths

Snake and grid-path modes move through the 4x4 grid using stored path shapes rather than a simple linear order. These modes are intended for non-linear melodic and rhythmic movement.

### Cartesian Movement

Cartesian modes use X/Y movement or addressing to select positions in the 4x4 grid. This allows row, column, coordinate, external CV, and reset-based movement schemes.

### Euclidean Gates

Euclidean modes distribute gate or trigger events across a selected length and fill count. Euclidean logic can be applied to gate lanes, trigger lanes, accent behaviour, or step masks.

## Hardware Architecture

Lattice uses a distributed input-panel architecture.

```text
Pico encoder panels
    -> scan encoders and buttons
    -> decode quadrature
    -> debounce buttons
    -> detect press/release/click/hold
    -> send UART event packets

Teensy 4.1 main controller
    -> receives encoder/button events
    -> stores sequencer state
    -> handles MIDI and clocking
    -> updates the playhead
    -> writes DAC outputs
    -> controls gate and trigger timing
    -> updates OLED feedback
    -> updates step LEDs
```

The encoder panels report hardware events only. The main controller owns all musical state.

## Main Controller

The main controller is a Teensy 4.1.

Main-controller responsibilities:

- sequencer timing
- MIDI clock and transport handling
- playhead movement
- step data storage
- DAC output
- gate output
- trigger output
- OLED updates
- LED updates
- encoder-panel UART input
- Eurorack clock/reset/CV input handling

## Encoder Panels

Each Raspberry Pi Pico encoder panel handles eight rotary encoders and their push buttons.

Each panel handles:

- encoder GPIO scanning
- quadrature decoding
- A/B direction correction
- button debounce
- press detection
- release detection
- click detection
- hold detection
- UART event output
- USB Serial debug output

Two Pico panels cover the full 16-encoder grid.

## Encoder Event Protocol

The Pico panels send newline-terminated text packets over UART.

Baud rate:

```text
115200
```

Encoder turn event:

```text
PANEL=0 ENC=3 EVENT=TURN VALUE=1 POS=127
```

Button events:

```text
PANEL=0 ENC=3 EVENT=PRESS VALUE=0
PANEL=0 ENC=3 EVENT=CLICK VALUE=1 HELD=180
PANEL=0 ENC=3 EVENT=HOLD VALUE=1 HELD=600
PANEL=0 ENC=3 EVENT=RELEASE VALUE=1 HELD=840
```

Field meanings:

| Field | Meaning |
|---|---|
| PANEL | Encoder panel ID |
| ENC | Encoder number local to that panel |
| EVENT | Event type |
| VALUE | Movement delta or button event value |
| POS | Raw running encoder position |
| HELD | Button held time in milliseconds |

`POS` allows the main controller to recover missed encoder movement if a UART packet is lost.

## Controls

The panel is based around direct step controls plus global controls.

Per-step controls:

| Control | Function |
|---|---|
| Encoder turn | Edit the selected parameter for that step |
| Encoder click | Enable/disable or select the step |
| Encoder hold | Secondary step action, such as hold/freeze or alternate edit operation |

Global controls can include:

- edit-lane select
- play-mode select
- clock division
- sequence length
- run/stop
- reset
- shift/alternate function
- randomise/mutate
- save/load where supported

## Edit Lanes

The encoder grid can edit different per-step lanes.

Core edit lanes:

```text
PITCH
VELOCITY
GATE
CV3
CV4
```

Pitch uses chromatic note values. The CV-style lanes can be displayed as simple 0-100 values for fast editing or stored at higher internal resolution where required.

## Display

The OLED is used for status feedback.

Typical display content:

- edited parameter
- edited value
- note name
- playback mode
- edit lane
- clock division
- sequence length
- MIDI start/stop/continue
- selected step
- active playhead position

The OLED is a feedback display rather than the primary control surface.

## LED Feedback

Step LEDs show sequencer state.

Typical LED uses:

- active playhead position
- selected step
- enabled/disabled state
- gate status
- held/frozen state
- edit-lane focus
- transport status

LED implementation may use single-colour LEDs, RGB LEDs, or addressable LEDs depending on the hardware build.

## DAC Hardware

The DAC section provides four analogue CV outputs.

The prototype hardware path uses dual SPI DACs. Higher-resolution DACs can be used for better pitch accuracy, finer modulation resolution, and calibration.

DAC design goals:

- four independent CV outputs
- pitch-capable primary CV
- general-purpose modulation CV lanes
- clean update timing
- replaceable DAC layer in firmware
- output scaling suitable for Eurorack levels
- calibration support

## Eurorack Signal Levels

Final Eurorack hardware should provide proper analogue output conditioning.

CV output stages should handle the required output range, such as:

- 0-5 V
- 0-10 V
- +/-5 V
- 1 V/oct pitch CV

Gate and trigger outputs should use suitable output buffering and protection rather than raw microcontroller pins.

## Eurorack Inputs

Useful input types include:

- clock input
- reset input
- run/stop input
- X CV input
- Y CV input
- transpose CV input
- probability/modulation CV input
- freeze/hold gate input

Clock and reset should be treated as timing-critical digital inputs. Analogue CV inputs should be protected and scaled before reaching the controller ADC.

## Pitch and Calibration

Pitch is stored as chromatic note data and rendered to CV.

The pitch output can operate from a basic volts-per-octave mapping or from a calibration table. Calibration can compensate for DAC gain/offset error, analogue output scaling error, and oscillator tracking differences.

A later self-tuning system can use a VCO audio return signal to measure pitch and build correction data for a connected oscillator.

## Split Operation

The 4x4 grid can also be divided into two related sequencer sections. This allows separate lanes, linked patterns, or independent playheads using the same hardware.

Possible split behaviours:

- upper/lower grid split
- left/right grid split
- separate clock divisions
- separate sequence lengths
- independent gate behaviour
- shared or independent CV lanes

## Advanced Sequencing Modes

The grid structure allows modes beyond standard linear sequencing.

Planned advanced modes include:

- snake paths
- random walk
- Cartesian X/Y addressing
- Euclidean gates
- probability masks
- ratchets
- glide/slew
- Klee-style combined-step behaviour
- multiple active cells contributing to output
- pattern mutation
- CV-addressed step selection

## Firmware Principles

The firmware is designed around non-blocking event handling.

Main principles:

- no blocking delays in sequencer timing
- short interrupt routines
- MIDI handled as events
- encoder panels handled as event sources
- DAC updates scheduled from sequencer state
- gate and trigger timing handled independently
- display updates rate-limited
- LED updates treated as low priority compared with clocking and CV output

The main controller owns all musical state. Peripheral controllers scan hardware and report events.

## Build Direction

Lattice is intended as a hardware sequencer platform rather than a single fixed sketch. The core architecture supports a staged build:

- 16-step grid control
- four CV lanes
- gate and trigger output
- MIDI sync
- Eurorack clock/reset
- OLED feedback
- LED feedback
- calibration
- advanced grid sequencing modes

The design keeps timing-critical outputs on the main controller and moves high-density panel scanning to dedicated input panels.
