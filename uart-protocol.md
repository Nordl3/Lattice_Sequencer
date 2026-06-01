# Pico Encoder Panel UART Protocol

The Raspberry Pi Pico encoder panel sends newline-terminated text packets to the Teensy 4.1 main controller.

Baud rate:

```text
115200
```

## Encoder Turn

```text
PANEL=0 ENC=3 EVENT=TURN VALUE=1 POS=127
```

| Field | Meaning |
|---|---|
| `PANEL` | Encoder-panel ID |
| `ENC` | Encoder number local to the panel |
| `EVENT` | Event type |
| `VALUE` | Movement delta for the event |
| `POS` | Raw running encoder position |

`POS` allows the Teensy to recover missed encoder movement. If a packet is lost, the Teensy can compare the latest `POS` value with the previously received position.

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

The Pico firmware handles GPIO scanning, quadrature decoding, button debounce, click detection and hold detection.

The Teensy firmware owns sequencer state and assigns musical meaning to the incoming events.
