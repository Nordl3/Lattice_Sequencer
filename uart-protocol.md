# Pico Encoder Panel UART Protocol

The Raspberry Pi Pico encoder panel sends newline-terminated text packets to the Teensy 4.1 main controller.

Baud rate:

```text
115200
```

The current Teensy build uses two receive-only Pico UART links:

```text
Top panel Pico TX    -> Teensy Serial5 RX pin 21
Bottom panel Pico TX -> Teensy Serial8 RX pin 34
```

The Pico `PANEL` value is not used for top/bottom identification in this build. The Teensy identifies the panel from the UART port.

## Encoder Turn

```text
PANEL=0 ENC=3 EVENT=TURN VALUE=1 POS=127
```

| Field | Meaning |
|---|---|
| `PANEL` | Encoder-panel ID from the Pico firmware |
| `ENC` | Encoder number local to the Pico panel, 0-7 |
| `EVENT` | Event type |
| `VALUE` | Movement delta for the event |
| `POS` | Raw running encoder position |

`POS` allows the Teensy to recover missed encoder movement by comparing the newest position with the previously received position.

## Pushbutton Events

```text
PANEL=0 ENC=3 EVENT=PRESS VALUE=0
PANEL=0 ENC=3 EVENT=CLICK VALUE=1 HELD=180
PANEL=0 ENC=3 EVENT=HOLD VALUE=1 HELD=600
PANEL=0 ENC=3 EVENT=RELEASE VALUE=1 HELD=840
```

| Event | Meaning |
|---|---|
| `PRESS` | Pushbutton pressed |
| `CLICK` | Pushbutton pressed and released before the hold threshold |
| `HOLD` | Pushbutton remains pressed beyond the hold threshold |
| `RELEASE` | Pushbutton released |

| Field | Meaning |
|---|---|
| `VALUE` | Button-event value |
| `HELD` | Button-held duration in milliseconds |

## Panel Ready

```text
PANEL_READY
```

The Pico sends this once after startup.

## Ownership

The Pico firmware handles:

```text
GPIO scanning
quadrature decoding
button debounce
click detection
hold detection
UART event output
```

The Teensy firmware owns:

```text
step data
playhead state
clocking
play mode
CV rendering
gate/trigger timing
display state
LED state
physical encoder-to-step mapping
```
