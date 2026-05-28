# UART Protocol

The Pico encoder panel sends one newline-terminated text packet per event.

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
| PANEL | Encoder panel ID |
| ENC | Encoder number local to that panel |
| EVENT | Event type |
| VALUE | Movement delta |
| POS | Raw running encoder position |

`POS` lets the receiver recover missed encoder movement if a UART packet is lost.

## Button Events

```text
PANEL=0 ENC=3 EVENT=PRESS VALUE=0
PANEL=0 ENC=3 EVENT=CLICK VALUE=1 HELD=180
PANEL=0 ENC=3 EVENT=HOLD VALUE=1 HELD=600
PANEL=0 ENC=3 EVENT=RELEASE VALUE=1 HELD=840
```

| Event | Meaning |
|---|---|
| PRESS | Button pressed |
| CLICK | Press/release without hold |
| HOLD | Button held past hold threshold |
| RELEASE | Button released |

| Field | Meaning |
|---|---|
| HELD | Button held time in milliseconds |

The Pico owns hardware scanning only. The Teensy owns sequencer state and musical meaning.
