/*
  Lattice Sequencer - Pico Encoder Panel

  Reads 8 rotary encoders with push buttons.
  Sends newline-terminated UART event packets to the main controller.
  Mirrors the same packets to USB Serial for debugging.

  TURN:
    PANEL=<id> ENC=<index> EVENT=TURN VALUE=<delta> POS=<position>

  BUTTONS:
    PANEL=<id> ENC=<index> EVENT=PRESS VALUE=0
    PANEL=<id> ENC=<index> EVENT=RELEASE VALUE=1 HELD=<ms>
    PANEL=<id> ENC=<index> EVENT=CLICK VALUE=1 HELD=<ms>
    PANEL=<id> ENC=<index> EVENT=HOLD VALUE=1 HELD=<ms>
*/

#include <Arduino.h>
#include <string.h>

const uint8_t PANEL_ID = 0;

const unsigned long USB_BAUD = 115200;
const unsigned long UART_BAUD = 115200;

const unsigned long DEBOUNCE_MS = 20;
const unsigned long HOLD_MS = 600;

const int COUNTS_PER_DETENT = 4;

struct EncoderConfig {
  uint8_t a;
  uint8_t b;
  uint8_t sw;
};

EncoderConfig enc[8] = {
  { 27, 26, 28 },
  { 21, 20, 22 },
  { 18, 17, 19 },
  { 15, 14, 16 },
  {  2,  3,  4 },
  {  5,  6,  7 },
  {  8,  9, 10 },
  { 11, 12, 13 }
};

bool swapAB[8] = {
  true, true, true, true,
  true, true, true, true
};

const int8_t quadTable[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};

uint8_t lastAB[8];
int rawAccum[8];
long encoderPos[8];

bool rawSW[8];
bool stableSW[8];
bool lastStableSW[8];
bool holdSent[8];

unsigned long lastBounceTime[8];
unsigned long pressStartTime[8];

uint8_t readAB(uint8_t i) {
  bool a;
  bool b;

  if (swapAB[i]) {
    a = digitalRead(enc[i].b);
    b = digitalRead(enc[i].a);
  } else {
    a = digitalRead(enc[i].a);
    b = digitalRead(enc[i].b);
  }

  return (a << 1) | b;
}

void sendLine(const char* msg) {
  Serial.println(msg);
  Serial1.println(msg);
}

void sendTurnEvent(uint8_t encIndex, int delta) {
  char msg[80];

  snprintf(
    msg,
    sizeof(msg),
    "PANEL=%u ENC=%u EVENT=TURN VALUE=%d POS=%ld",
    PANEL_ID,
    encIndex,
    delta,
    encoderPos[encIndex]
  );

  sendLine(msg);
}

void sendButtonEvent(uint8_t encIndex, const char* eventName, int value, unsigned long heldMs) {
  char msg[96];

  if (strcmp(eventName, "PRESS") == 0) {
    snprintf(
      msg,
      sizeof(msg),
      "PANEL=%u ENC=%u EVENT=%s VALUE=%d",
      PANEL_ID,
      encIndex,
      eventName,
      value
    );
  } else {
    snprintf(
      msg,
      sizeof(msg),
      "PANEL=%u ENC=%u EVENT=%s VALUE=%d HELD=%lu",
      PANEL_ID,
      encIndex,
      eventName,
      value,
      heldMs
    );
  }

  sendLine(msg);
}

void setupEncoderPanel() {
  unsigned long now = millis();

  for (uint8_t i = 0; i < 8; i++) {
    pinMode(enc[i].a, INPUT_PULLUP);
    pinMode(enc[i].b, INPUT_PULLUP);
    pinMode(enc[i].sw, INPUT_PULLUP);

    lastAB[i] = readAB(i);
    rawAccum[i] = 0;
    encoderPos[i] = 0;

    rawSW[i] = digitalRead(enc[i].sw);
    stableSW[i] = rawSW[i];
    lastStableSW[i] = rawSW[i];

    holdSent[i] = false;
    lastBounceTime[i] = now;
    pressStartTime[i] = 0;
  }
}

void handleEncoder(uint8_t i) {
  uint8_t abNow = readAB(i);
  if (abNow == lastAB[i]) return;

  uint8_t transition = (lastAB[i] << 2) | abNow;
  int8_t movement = quadTable[transition];

  lastAB[i] = abNow;
  if (movement == 0) return;

  rawAccum[i] += movement;

  if (rawAccum[i] >= COUNTS_PER_DETENT) {
    rawAccum[i] = 0;
    encoderPos[i]++;
    sendTurnEvent(i, 1);
  }

  if (rawAccum[i] <= -COUNTS_PER_DETENT) {
    rawAccum[i] = 0;
    encoderPos[i]--;
    sendTurnEvent(i, -1);
  }
}

void handleButton(uint8_t i) {
  bool nowRaw = digitalRead(enc[i].sw);
  unsigned long now = millis();

  if (nowRaw != rawSW[i]) {
    rawSW[i] = nowRaw;
    lastBounceTime[i] = now;
  }

  if ((now - lastBounceTime[i]) >= DEBOUNCE_MS) {
    stableSW[i] = rawSW[i];
  }

  if (stableSW[i] != lastStableSW[i]) {
    lastStableSW[i] = stableSW[i];

    if (stableSW[i] == LOW) {
      pressStartTime[i] = now;
      holdSent[i] = false;
      sendButtonEvent(i, "PRESS", 0, 0);
    } else {
      unsigned long heldMs = now - pressStartTime[i];
      sendButtonEvent(i, "RELEASE", 1, heldMs);

      if (!holdSent[i]) {
        sendButtonEvent(i, "CLICK", 1, heldMs);
      }
    }
  }

  if (stableSW[i] == LOW && !holdSent[i]) {
    unsigned long heldMs = now - pressStartTime[i];

    if (heldMs >= HOLD_MS) {
      holdSent[i] = true;
      sendButtonEvent(i, "HOLD", 1, heldMs);
    }
  }
}

void setup() {
  Serial.begin(USB_BAUD);

  Serial1.setTX(0);
  Serial1.setRX(1);
  Serial1.begin(UART_BAUD);

  delay(1000);
  setupEncoderPanel();

  sendLine("PANEL_READY");
}

void loop() {
  for (uint8_t i = 0; i < 8; i++) {
    handleEncoder(i);
    handleButton(i);
  }

  delayMicroseconds(300);
}
