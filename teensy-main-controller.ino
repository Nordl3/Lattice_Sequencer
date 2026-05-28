/*
  Lattice Sequencer - Teensy Main Controller

  Teensy 4.1 main sequencer controller.
  Set USB Type to Serial + MIDI.

  - Receives Pico encoder-panel events on Serial5.
  - Responds to USB MIDI start, stop, continue, and clock.
  - Stores per-step pitch, velocity, gate length, CV3, CV4, and enabled state.
  - Drives 2 x MCP4822 dual DACs for four CV outputs.
  - Drives direct GPIO gate and trigger outputs.
  - Uses a raw SSD1306 I2C text display driver.
  - Uses MCP23017 GPA0-GPA7 for step LEDs.
*/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <string.h>
#include <stdlib.h>

const uint8_t GATE_PIN = 2;
const uint8_t TRIG_PIN = 3;
const uint8_t EDIT_BUTTON_PIN = 4;
const uint8_t PLAY_BUTTON_PIN = 5;

const uint8_t CLOCK_DIV_POT = A0;
const uint8_t SEQ_LEN_POT = A1;

const uint8_t PICO_RUN_PIN = 22;

const uint8_t DAC_LEFT_CS = 30;
const uint8_t DAC_RIGHT_CS = 31;

const unsigned long USB_BAUD = 115200;
const unsigned long PICO_UART_BAUD = 115200;

HardwareSerial& PicoSerial = Serial5;
TwoWire& I2CBus = Wire2;

const uint8_t OLED_ADDR = 0x3C;
const uint8_t MCP_ADDR = 0x20;

Adafruit_MCP23X17 mcp;

#define OLED_W 128
#define OLED_H 64
#define OLED_BUF_SIZE 1024

uint8_t oledBuf[OLED_BUF_SIZE];

void oledCommand(uint8_t cmd) {
  I2CBus.beginTransmission(OLED_ADDR);
  I2CBus.write(0x00);
  I2CBus.write(cmd);
  I2CBus.endTransmission();
}

void oledDataBlock(uint8_t* data, uint16_t len) {
  uint16_t pos = 0;

  while (pos < len) {
    uint8_t chunk = min((uint16_t)16, (uint16_t)(len - pos));
    I2CBus.beginTransmission(OLED_ADDR);
    I2CBus.write(0x40);

    for (uint8_t i = 0; i < chunk; i++) {
      I2CBus.write(data[pos++]);
    }

    I2CBus.endTransmission();
  }
}

void oledInit() {
  delay(50);
  oledCommand(0xAE);
  oledCommand(0xD5); oledCommand(0x80);
  oledCommand(0xA8); oledCommand(0x3F);
  oledCommand(0xD3); oledCommand(0x00);
  oledCommand(0x40);
  oledCommand(0x8D); oledCommand(0x14);
  oledCommand(0x20); oledCommand(0x00);
  oledCommand(0xA1);
  oledCommand(0xC8);
  oledCommand(0xDA); oledCommand(0x12);
  oledCommand(0x81); oledCommand(0xCF);
  oledCommand(0xD9); oledCommand(0xF1);
  oledCommand(0xDB); oledCommand(0x40);
  oledCommand(0xA4);
  oledCommand(0xA6);
  oledCommand(0xAF);
}

void oledClearBuffer() {
  memset(oledBuf, 0, OLED_BUF_SIZE);
}

void oledDisplay() {
  oledCommand(0x21);
  oledCommand(0);
  oledCommand(127);
  oledCommand(0x22);
  oledCommand(0);
  oledCommand(7);
  oledDataBlock(oledBuf, OLED_BUF_SIZE);
}

void oledClear() {
  oledClearBuffer();
  oledDisplay();
}

void oledPixel(int x, int y, bool on) {
  if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;

  uint16_t index = x + (y / 8) * OLED_W;
  uint8_t mask = 1 << (y & 7);

  if (on) oledBuf[index] |= mask;
  else oledBuf[index] &= ~mask;
}

void getGlyph(char c, uint8_t out[5]) {
  for (uint8_t i = 0; i < 5; i++) out[i] = 0x00;

  switch (c) {
    case '0': { uint8_t g[5] = {0x3E,0x51,0x49,0x45,0x3E}; memcpy(out,g,5); break; }
    case '1': { uint8_t g[5] = {0x00,0x42,0x7F,0x40,0x00}; memcpy(out,g,5); break; }
    case '2': { uint8_t g[5] = {0x42,0x61,0x51,0x49,0x46}; memcpy(out,g,5); break; }
    case '3': { uint8_t g[5] = {0x21,0x41,0x45,0x4B,0x31}; memcpy(out,g,5); break; }
    case '4': { uint8_t g[5] = {0x18,0x14,0x12,0x7F,0x10}; memcpy(out,g,5); break; }
    case '5': { uint8_t g[5] = {0x27,0x45,0x45,0x45,0x39}; memcpy(out,g,5); break; }
    case '6': { uint8_t g[5] = {0x3C,0x4A,0x49,0x49,0x30}; memcpy(out,g,5); break; }
    case '7': { uint8_t g[5] = {0x01,0x71,0x09,0x05,0x03}; memcpy(out,g,5); break; }
    case '8': { uint8_t g[5] = {0x36,0x49,0x49,0x49,0x36}; memcpy(out,g,5); break; }
    case '9': { uint8_t g[5] = {0x06,0x49,0x49,0x29,0x1E}; memcpy(out,g,5); break; }
    case 'A': { uint8_t g[5] = {0x7E,0x11,0x11,0x11,0x7E}; memcpy(out,g,5); break; }
    case 'B': { uint8_t g[5] = {0x7F,0x49,0x49,0x49,0x36}; memcpy(out,g,5); break; }
    case 'C': { uint8_t g[5] = {0x3E,0x41,0x41,0x41,0x22}; memcpy(out,g,5); break; }
    case 'D': { uint8_t g[5] = {0x7F,0x41,0x41,0x22,0x1C}; memcpy(out,g,5); break; }
    case 'E': { uint8_t g[5] = {0x7F,0x49,0x49,0x49,0x41}; memcpy(out,g,5); break; }
    case 'F': { uint8_t g[5] = {0x7F,0x09,0x09,0x09,0x01}; memcpy(out,g,5); break; }
    case 'G': { uint8_t g[5] = {0x3E,0x41,0x49,0x49,0x7A}; memcpy(out,g,5); break; }
    case 'H': { uint8_t g[5] = {0x7F,0x08,0x08,0x08,0x7F}; memcpy(out,g,5); break; }
    case 'I': { uint8_t g[5] = {0x00,0x41,0x7F,0x41,0x00}; memcpy(out,g,5); break; }
    case 'J': { uint8_t g[5] = {0x20,0x40,0x41,0x3F,0x01}; memcpy(out,g,5); break; }
    case 'K': { uint8_t g[5] = {0x7F,0x08,0x14,0x22,0x41}; memcpy(out,g,5); break; }
    case 'L': { uint8_t g[5] = {0x7F,0x40,0x40,0x40,0x40}; memcpy(out,g,5); break; }
    case 'M': { uint8_t g[5] = {0x7F,0x02,0x0C,0x02,0x7F}; memcpy(out,g,5); break; }
    case 'N': { uint8_t g[5] = {0x7F,0x04,0x08,0x10,0x7F}; memcpy(out,g,5); break; }
    case 'O': { uint8_t g[5] = {0x3E,0x41,0x41,0x41,0x3E}; memcpy(out,g,5); break; }
    case 'P': { uint8_t g[5] = {0x7F,0x09,0x09,0x09,0x06}; memcpy(out,g,5); break; }
    case 'Q': { uint8_t g[5] = {0x3E,0x41,0x51,0x21,0x5E}; memcpy(out,g,5); break; }
    case 'R': { uint8_t g[5] = {0x7F,0x09,0x19,0x29,0x46}; memcpy(out,g,5); break; }
    case 'S': { uint8_t g[5] = {0x46,0x49,0x49,0x49,0x31}; memcpy(out,g,5); break; }
    case 'T': { uint8_t g[5] = {0x01,0x01,0x7F,0x01,0x01}; memcpy(out,g,5); break; }
    case 'U': { uint8_t g[5] = {0x3F,0x40,0x40,0x40,0x3F}; memcpy(out,g,5); break; }
    case 'V': { uint8_t g[5] = {0x1F,0x20,0x40,0x20,0x1F}; memcpy(out,g,5); break; }
    case 'W': { uint8_t g[5] = {0x7F,0x20,0x18,0x20,0x7F}; memcpy(out,g,5); break; }
    case 'X': { uint8_t g[5] = {0x63,0x14,0x08,0x14,0x63}; memcpy(out,g,5); break; }
    case 'Y': { uint8_t g[5] = {0x07,0x08,0x70,0x08,0x07}; memcpy(out,g,5); break; }
    case 'Z': { uint8_t g[5] = {0x61,0x51,0x49,0x45,0x43}; memcpy(out,g,5); break; }
    case '#': { uint8_t g[5] = {0x14,0x7F,0x14,0x7F,0x14}; memcpy(out,g,5); break; }
    case '-': { uint8_t g[5] = {0x08,0x08,0x08,0x08,0x08}; memcpy(out,g,5); break; }
    case '%': { uint8_t g[5] = {0x63,0x13,0x08,0x64,0x63}; memcpy(out,g,5); break; }
    case ' ': default: break;
  }
}

void oledChar(int x, int y, char c, uint8_t scale) {
  uint8_t glyph[5];
  getGlyph(c, glyph);

  for (uint8_t col = 0; col < 5; col++) {
    for (uint8_t row = 0; row < 7; row++) {
      if (glyph[col] & (1 << row)) {
        for (uint8_t sx = 0; sx < scale; sx++) {
          for (uint8_t sy = 0; sy < scale; sy++) {
            oledPixel(x + col * scale + sx, y + row * scale + sy, true);
          }
        }
      }
    }
  }
}

void oledText(int x, int y, const char* text, uint8_t scale) {
  int cx = x;

  while (*text) {
    oledChar(cx, y, *text, scale);
    cx += 6 * scale;
    text++;
  }
}

const uint8_t DAC_CHANNEL_A = 0;
const uint8_t DAC_CHANNEL_B = 1;
const uint16_t DAC_MAX = 4095;

void writeMCP4822(uint8_t csPin, uint8_t channel, uint16_t value) {
  value &= 0x0FFF;

  uint16_t command = 0;
  if (channel == DAC_CHANNEL_B) command |= 0x8000;
  command |= 0x4000;
  command |= 0x2000;
  command |= 0x1000;
  command |= value;

  digitalWrite(csPin, LOW);
  SPI.transfer16(command);
  digitalWrite(csPin, HIGH);
}

void setDacOutput(uint8_t outputIndex, uint16_t value) {
  switch (outputIndex) {
    case 0: writeMCP4822(DAC_LEFT_CS,  DAC_CHANNEL_A, value); break;
    case 1: writeMCP4822(DAC_LEFT_CS,  DAC_CHANNEL_B, value); break;
    case 2: writeMCP4822(DAC_RIGHT_CS, DAC_CHANNEL_A, value); break;
    case 3: writeMCP4822(DAC_RIGHT_CS, DAC_CHANNEL_B, value); break;
  }
}

uint16_t valueToDac(uint8_t value0to100) {
  if (value0to100 > 100) value0to100 = 100;
  return (uint16_t)((uint32_t)value0to100 * DAC_MAX / 100);
}

const uint8_t NUM_STEPS = 8;

int stepNote[NUM_STEPS] = { 36, 37, 38, 39, 40, 41, 42, 43 };
uint8_t stepVelocity[NUM_STEPS] = { 50, 50, 50, 50, 50, 50, 50, 50 };
uint8_t stepGatePct[NUM_STEPS] = { 50, 50, 50, 50, 50, 50, 50, 50 };
uint8_t stepCV3[NUM_STEPS] = { 50, 50, 50, 50, 50, 50, 50, 50 };
uint8_t stepCV4[NUM_STEPS] = { 50, 50, 50, 50, 50, 50, 50, 50 };
bool stepEnabled[NUM_STEPS] = { true, true, true, true, true, true, true, true };

const uint8_t EDIT_PITCH = 0;
const uint8_t EDIT_VELOCITY = 1;
const uint8_t EDIT_GATE = 2;
const uint8_t EDIT_CV3 = 3;
const uint8_t EDIT_CV4 = 4;
const uint8_t NUM_EDIT_MODES = 5;

const uint8_t PLAY_FWD = 0;
const uint8_t PLAY_REV = 1;
const uint8_t PLAY_PING = 2;
const uint8_t PLAY_RAND = 3;
const uint8_t NUM_PLAY_MODES = 4;

uint8_t editMode = EDIT_PITCH;
uint8_t playMode = PLAY_FWD;

const char* editModeName() {
  switch (editMode) {
    case EDIT_PITCH: return "PITCH";
    case EDIT_VELOCITY: return "VEL";
    case EDIT_GATE: return "GATE";
    case EDIT_CV3: return "CV3";
    case EDIT_CV4: return "CV4";
  }
  return "EDIT";
}

const char* playModeName() {
  switch (playMode) {
    case PLAY_FWD: return "FWD";
    case PLAY_REV: return "REV";
    case PLAY_PING: return "PING";
    case PLAY_RAND: return "RAND";
  }
  return "PLAY";
}

const int NOTE_MIN = 24;
const int NOTE_MAX = 84;
const int NOTE_DAC_MIN = 24;
const int NOTE_DAC_RANGE = 60;

const char* noteNames[12] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

void noteToText(int note, char* out, uint8_t outSize) {
  int n = note % 12;
  int octave = (note / 12) - 1;
  snprintf(out, outSize, "%s%d", noteNames[n], octave);
}

uint16_t noteToDac(int note) {
  if (note < NOTE_DAC_MIN) note = NOTE_DAC_MIN;
  if (note > NOTE_DAC_MIN + NOTE_DAC_RANGE) note = NOTE_DAC_MIN + NOTE_DAC_RANGE;
  int semis = note - NOTE_DAC_MIN;
  return (uint16_t)((uint32_t)semis * DAC_MAX / NOTE_DAC_RANGE);
}

const uint8_t NUM_DIVS = 6;
const uint8_t divClockCounts[NUM_DIVS] = { 24, 18, 12, 9, 6, 3 };
const char* divNames[NUM_DIVS] = { "4", "8D", "8", "16D", "16", "32" };

uint8_t clockDivIndex = 4;
uint8_t sequenceLength = 8;
bool midiRunning = false;
uint8_t midiClockCounter = 0;
uint8_t currentStep = 0;
int8_t pingDirection = 1;

unsigned long lastStepTime = 0;
unsigned long measuredStepMs = 250;
bool gateActive = false;
bool trigActive = false;
unsigned long gateOffTime = 0;
unsigned long trigOffTime = 0;
const unsigned long TRIGGER_MS = 10;

const unsigned long DISPLAY_TIMEOUT_MS = 1000;
const unsigned long DISPLAY_REFRESH_MS = 50;
bool displayDirty = false;
bool displayActive = false;
unsigned long lastDisplayRequest = 0;
unsigned long lastDisplayDraw = 0;
char displayLine1[12] = "";
char displayLine2[12] = "";

void requestDisplay(const char* line1, const char* line2) {
  strncpy(displayLine1, line1, sizeof(displayLine1) - 1);
  displayLine1[sizeof(displayLine1) - 1] = '\0';
  strncpy(displayLine2, line2, sizeof(displayLine2) - 1);
  displayLine2[sizeof(displayLine2) - 1] = '\0';
  displayDirty = true;
  displayActive = true;
  lastDisplayRequest = millis();
}

void requestDisplayNumber(const char* label, int value, bool percent) {
  char buf[12];
  if (percent) snprintf(buf, sizeof(buf), "%d%%", value);
  else snprintf(buf, sizeof(buf), "%d", value);
  requestDisplay(label, buf);
}

void drawDisplayNow() {
  oledClearBuffer();
  if (displayActive) {
    oledText(0, 0, displayLine1, 2);
    oledText(0, 28, displayLine2, 4);
  }
  oledDisplay();
  displayDirty = false;
  lastDisplayDraw = millis();
}

void updateDisplay() {
  unsigned long now = millis();
  if (displayDirty && now - lastDisplayDraw >= DISPLAY_REFRESH_MS) drawDisplayNow();
  if (displayActive && now - lastDisplayRequest >= DISPLAY_TIMEOUT_MS) {
    displayActive = false;
    displayDirty = true;
  }
}

void clearStepLeds() {
  for (uint8_t i = 0; i < 8; i++) mcp.digitalWrite(i, LOW);
}

void updateStepLeds() {
  clearStepLeds();
  if (currentStep < 8) mcp.digitalWrite(currentStep, HIGH);
}

bool editButtonLast = HIGH;
bool playButtonLast = HIGH;
unsigned long editButtonChangeTime = 0;
unsigned long playButtonChangeTime = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 25;

void showEditMode() {
  requestDisplay("EDIT", editModeName());
  Serial.print("EDIT MODE: ");
  Serial.println(editModeName());
}

void showPlayMode() {
  requestDisplay("PLAY", playModeName());
  Serial.print("PLAY MODE: ");
  Serial.println(playModeName());
}

void readLocalButtons() {
  unsigned long now = millis();
  bool editNow = digitalRead(EDIT_BUTTON_PIN);
  bool playNow = digitalRead(PLAY_BUTTON_PIN);

  if (editNow != editButtonLast && now - editButtonChangeTime > BUTTON_DEBOUNCE_MS) {
    editButtonChangeTime = now;
    editButtonLast = editNow;
    if (editNow == LOW) {
      editMode++;
      if (editMode >= NUM_EDIT_MODES) editMode = 0;
      showEditMode();
    }
  }

  if (playNow != playButtonLast && now - playButtonChangeTime > BUTTON_DEBOUNCE_MS) {
    playButtonChangeTime = now;
    playButtonLast = playNow;
    if (playNow == LOW) {
      playMode++;
      if (playMode >= NUM_PLAY_MODES) playMode = 0;
      if (playMode == PLAY_REV) currentStep = sequenceLength - 1;
      if (playMode == PLAY_FWD || playMode == PLAY_PING) currentStep = 0;
      pingDirection = 1;
      showPlayMode();
    }
  }
}

void readPots() {
  static uint8_t lastDivIndex = 255;
  static uint8_t lastSeqLength = 255;
  static unsigned long lastPotRead = 0;
  unsigned long now = millis();
  if (now - lastPotRead < 80) return;
  lastPotRead = now;

  int divRaw = analogRead(CLOCK_DIV_POT);
  int lenRaw = analogRead(SEQ_LEN_POT);

  uint8_t newDiv = (uint32_t)divRaw * NUM_DIVS / 1024;
  if (newDiv >= NUM_DIVS) newDiv = NUM_DIVS - 1;

  uint8_t newLen = 1 + ((uint32_t)lenRaw * 8 / 1024);
  if (newLen < 1) newLen = 1;
  if (newLen > 8) newLen = 8;

  if (newDiv != lastDivIndex) {
    clockDivIndex = newDiv;
    lastDivIndex = newDiv;
    requestDisplay("DIV", divNames[clockDivIndex]);
    Serial.print("CLOCK DIV: ");
    Serial.println(divNames[clockDivIndex]);
  }

  if (newLen != lastSeqLength) {
    sequenceLength = newLen;
    lastSeqLength = newLen;
    if (currentStep >= sequenceLength) currentStep = 0;
    requestDisplayNumber("LEN", sequenceLength, false);
    Serial.print("SEQ LENGTH: ");
    Serial.println(sequenceLength);
  }
}

const uint8_t LINE_BUF_SIZE = 120;
char lineBuf[LINE_BUF_SIZE];
uint8_t linePos = 0;

int parsedPanel = -1;
int parsedEnc = -1;
char parsedEvent[16];
int parsedValue = 0;
long parsedPos = 0;
bool parsedHasPos = false;
bool parsedValid = false;

bool picoPosKnown[NUM_STEPS] = { false, false, false, false, false, false, false, false };
long lastPicoPos[NUM_STEPS] = { 0, 0, 0, 0, 0, 0, 0, 0 };

void parsePicoLine(char* line) {
  parsedPanel = -1;
  parsedEnc = -1;
  parsedEvent[0] = '\0';
  parsedValue = 0;
  parsedPos = 0;
  parsedHasPos = false;
  parsedValid = false;

  char* token = strtok(line, " ");
  while (token != nullptr) {
    if (strncmp(token, "PANEL=", 6) == 0) parsedPanel = atoi(token + 6);
    else if (strncmp(token, "ENC=", 4) == 0) parsedEnc = atoi(token + 4);
    else if (strncmp(token, "EVENT=", 6) == 0) {
      strncpy(parsedEvent, token + 6, sizeof(parsedEvent) - 1);
      parsedEvent[sizeof(parsedEvent) - 1] = '\0';
    } else if (strncmp(token, "VALUE=", 6) == 0) parsedValue = atoi(token + 6);
    else if (strncmp(token, "POS=", 4) == 0) {
      parsedPos = atol(token + 4);
      parsedHasPos = true;
    }
    token = strtok(nullptr, " ");
  }

  parsedValid = (parsedPanel >= 0 && parsedEnc >= 0 && parsedEvent[0] != '\0');
}

int getMovementDeltaFromPacket() {
  if (!parsedHasPos || parsedEnc < 0 || parsedEnc >= NUM_STEPS) return parsedValue;

  int delta;
  if (!picoPosKnown[parsedEnc]) {
    delta = parsedValue;
    picoPosKnown[parsedEnc] = true;
  } else {
    long rawDelta = parsedPos - lastPicoPos[parsedEnc];
    if (rawDelta > 50 || rawDelta < -50) delta = parsedValue;
    else delta = (int)rawDelta;
  }

  lastPicoPos[parsedEnc] = parsedPos;
  return delta;
}

int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

void showStepEdited(uint8_t step) {
  if (editMode == EDIT_PITCH) {
    char noteText[8];
    noteToText(stepNote[step], noteText, sizeof(noteText));
    requestDisplay("PITCH", noteText);
  } else if (editMode == EDIT_VELOCITY) requestDisplayNumber("VEL", stepVelocity[step], true);
  else if (editMode == EDIT_GATE) requestDisplayNumber("GATE", stepGatePct[step], true);
  else if (editMode == EDIT_CV3) requestDisplayNumber("CV3", stepCV3[step], true);
  else if (editMode == EDIT_CV4) requestDisplayNumber("CV4", stepCV4[step], true);
}

void editStepValue(uint8_t step, int delta) {
  if (editMode == EDIT_PITCH) stepNote[step] = clampInt(stepNote[step] + delta, NOTE_MIN, NOTE_MAX);
  else if (editMode == EDIT_VELOCITY) stepVelocity[step] = clampInt(stepVelocity[step] + delta * 5, 0, 100);
  else if (editMode == EDIT_GATE) stepGatePct[step] = clampInt(stepGatePct[step] + delta * 5, 0, 100);
  else if (editMode == EDIT_CV3) stepCV3[step] = clampInt(stepCV3[step] + delta * 5, 0, 100);
  else if (editMode == EDIT_CV4) stepCV4[step] = clampInt(stepCV4[step] + delta * 5, 0, 100);

  showStepEdited(step);

  Serial.print("STEP ");
  Serial.print(step);
  Serial.print(" ");
  Serial.print(editModeName());
  Serial.println(" edited");
}

void toggleStep(uint8_t step) {
  stepEnabled[step] = !stepEnabled[step];
  requestDisplay(stepEnabled[step] ? "STEP ON" : "STEP OFF", "");
  Serial.print("STEP ");
  Serial.print(step);
  Serial.print(" ");
  Serial.println(stepEnabled[step] ? "ON" : "OFF");
}

void handleParsedPicoEvent() {
  if (!parsedValid) return;
  if (parsedEnc < 0 || parsedEnc >= NUM_STEPS) return;

  if (strcmp(parsedEvent, "TURN") == 0) {
    int movement = getMovementDeltaFromPacket();
    editStepValue(parsedEnc, movement);
  } else if (strcmp(parsedEvent, "CLICK") == 0) {
    toggleStep(parsedEnc);
  }
}

void handlePicoLine(char* line) {
  Serial.print("PICO: ");
  Serial.println(line);

  char parseCopy[LINE_BUF_SIZE];
  strncpy(parseCopy, line, sizeof(parseCopy) - 1);
  parseCopy[sizeof(parseCopy) - 1] = '\0';
  parsePicoLine(parseCopy);
  handleParsedPicoEvent();
}

void readPicoSerial() {
  while (PicoSerial.available() > 0) {
    char c = PicoSerial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      lineBuf[linePos] = '\0';
      if (linePos > 0) handlePicoLine(lineBuf);
      linePos = 0;
      continue;
    }

    if (linePos < LINE_BUF_SIZE - 1) lineBuf[linePos++] = c;
    else {
      linePos = 0;
      Serial.println("ERROR: Pico UART line overflow");
    }
  }
}

void writeStepCVs(uint8_t step) {
  setDacOutput(0, noteToDac(stepNote[step]));
  setDacOutput(1, valueToDac(stepVelocity[step]));
  setDacOutput(2, valueToDac(stepCV3[step]));
  setDacOutput(3, valueToDac(stepCV4[step]));
}

int findRandomEnabledStep() {
  uint8_t candidates[NUM_STEPS];
  uint8_t count = 0;
  for (uint8_t i = 0; i < sequenceLength; i++) {
    if (stepEnabled[i]) candidates[count++] = i;
  }
  if (count == 0) return -1;
  return candidates[random(count)];
}

void fireCurrentStep() {
  unsigned long now = millis();
  writeStepCVs(currentStep);
  updateStepLeds();

  digitalWrite(GATE_PIN, LOW);
  digitalWrite(TRIG_PIN, LOW);
  gateActive = false;
  trigActive = false;

  if (!stepEnabled[currentStep]) {
    Serial.print("STEP ");
    Serial.print(currentStep);
    Serial.println(" muted");
    return;
  }

  digitalWrite(TRIG_PIN, HIGH);
  trigActive = true;
  trigOffTime = now + TRIGGER_MS;

  if (stepGatePct[currentStep] > 0) {
    unsigned long gateMs = ((uint32_t)measuredStepMs * stepGatePct[currentStep]) / 100;
    if (gateMs < 2) gateMs = 2;
    digitalWrite(GATE_PIN, HIGH);
    gateActive = true;
    gateOffTime = now + gateMs;
  }

  char noteText[8];
  noteToText(stepNote[currentStep], noteText, sizeof(noteText));
  Serial.print("PLAY STEP ");
  Serial.print(currentStep);
  Serial.print(" NOTE ");
  Serial.println(noteText);
}

void advancePlayhead() {
  if (sequenceLength < 1) sequenceLength = 1;

  unsigned long now = millis();
  if (lastStepTime > 0) {
    measuredStepMs = now - lastStepTime;
    if (measuredStepMs < 10) measuredStepMs = 10;
  }
  lastStepTime = now;

  if (playMode == PLAY_RAND) {
    int randomStep = findRandomEnabledStep();
    if (randomStep < 0) {
      clearStepLeds();
      digitalWrite(GATE_PIN, LOW);
      digitalWrite(TRIG_PIN, LOW);
      gateActive = false;
      trigActive = false;
      Serial.println("RANDOM: no enabled steps");
      return;
    }
    currentStep = randomStep;
    fireCurrentStep();
    return;
  }

  fireCurrentStep();

  if (playMode == PLAY_FWD) {
    currentStep++;
    if (currentStep >= sequenceLength) currentStep = 0;
  } else if (playMode == PLAY_REV) {
    if (currentStep == 0) currentStep = sequenceLength - 1;
    else currentStep--;
  } else if (playMode == PLAY_PING) {
    if (sequenceLength <= 1) {
      currentStep = 0;
      return;
    }
    int nextStep = (int)currentStep + pingDirection;
    if (nextStep >= sequenceLength) {
      pingDirection = -1;
      nextStep = sequenceLength - 2;
    }
    if (nextStep < 0) {
      pingDirection = 1;
      nextStep = 1;
    }
    currentStep = (uint8_t)nextStep;
  }
}

void updateGateTrigger() {
  unsigned long now = millis();
  if (trigActive && (long)(now - trigOffTime) >= 0) {
    digitalWrite(TRIG_PIN, LOW);
    trigActive = false;
  }
  if (gateActive && (long)(now - gateOffTime) >= 0) {
    digitalWrite(GATE_PIN, LOW);
    gateActive = false;
  }
}

void handleMidiStart() {
  midiRunning = true;
  midiClockCounter = 0;
  lastStepTime = 0;
  pingDirection = 1;

  if (playMode == PLAY_REV) currentStep = sequenceLength - 1;
  else if (playMode == PLAY_RAND) {
    int s = findRandomEnabledStep();
    currentStep = (s >= 0) ? s : 0;
  } else currentStep = 0;

  requestDisplay("START", "");
  Serial.println("MIDI START");
  fireCurrentStep();
}

void handleMidiStop() {
  midiRunning = false;
  midiClockCounter = 0;
  digitalWrite(GATE_PIN, LOW);
  digitalWrite(TRIG_PIN, LOW);
  gateActive = false;
  trigActive = false;
  requestDisplay("STOP", "");
  Serial.println("MIDI STOP");
}

void handleMidiContinue() {
  midiRunning = true;
  midiClockCounter = 0;
  requestDisplay("CONT", "");
  Serial.println("MIDI CONTINUE");
}

void handleMidiClock() {
  if (!midiRunning) return;
  midiClockCounter++;
  if (midiClockCounter >= divClockCounts[clockDivIndex]) {
    midiClockCounter = 0;
    advancePlayhead();
  }
}

void setupMidiCallbacks() {
  usbMIDI.setHandleStart(handleMidiStart);
  usbMIDI.setHandleStop(handleMidiStop);
  usbMIDI.setHandleContinue(handleMidiContinue);
  usbMIDI.setHandleClock(handleMidiClock);
}

void resetPicoPanel() {
  digitalWrite(PICO_RUN_PIN, LOW);
  delay(50);
  digitalWrite(PICO_RUN_PIN, HIGH);
  delay(500);
}

void setup() {
  Serial.begin(USB_BAUD);
  delay(1000);

  Serial.println();
  Serial.println("Lattice Sequencer - Teensy Main Controller");
  Serial.println("-----------------------------------------");

  pinMode(GATE_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(GATE_PIN, LOW);
  digitalWrite(TRIG_PIN, LOW);

  pinMode(EDIT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PLAY_BUTTON_PIN, INPUT_PULLUP);

  pinMode(PICO_RUN_PIN, OUTPUT);
  digitalWrite(PICO_RUN_PIN, HIGH);

  pinMode(DAC_LEFT_CS, OUTPUT);
  pinMode(DAC_RIGHT_CS, OUTPUT);
  digitalWrite(DAC_LEFT_CS, HIGH);
  digitalWrite(DAC_RIGHT_CS, HIGH);

  SPI.begin();

  setDacOutput(0, 0);
  setDacOutput(1, 0);
  setDacOutput(2, valueToDac(50));
  setDacOutput(3, valueToDac(50));

  PicoSerial.begin(PICO_UART_BAUD);

  I2CBus.begin();
  I2CBus.setClock(400000);

  oledInit();
  oledClear();

  if (!mcp.begin_I2C(MCP_ADDR, &I2CBus)) {
    Serial.println("MCP23017 init failed.");
    while (true) delay(100);
  }

  for (uint8_t i = 0; i < 16; i++) {
    mcp.pinMode(i, OUTPUT);
    mcp.digitalWrite(i, LOW);
  }

  randomSeed(analogRead(A9));
  setupMidiCallbacks();

  resetPicoPanel();
  showEditMode();
  showPlayMode();

  Serial.println("Ready.");
}

void loop() {
  usbMIDI.read();
  readPicoSerial();
  readLocalButtons();
  readPots();
  updateGateTrigger();
  updateDisplay();
}
