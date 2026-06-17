/*
  Lattice Sequencer - Teensy 4.1 Main Controller
  Version 0.3.14
  Timing-safe 16-step sequencer core.
*/

#include <SPI.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <string.h>
#include <stdlib.h>

#if !defined(USB_MIDI) && !defined(USB_MIDI_SERIAL) && \
    !defined(USB_MIDI4) && !defined(USB_MIDI4_SERIAL) && \
    !defined(USB_MIDI16) && !defined(USB_MIDI16_SERIAL) && \
    !defined(USB_MIDI_AUDIO_SERIAL) && !defined(USB_MIDI16_AUDIO_SERIAL) && \
    !defined(USB_EVERYTHING)
#error "Select a Teensy USB Type that includes MIDI, such as Serial + MIDI."
#endif

// -----------------------------------------------------------------------------
// Build settings
// -----------------------------------------------------------------------------

const char* FIRMWARE_VERSION = "0.3.14";

const uint8_t NUM_STEPS = 16;

const unsigned long POT_READ_INTERVAL_MS = 50;
const int POT_HYSTERESIS_ADC_COUNTS = 8;

const int GRID_ENCODER_DIRECTION = -1;
const int GLOBAL_ENCODER_DIRECTION = 1;

const uint32_t DAC_SPI_CLOCK_HZ = 30000000UL;
const uint16_t DAC_OUTPUT_MAX_CODE = 0xE000U;

// Encoder/grid mapping.
//
// Physical encoder numbers are 1-16 in real life.
// Internally they are 0-15.
// Serial5/RX5 provides physical encoders 1-8.
// Serial8/RX8 provides physical encoders 9-16.
//
// Current 4x4 board default:
//   top Pico panel has its two rows swapped
//   bottom Pico panel has its two rows swapped
//
// Physical encoder -> logical step:
//   5  6  7  8      becomes      1  2  3  4
//   1  2  3  4                   5  6  7  8
//   13 14 15 16                  9  10 11 12
//   9  10 11 12                  13 14 15 16
const uint8_t ENCODER_TO_STEP[NUM_STEPS] = {
  4,  5,  6,  7,
  0,  1,  2,  3,
  12, 13, 14, 15,
  8,  9,  10, 11
};

uint8_t mapPhysicalEncoderToStep(uint8_t physicalEncoder) {
  if (physicalEncoder >= NUM_STEPS) return 255;
  return ENCODER_TO_STEP[physicalEncoder];
}

// -----------------------------------------------------------------------------
// Main pins
// -----------------------------------------------------------------------------

const uint8_t GATE_PIN = 2;
const uint8_t TRIG_PIN = 3;

const uint8_t LANE_BUTTON_PIN = 4;
const uint8_t RUN_BUTTON_PIN = 5;

const uint8_t PIN_DAC1_SYNC = 30;
const uint8_t PIN_DAC2_SYNC = 31;
const uint8_t PIN_DAC_LDAC = 32;

const uint8_t EXT_CLOCK_PIN = 39;
const uint8_t GLOBAL_BUTTON_PIN = 33;

const uint8_t PICO_BOTTOM_RX8_PIN = 34;

const uint8_t GLOBAL_ENC_A_PIN = 36;
const uint8_t GLOBAL_ENC_B_PIN = 37;

const uint8_t RESET_IN_PIN = 38;

const uint8_t LENGTH_POT_PIN = A0;
const uint8_t CLOCK_DIV_POT_PIN = A1;

// -----------------------------------------------------------------------------
// Serial / UART
// -----------------------------------------------------------------------------

const unsigned long PICO_UART_BAUD = 115200;

HardwareSerial& PicoTopSerial = Serial5;
HardwareSerial& PicoBottomSerial = Serial8;

// -----------------------------------------------------------------------------
// I2C devices
// -----------------------------------------------------------------------------

TwoWire& I2CBus = Wire2;

const uint8_t MCP_LEFT_ADDR = 0x20;
const uint8_t MCP_RIGHT_ADDR = 0x21;

// MCP23017 register addresses in BANK=0 mode.
const uint8_t MCP_IODIRA = 0x00;
const uint8_t MCP_IODIRB = 0x01;
const uint8_t MCP_GPIOA = 0x12;
const uint8_t MCP_GPIOB = 0x13;

// -----------------------------------------------------------------------------
// SSD1309 OLED display backend
// -----------------------------------------------------------------------------

// New 128x64 SSD1309 I2C OLED on the default Teensy I2C bus:
//   SDA = pin 18
//   SCL = pin 19
//
// MCP23017 LED expanders remain on Wire2, pins 24/25.
//
// U8g2 full-buffer mode is used here because it is known to work cleanly with
// this SSD1309 module. Display writes are still deferred and only happen when
// the screen is dirty.

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

bool oledFlushActive = false;

void oledInit() {
  Wire.begin();
  Wire.setClock(400000);

  u8g2.begin();
  u8g2.setBusClock(400000);
  u8g2.setContrast(180);
}

void oledClearBuffer() {
  u8g2.clearBuffer();
}

void oledSetFontForScale(uint8_t scale) {
  (void)scale;
  u8g2.setFont(u8g2_font_helvB12_tr);
}

void oledText(int x, int y, const char* text, uint8_t scale) {
  oledSetFontForScale(scale);
  u8g2.drawStr(x, y + u8g2.getAscent(), text);
}

void oledInt(int x, int y, int value, uint8_t scale) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", value);
  oledText(x, y, buf, scale);
}

void oledStartFlush() {
  oledFlushActive = true;
  u8g2.sendBuffer();
  oledFlushActive = false;
}

void oledFlushOneChunk() {
  // U8g2 is doing the SSD1309 transfer as one hardware-I2C buffer send.
  // The firmware still avoids drawing unless displayDirty is set.
}

// -----------------------------------------------------------------------------
// MCP23017 helpers
// -----------------------------------------------------------------------------

uint8_t mcpLeftPortA = 0;
uint8_t mcpLeftPortB = 0;
uint8_t mcpRightPortA = 0;
uint8_t mcpRightPortB = 0;

bool ledDirty = false;
unsigned long lastLedWriteMs = 0;
const unsigned long LED_WRITE_INTERVAL_MS = 4;

void mcpWriteRegister(uint8_t addr, uint8_t reg, uint8_t value) {
  I2CBus.beginTransmission(addr);
  I2CBus.write(reg);
  I2CBus.write(value);
  I2CBus.endTransmission();
}

void mcpInit(uint8_t addr) {
  mcpWriteRegister(addr, MCP_IODIRA, 0x00);
  mcpWriteRegister(addr, MCP_IODIRB, 0x00);
  mcpWriteRegister(addr, MCP_GPIOA, 0x00);
  mcpWriteRegister(addr, MCP_GPIOB, 0x00);
}

void mcpInitAll() {
  mcpInit(MCP_LEFT_ADDR);
  mcpInit(MCP_RIGHT_ADDR);

  mcpLeftPortA = 0;
  mcpLeftPortB = 0;
  mcpRightPortA = 0;
  mcpRightPortB = 0;
}

void requestLedUpdate(uint8_t activeStep, const bool* enabledSteps) {
  mcpLeftPortA = 0;
  mcpRightPortA = 0;

  if (activeStep < 8) {
    mcpLeftPortA = 1 << activeStep;
  } else if (activeStep < 16) {
    mcpRightPortA = 1 << (activeStep - 8);
  }

  uint8_t leftEnabledMask = 0;
  uint8_t rightEnabledMask = 0;

  for (uint8_t step = 0; step < 8; step++) {
    if (enabledSteps[step]) {
      leftEnabledMask |= (1 << (7 - step));
    }
  }

  for (uint8_t step = 8; step < 16; step++) {
    if (enabledSteps[step]) {
      rightEnabledMask |= (1 << (15 - step));
    }
  }

  mcpLeftPortB = leftEnabledMask;
  mcpRightPortB = rightEnabledMask;
  ledDirty = true;
}

void serviceLedUpdate() {
  if (!ledDirty) return;

  unsigned long now = millis();
  if (now - lastLedWriteMs < LED_WRITE_INTERVAL_MS) return;

  lastLedWriteMs = now;

  mcpWriteRegister(MCP_LEFT_ADDR, MCP_GPIOA, mcpLeftPortA);
  mcpWriteRegister(MCP_LEFT_ADDR, MCP_GPIOB, mcpLeftPortB);
  mcpWriteRegister(MCP_RIGHT_ADDR, MCP_GPIOA, mcpRightPortA);
  mcpWriteRegister(MCP_RIGHT_ADDR, MCP_GPIOB, mcpRightPortB);

  ledDirty = false;
}

// -----------------------------------------------------------------------------
// DAC8562 helpers
// -----------------------------------------------------------------------------

const uint8_t CMD_WRITE_INPUT_ONLY = 0b000;
const uint8_t CMD_SET_LDAC_REGISTER = 0b110;
const uint8_t CMD_INTERNAL_REFERENCE = 0b111;

const uint8_t ADDR_DAC_A = 0b000;
const uint8_t ADDR_DAC_B = 0b001;

SPISettings dacSpiSettings(DAC_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE1);

inline void dacWriteFrame(uint8_t syncPin, uint8_t command, uint8_t address, uint16_t data) {
  const uint32_t frame =
    ((uint32_t)(command & 0x07U) << 19) |
    ((uint32_t)(address & 0x07U) << 16) |
    data;

  digitalWriteFast(syncPin, LOW);

  SPI.transfer((uint8_t)(frame >> 16));
  SPI.transfer((uint8_t)(frame >> 8));
  SPI.transfer((uint8_t)frame);

  digitalWriteFast(syncPin, HIGH);
}

inline void dacWriteInputOnly(uint8_t syncPin, uint8_t address, uint16_t value) {
  dacWriteFrame(syncPin, CMD_WRITE_INPUT_ONLY, address, value);
}

inline void dacPulseLdac() {
  digitalWriteFast(PIN_DAC_LDAC, LOW);
  delayMicroseconds(1);
  digitalWriteFast(PIN_DAC_LDAC, HIGH);
}

void configureDac8562(uint8_t syncPin) {
  dacWriteFrame(syncPin, CMD_INTERNAL_REFERENCE, 0, 0x0001);
  dacWriteFrame(syncPin, CMD_SET_LDAC_REGISTER, 0, 0x0000);
}

void configureDacs() {
  configureDac8562(PIN_DAC1_SYNC);
  configureDac8562(PIN_DAC2_SYNC);

  dacWriteInputOnly(PIN_DAC1_SYNC, ADDR_DAC_A, 0);
  dacWriteInputOnly(PIN_DAC1_SYNC, ADDR_DAC_B, 0);
  dacWriteInputOnly(PIN_DAC2_SYNC, ADDR_DAC_A, 0);
  dacWriteInputOnly(PIN_DAC2_SYNC, ADDR_DAC_B, 0);
  dacPulseLdac();
}

void setAllDacOutputs(uint16_t cv1, uint16_t cv2, uint16_t cv3, uint16_t cv4) {
  dacWriteInputOnly(PIN_DAC1_SYNC, ADDR_DAC_A, cv1);
  dacWriteInputOnly(PIN_DAC1_SYNC, ADDR_DAC_B, cv2);
  dacWriteInputOnly(PIN_DAC2_SYNC, ADDR_DAC_A, cv3);
  dacWriteInputOnly(PIN_DAC2_SYNC, ADDR_DAC_B, cv4);
  dacPulseLdac();
}

uint16_t percentToDac(uint8_t value0to100) {
  if (value0to100 > 100) value0to100 = 100;
  return (uint16_t)((uint32_t)value0to100 * DAC_OUTPUT_MAX_CODE / 100);
}

// -----------------------------------------------------------------------------
// Sequencer state
// -----------------------------------------------------------------------------

// Default chromatic notes: C2 to D#3.
int stepNote[NUM_STEPS] = {
  36, 37, 38, 39, 40, 41, 42, 43,
  44, 45, 46, 47, 48, 49, 50, 51
};

uint8_t stepVelocity[NUM_STEPS] = {
  50, 50, 50, 50, 50, 50, 50, 50,
  50, 50, 50, 50, 50, 50, 50, 50
};

uint8_t stepGatePct[NUM_STEPS] = {
  50, 50, 50, 50, 50, 50, 50, 50,
  50, 50, 50, 50, 50, 50, 50, 50
};

uint8_t stepCV3[NUM_STEPS] = {
  50, 50, 50, 50, 50, 50, 50, 50,
  50, 50, 50, 50, 50, 50, 50, 50
};

uint8_t stepCV4[NUM_STEPS] = {
  50, 50, 50, 50, 50, 50, 50, 50,
  50, 50, 50, 50, 50, 50, 50, 50
};

bool stepEnabled[NUM_STEPS] = {
  true, true, true, true, true, true, true, true,
  true, true, true, true, true, true, true, true
};

const uint8_t EDIT_PITCH = 0;
const uint8_t EDIT_VELOCITY = 1;
const uint8_t EDIT_GATE = 2;
const uint8_t EDIT_CV3 = 3;
const uint8_t EDIT_CV4 = 4;
const uint8_t NUM_EDIT_MODES = 5;

uint8_t editMode = EDIT_PITCH;

const uint8_t PLAY_FWD = 0;
const uint8_t PLAY_REV = 1;
const uint8_t PLAY_PING = 2;
const uint8_t PLAY_RAND = 3;
const uint8_t NUM_PLAY_MODES = 4;

uint8_t playMode = PLAY_FWD;
uint8_t currentStep = 0;
uint8_t visiblePlayheadStep = 0;
int8_t pingDirection = 1;
uint8_t sequenceLength = 16;

// -----------------------------------------------------------------------------
// Scale / pitch helpers
// -----------------------------------------------------------------------------

const int NOTE_MIN = 24;
const int NOTE_MAX = 84;
const int MIDI_TRANSPOSE_REFERENCE = 60;

int transposeOffset = 0;
int activeTransposeNote = -1;

uint8_t rootNote = 0;
bool quantiseEnabled = true;

struct ScaleDef {
  const char* name;
  uint16_t mask;
};

const ScaleDef scales[] = {
  {"CHROM",  0b111111111111},
  {"MAJOR",  0b101011010101},
  {"MINOR",  0b101101011010},
  {"DORIAN", 0b101101010110},
  {"PHRYG",  0b110101011010},
  {"LYDIAN", 0b101010110101},
  {"MIXOLY", 0b101011010110},
  {"HMIN",   0b101101011001},
  {"PMAJ",   0b101001010100},
  {"PMIN",   0b100101010010}
};

const uint8_t NUM_SCALES = sizeof(scales) / sizeof(scales[0]);
uint8_t scaleIndex = 0;

const char* noteNames[12] = {
  "C", "C#", "D", "D#", "E", "F",
  "F#", "G", "G#", "A", "A#", "B"
};

int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

void noteToText(int note, char* out, uint8_t outSize) {
  note = clampInt(note, 0, 127);
  int pitchClass = note % 12;
  int octave = (note / 12) - 1;
  snprintf(out, outSize, "%s%d", noteNames[pitchClass], octave);
}

bool scaleAllowsPitchClass(int pitchClass) {
  int relative = (pitchClass - rootNote + 12) % 12;
  return (scales[scaleIndex].mask & (1 << relative)) != 0;
}

int quantiseNoteNearest(int note) {
  note = clampInt(note, NOTE_MIN, NOTE_MAX);

  if (!quantiseEnabled || scaleIndex == 0) return note;

  for (int distance = 0; distance <= 12; distance++) {
    int down = note - distance;
    int up = note + distance;

    if (down >= NOTE_MIN && scaleAllowsPitchClass(down % 12)) return down;
    if (up <= NOTE_MAX && scaleAllowsPitchClass(up % 12)) return up;
  }

  return note;
}

int renderedPitchNote(uint8_t step) {
  return quantiseNoteNearest(stepNote[step] + transposeOffset);
}

// Temporary prototype pitch mapping:
// C2 = 0V, C6 = approximately 4.096V, 1V/octave.
uint16_t noteToPitchDac(int note) {
  const int baseNote = 36;
  const int maxSemitones = 48;

  note = clampInt(note, baseNote, baseNote + maxSemitones);
  int semitones = note - baseNote;

  return (uint16_t)((uint32_t)semitones * DAC_OUTPUT_MAX_CODE / maxSemitones);
}

// -----------------------------------------------------------------------------
// Clock source and timing
// -----------------------------------------------------------------------------

const uint8_t CLOCK_MIDI = 0;
const uint8_t CLOCK_INT = 1;
const uint8_t CLOCK_EXT = 2;
const uint8_t NUM_CLOCK_SOURCES = 3;

uint8_t clockSource = CLOCK_INT;
int bpm = 120;

const uint8_t NUM_DIVS = 6;
const uint8_t divClockCounts[NUM_DIVS] = {24, 18, 12, 9, 6, 3};
const char* divNames[NUM_DIVS] = {"1/4", "1/8D", "1/8", "1/16D", "1/16", "1/32"};
uint8_t clockDivIndex = 4;

bool transportRunning = false;
uint8_t midiClockCounter = 0;

volatile uint8_t pendingExternalClockEdges = 0;
volatile bool pendingResetEdge = false;

bool pendingMidiStart = false;
bool pendingMidiStop = false;
bool pendingMidiContinue = false;
volatile uint8_t pendingMidiClockTicks = 0;

unsigned long nextInternalStepUs = 0;
unsigned long currentStepDurationUs = 250000;

bool gateActive = false;
bool trigActive = false;
unsigned long gateOffUs = 0;
unsigned long trigOffUs = 0;

const unsigned long TRIGGER_US = 10000;

unsigned long quarterNoteUs() {
  return 60000000UL / (unsigned long)bpm;
}

unsigned long selectedStepDurationUs() {
  return (quarterNoteUs() * divClockCounts[clockDivIndex]) / 24UL;
}

// -----------------------------------------------------------------------------
// Display state
// -----------------------------------------------------------------------------

enum DisplayScreen {
  SCREEN_HOME,
  SCREEN_TEMP,
  SCREEN_GLOBAL,
  SCREEN_SPLASH
};

DisplayScreen displayScreen = SCREEN_SPLASH;

char displayLine1[16] = "";
char displayLine2[16] = "";
char displayLine3[16] = "";
char displayLine4[16] = "";

bool displayDirty = false;
unsigned long displayReturnHomeAtMs = 0;
unsigned long splashEndsAtMs = 0;

const unsigned long STEP_EDIT_DISPLAY_MS = 1000;
const unsigned long GLOBAL_EDIT_DISPLAY_MS = 2000;
const unsigned long SPLASH_MS = 700;

void requestDisplayRedraw() {
  displayDirty = true;
}


void setDisplayLines(const char* l1, const char* l2, const char* l3 = "", const char* l4 = "") {
  strncpy(displayLine1, l1, sizeof(displayLine1) - 1);
  displayLine1[sizeof(displayLine1) - 1] = '\0';

  strncpy(displayLine2, l2, sizeof(displayLine2) - 1);
  displayLine2[sizeof(displayLine2) - 1] = '\0';

  strncpy(displayLine3, l3, sizeof(displayLine3) - 1);
  displayLine3[sizeof(displayLine3) - 1] = '\0';

  strncpy(displayLine4, l4, sizeof(displayLine4) - 1);
  displayLine4[sizeof(displayLine4) - 1] = '\0';

  requestDisplayRedraw();
}

const char* playModeName() {
  switch (playMode) {
    case PLAY_FWD: return "FWD";
    case PLAY_REV: return "REV";
    case PLAY_PING: return "PING";
    case PLAY_RAND: return "RAND";
  }
  return "FWD";
}

const char* clockSourceName() {
  switch (clockSource) {
    case CLOCK_MIDI: return "MIDI";
    case CLOCK_INT: return "INT";
    case CLOCK_EXT: return "EXT";
  }
  return "MIDI";
}

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

void drawStepEnabledGrid(int x0, int y0, uint8_t cell, uint8_t gap) {
  for (uint8_t step = 0; step < NUM_STEPS; step++) {
    uint8_t x = step % 4;
    uint8_t y = step / 4;

    int px = x0 + x * (cell + gap);
    int py = y0 + y * (cell + gap);

    u8g2.drawFrame(px, py, cell, cell);

    if (stepEnabled[step]) {
      u8g2.drawBox(px + 2, py + 2, cell - 4, cell - 4);
    }
  }
}

void drawHomeBuffer() {
  oledClearBuffer();

  char line1[18];
  char line2[18];
  char line3[18];
  char line4[18];

  if (clockSource == CLOCK_INT) {
    snprintf(line1, sizeof(line1), "INT %d", bpm);
  } else {
    snprintf(line1, sizeof(line1), "%s", clockSourceName());
  }

  snprintf(line2, sizeof(line2), "DIV %s", divNames[clockDivIndex]);
  snprintf(line3, sizeof(line3), "LEN %u", sequenceLength);
  snprintf(line4, sizeof(line4), "%s", playModeName());

  oledText(0, 0, line1, 1);
  oledText(0, 16, line2, 1);
  oledText(0, 32, line3, 1);
  oledText(0, 48, line4, 1);

  drawStepEnabledGrid(88, 8, 8, 2);
}

void drawTempBuffer() {
  oledClearBuffer();

  oledText(0, 0, displayLine1, 1);
  oledText(0, 20, displayLine2, 1);
  if (displayLine3[0]) oledText(0, 40, displayLine3, 1);

  drawStepEnabledGrid(88, 8, 8, 2);
}

void drawGlobalBuffer() {
  oledClearBuffer();

  oledText(0, 0, displayLine1, 1);
  oledText(0, 20, displayLine2, 1);
  if (displayLine3[0]) oledText(0, 40, displayLine3, 1);

  drawStepEnabledGrid(88, 8, 8, 2);
}

void drawSplashBuffer() {
  oledClearBuffer();

  oledText(0, 8, "READY", 1);
  oledText(0, 32, FIRMWARE_VERSION, 1);

  drawStepEnabledGrid(88, 8, 8, 2);
}

void rebuildDisplayBuffer() {
  switch (displayScreen) {
    case SCREEN_HOME: drawHomeBuffer(); break;
    case SCREEN_TEMP: drawTempBuffer(); break;
    case SCREEN_GLOBAL: drawGlobalBuffer(); break;
    case SCREEN_SPLASH: drawSplashBuffer(); break;
  }

  oledStartFlush();
  displayDirty = false;
}

void showHomeScreen() {
  displayScreen = SCREEN_HOME;
  displayReturnHomeAtMs = 0;
  requestDisplayRedraw();
}

void showTempValue(const char* label, const char* value, const char* footer = "") {
  setDisplayLines(label, value, footer);
  displayScreen = SCREEN_TEMP;
  displayReturnHomeAtMs = millis() + STEP_EDIT_DISPLAY_MS;
}

void showTempNumber(const char* label, int value, bool percent = false) {
  char buf[16];

  if (percent) snprintf(buf, sizeof(buf), "%d%%", value);
  else snprintf(buf, sizeof(buf), "%d", value);

  showTempValue(label, buf);
}

void showGlobalValue(const char* label, const char* value, const char* footer = "") {
  setDisplayLines(label, value, footer);
  displayScreen = SCREEN_GLOBAL;
  displayReturnHomeAtMs = millis() + GLOBAL_EDIT_DISPLAY_MS;
}

void showGlobalNumber(const char* label, int value) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", value);
  showGlobalValue(label, buf);
}

void serviceDisplay() {
  unsigned long now = millis();

  if (displayScreen == SCREEN_SPLASH && now >= splashEndsAtMs) {
    showHomeScreen();
  }

  if (displayReturnHomeAtMs != 0 && now >= displayReturnHomeAtMs) {
    showHomeScreen();
  }

  if (displayDirty && !oledFlushActive) {
    rebuildDisplayBuffer();
  }

  oledFlushOneChunk();
}

// -----------------------------------------------------------------------------
// Global menu encoder ISR
// -----------------------------------------------------------------------------

const int8_t quadTable[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};

volatile uint8_t globalLastAB = 0;
volatile int8_t globalRawAccum = 0;
volatile int globalEncoderDelta = 0;

uint8_t readGlobalAB() {
  uint8_t a = digitalReadFast(GLOBAL_ENC_A_PIN) ? 1 : 0;
  uint8_t b = digitalReadFast(GLOBAL_ENC_B_PIN) ? 1 : 0;
  return (a << 1) | b;
}

void globalEncoderISR() {
  uint8_t nowAB = readGlobalAB();
  uint8_t transition = (globalLastAB << 2) | nowAB;
  int8_t movement = quadTable[transition];

  globalLastAB = nowAB;

  if (movement == 0) return;

  globalRawAccum += movement;

  if (globalRawAccum >= 4) {
    globalRawAccum = 0;
    globalEncoderDelta += GLOBAL_ENCODER_DIRECTION;
  } else if (globalRawAccum <= -4) {
    globalRawAccum = 0;
    globalEncoderDelta -= GLOBAL_ENCODER_DIRECTION;
  }
}

// -----------------------------------------------------------------------------
// Global menu
// -----------------------------------------------------------------------------

const uint8_t GLOBAL_BPM = 0;
const uint8_t GLOBAL_CLOCK = 1;
const uint8_t GLOBAL_PLAY = 2;
const uint8_t GLOBAL_KEY = 3;
const uint8_t GLOBAL_SCALE = 4;
const uint8_t GLOBAL_QUANT = 5;
const uint8_t NUM_GLOBAL_ITEMS = 6;

uint8_t globalMenuItem = GLOBAL_BPM;

bool globalButtonRaw = HIGH;
bool globalButtonStable = HIGH;
bool globalButtonLastStable = HIGH;
bool globalButtonHoldHandled = false;

unsigned long globalButtonRawChangedAtMs = 0;
unsigned long globalButtonPressedAtMs = 0;

const unsigned long GLOBAL_BUTTON_DEBOUNCE_MS = 25;
const unsigned long GLOBAL_BUTTON_HOLD_MS = 700;

void showCurrentGlobalMenuItem() {
  switch (globalMenuItem) {
    case GLOBAL_BPM:
      showGlobalNumber("BPM", bpm);
      break;

    case GLOBAL_CLOCK:
      showGlobalValue("CLOCK", clockSourceName());
      break;

    case GLOBAL_PLAY:
      showGlobalValue("PLAY", playModeName());
      break;

    case GLOBAL_KEY:
      showGlobalValue("KEY", noteNames[rootNote]);
      break;

    case GLOBAL_SCALE:
      showGlobalValue("SCALE", scales[scaleIndex].name);
      break;

    case GLOBAL_QUANT:
      showGlobalValue("QUANT", quantiseEnabled ? "ON" : "OFF");
      break;
  }
}

void applyGlobalEncoderDelta(int delta) {
  if (delta == 0) return;

  switch (globalMenuItem) {
    case GLOBAL_BPM:
      bpm = clampInt(bpm + delta, 30, 300);
      if (clockSource == CLOCK_INT) {
        nextInternalStepUs = micros() + selectedStepDurationUs();
      }
      break;

    case GLOBAL_CLOCK:
      clockSource = (clockSource + NUM_CLOCK_SOURCES + delta) % NUM_CLOCK_SOURCES;
      midiClockCounter = 0;

      if (clockSource == CLOCK_INT) {
        nextInternalStepUs = micros() + selectedStepDurationUs();
      }
      break;

    case GLOBAL_PLAY:
      playMode = (playMode + NUM_PLAY_MODES + delta) % NUM_PLAY_MODES;
      pingDirection = 1;
      requestLedUpdate(currentStep, stepEnabled);
      break;

    case GLOBAL_KEY:
      rootNote = (rootNote + 12 + delta) % 12;
      break;

    case GLOBAL_SCALE:
      scaleIndex = (scaleIndex + NUM_SCALES + delta) % NUM_SCALES;
      break;

    case GLOBAL_QUANT:
      if (delta != 0) quantiseEnabled = !quantiseEnabled;
      break;
  }

  showCurrentGlobalMenuItem();
}

void serviceGlobalEncoder() {
  int delta = 0;

  noInterrupts();
  delta = globalEncoderDelta;
  globalEncoderDelta = 0;
  interrupts();

  applyGlobalEncoderDelta(delta);
}

void serviceGlobalButton() {
  bool nowRaw = digitalRead(GLOBAL_BUTTON_PIN);
  unsigned long now = millis();

  if (nowRaw != globalButtonRaw) {
    globalButtonRaw = nowRaw;
    globalButtonRawChangedAtMs = now;
  }

  if (now - globalButtonRawChangedAtMs >= GLOBAL_BUTTON_DEBOUNCE_MS) {
    globalButtonStable = globalButtonRaw;
  }

  if (globalButtonStable != globalButtonLastStable) {
    globalButtonLastStable = globalButtonStable;

    if (globalButtonStable == LOW) {
      globalButtonPressedAtMs = now;
      globalButtonHoldHandled = false;
    } else {
      if (!globalButtonHoldHandled) {
        bool menuAlreadyVisible =
          (displayScreen == SCREEN_GLOBAL &&
           displayReturnHomeAtMs != 0 &&
           now < displayReturnHomeAtMs);

        if (menuAlreadyVisible) {
          globalMenuItem++;
          if (globalMenuItem >= NUM_GLOBAL_ITEMS) globalMenuItem = 0;
        }

        showCurrentGlobalMenuItem();
      }
    }
  }

  if (globalButtonStable == LOW && !globalButtonHoldHandled) {
    if (now - globalButtonPressedAtMs >= GLOBAL_BUTTON_HOLD_MS) {
      globalButtonHoldHandled = true;
      showHomeScreen();
    }
  }
}

void forceGateTriggerLow();
void resetPlayhead();
void processSequencerStep(unsigned long stepDurationUs);

// -----------------------------------------------------------------------------
// Local LANE / RUN buttons
// -----------------------------------------------------------------------------

struct DebouncedButton {
  uint8_t pin;
  bool raw;
  bool stable;
  bool lastStable;
  unsigned long changedAtMs;
};

DebouncedButton laneButton = {LANE_BUTTON_PIN, HIGH, HIGH, HIGH, 0};
DebouncedButton runButton = {RUN_BUTTON_PIN, HIGH, HIGH, HIGH, 0};

const unsigned long LOCAL_BUTTON_DEBOUNCE_MS = 25;

bool pollButtonPressed(
  uint8_t pin,
  bool& raw,
  bool& stable,
  bool& lastStable,
  unsigned long& changedAtMs
) {
  bool nowRaw = digitalRead(pin);
  unsigned long now = millis();

  if (nowRaw != raw) {
    raw = nowRaw;
    changedAtMs = now;
  }

  if (now - changedAtMs >= LOCAL_BUTTON_DEBOUNCE_MS) {
    stable = raw;
  }

  bool pressed = false;

  if (stable != lastStable) {
    lastStable = stable;
    pressed = (stable == LOW);
  }

  return pressed;
}

void toggleRunStop() {
  if (transportRunning) {
    transportRunning = false;
    forceGateTriggerLow();
    showTempValue("STOP", "");
    return;
  }

  transportRunning = true;
  resetPlayhead();

  if (clockSource == CLOCK_INT) {
    nextInternalStepUs = micros() + selectedStepDurationUs();
  }

  processSequencerStep(selectedStepDurationUs());
  showTempValue("RUN", "");
}

void serviceLocalButtons() {
  if (pollButtonPressed(
    laneButton.pin,
    laneButton.raw,
    laneButton.stable,
    laneButton.lastStable,
    laneButton.changedAtMs
  )) {
    editMode++;
    if (editMode >= NUM_EDIT_MODES) editMode = 0;
    showTempValue("LANE", editModeName());
  }

  if (pollButtonPressed(
    runButton.pin,
    runButton.raw,
    runButton.stable,
    runButton.lastStable,
    runButton.changedAtMs
  )) {
    toggleRunStop();
  }
}

// -----------------------------------------------------------------------------
// Pico UART parser
// -----------------------------------------------------------------------------

const uint8_t PICO_LINE_BUF_SIZE = 120;
const uint8_t PICO_CHARS_PER_LOOP = 32;

char picoTopLineBuf[PICO_LINE_BUF_SIZE];
uint8_t picoTopLinePos = 0;

char picoBottomLineBuf[PICO_LINE_BUF_SIZE];
uint8_t picoBottomLinePos = 0;

int parsedPanel = -1;
int parsedEnc = -1;
char parsedEvent[16] = "";
int parsedValue = 0;
long parsedPos = 0;
bool parsedHasPos = false;
bool parsedValid = false;

bool picoPosKnown[NUM_STEPS] = {false};
long lastPicoPos[NUM_STEPS] = {0};

uint8_t parsedPhysicalEncoder = 0;
uint8_t parsedStep = 0;

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
    if (strncmp(token, "PANEL=", 6) == 0) {
      parsedPanel = atoi(token + 6);
    } else if (strncmp(token, "ENC=", 4) == 0) {
      parsedEnc = atoi(token + 4);
    } else if (strncmp(token, "EVENT=", 6) == 0) {
      strncpy(parsedEvent, token + 6, sizeof(parsedEvent) - 1);
      parsedEvent[sizeof(parsedEvent) - 1] = '\0';
    } else if (strncmp(token, "VALUE=", 6) == 0) {
      parsedValue = atoi(token + 6);
    } else if (strncmp(token, "POS=", 4) == 0) {
      parsedPos = atol(token + 4);
      parsedHasPos = true;
    }

    token = strtok(nullptr, " ");
  }

  parsedValid = (parsedPanel >= 0 && parsedEnc >= 0 && parsedEvent[0] != '\0');
}

int movementFromParsedPacket() {
  if (!parsedHasPos || parsedPhysicalEncoder >= NUM_STEPS) {
    return parsedValue * GRID_ENCODER_DIRECTION;
  }

  int delta = parsedValue;

  if (picoPosKnown[parsedPhysicalEncoder]) {
    long recovered = parsedPos - lastPicoPos[parsedPhysicalEncoder];

    if (recovered >= -50 && recovered <= 50) {
      delta = (int)recovered;
    }
  } else {
    picoPosKnown[parsedPhysicalEncoder] = true;
  }

  lastPicoPos[parsedPhysicalEncoder] = parsedPos;
  return delta * GRID_ENCODER_DIRECTION;
}

void showStepEdited(uint8_t step) {
  char stepLabel[16];
  char valueText[16];

  snprintf(stepLabel, sizeof(stepLabel), "STEP %u", step + 1);

  if (editMode == EDIT_PITCH) {
    noteToText(stepNote[step], valueText, sizeof(valueText));
  } else if (editMode == EDIT_VELOCITY) {
    snprintf(valueText, sizeof(valueText), "%u%%", stepVelocity[step]);
  } else if (editMode == EDIT_GATE) {
    snprintf(valueText, sizeof(valueText), "%u%%", stepGatePct[step]);
  } else if (editMode == EDIT_CV3) {
    snprintf(valueText, sizeof(valueText), "%u%%", stepCV3[step]);
  } else if (editMode == EDIT_CV4) {
    snprintf(valueText, sizeof(valueText), "%u%%", stepCV4[step]);
  } else {
    snprintf(valueText, sizeof(valueText), "-");
  }

  showTempValue(stepLabel, valueText, editModeName());
}

void editStepValue(uint8_t step, int delta) {
  if (delta == 0) return;

  switch (editMode) {
    case EDIT_PITCH:
      stepNote[step] = clampInt(stepNote[step] + delta, NOTE_MIN, NOTE_MAX);
      break;

    case EDIT_VELOCITY:
      stepVelocity[step] = clampInt(stepVelocity[step] + delta * 5, 0, 100);
      break;

    case EDIT_GATE:
      stepGatePct[step] = clampInt(stepGatePct[step] + delta * 5, 0, 100);
      break;

    case EDIT_CV3:
      stepCV3[step] = clampInt(stepCV3[step] + delta * 5, 0, 100);
      break;

    case EDIT_CV4:
      stepCV4[step] = clampInt(stepCV4[step] + delta * 5, 0, 100);
      break;
  }

  showStepEdited(step);
}

void toggleStepEnabled(uint8_t step) {
  stepEnabled[step] = !stepEnabled[step];

  char stepLabel[16];
  snprintf(stepLabel, sizeof(stepLabel), "STEP %u", step + 1);

  requestLedUpdate(currentStep, stepEnabled);
  requestDisplayRedraw();
  showTempValue(stepLabel, stepEnabled[step] ? "ON" : "OFF");
}

void handleParsedPicoEvent(uint8_t encoderOffset) {
  if (!parsedValid) return;
  if (parsedEnc < 0 || parsedEnc >= 8) return;

  parsedPhysicalEncoder = encoderOffset + parsedEnc;
  if (parsedPhysicalEncoder >= NUM_STEPS) return;

  parsedStep = mapPhysicalEncoderToStep(parsedPhysicalEncoder);
  if (parsedStep >= NUM_STEPS) return;

  if (strcmp(parsedEvent, "TURN") == 0) {
    editStepValue(parsedStep, movementFromParsedPacket());
  } else if (strcmp(parsedEvent, "CLICK") == 0) {
    toggleStepEnabled(parsedStep);
  }
}

void handlePicoLine(char* line, uint8_t encoderOffset) {
  char copy[PICO_LINE_BUF_SIZE];
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';

  parsePicoLine(copy);

  handleParsedPicoEvent(encoderOffset);
}

void servicePicoTopSerialLimited() {
  uint8_t processed = 0;

  while (PicoTopSerial.available() > 0 && processed < PICO_CHARS_PER_LOOP) {
    processed++;

    char c = PicoTopSerial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      picoTopLineBuf[picoTopLinePos] = '\0';

      if (picoTopLinePos > 0) {
        handlePicoLine(picoTopLineBuf, 0);
      }

      picoTopLinePos = 0;
      continue;
    }

    if (picoTopLinePos < PICO_LINE_BUF_SIZE - 1) {
      picoTopLineBuf[picoTopLinePos++] = c;
    } else {
      picoTopLinePos = 0;
    }
  }
}

void servicePicoBottomSerialLimited() {
  uint8_t processed = 0;

  while (PicoBottomSerial.available() > 0 && processed < PICO_CHARS_PER_LOOP) {
    processed++;

    char c = PicoBottomSerial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      picoBottomLineBuf[picoBottomLinePos] = '\0';

      if (picoBottomLinePos > 0) {
        handlePicoLine(picoBottomLineBuf, 8);
      }

      picoBottomLinePos = 0;
      continue;
    }

    if (picoBottomLinePos < PICO_LINE_BUF_SIZE - 1) {
      picoBottomLineBuf[picoBottomLinePos++] = c;
    } else {
      picoBottomLinePos = 0;
    }
  }
}

void servicePicoSerialsLimited() {
  servicePicoTopSerialLimited();
  servicePicoBottomSerialLimited();
}

// -----------------------------------------------------------------------------
// Sequencer rendering
// -----------------------------------------------------------------------------

int findRandomEnabledStep() {
  uint8_t candidates[NUM_STEPS];
  uint8_t count = 0;

  for (uint8_t i = 0; i < sequenceLength; i++) {
    if (stepEnabled[i]) candidates[count++] = i;
  }

  if (count == 0) return -1;
  return candidates[random(count)];
}

void writeStepCVs(uint8_t step) {
  setAllDacOutputs(
    noteToPitchDac(renderedPitchNote(step)),
    percentToDac(stepVelocity[step]),
    percentToDac(stepCV3[step]),
    percentToDac(stepCV4[step])
  );
}

void forceGateTriggerLow() {
  digitalWriteFast(GATE_PIN, LOW);
  digitalWriteFast(TRIG_PIN, LOW);

  gateActive = false;
  trigActive = false;
}

void renderCurrentStepNow(unsigned long stepDurationUs) {
  unsigned long now = micros();

  visiblePlayheadStep = currentStep;

  writeStepCVs(currentStep);
  requestLedUpdate(visiblePlayheadStep, stepEnabled);

  forceGateTriggerLow();

  if (!stepEnabled[currentStep]) return;

  digitalWriteFast(TRIG_PIN, HIGH);
  trigActive = true;
  trigOffUs = now + TRIGGER_US;

  if (stepGatePct[currentStep] > 0) {
    unsigned long gateLengthUs =
      ((uint32_t)stepDurationUs * stepGatePct[currentStep]) / 100UL;

    if (gateLengthUs < 1000) gateLengthUs = 1000;

    digitalWriteFast(GATE_PIN, HIGH);
    gateActive = true;
    gateOffUs = now + gateLengthUs;
  }
}

void movePlayheadAfterRender() {
  if (playMode == PLAY_FWD) {
    currentStep++;
    if (currentStep >= sequenceLength) currentStep = 0;
    return;
  }

  if (playMode == PLAY_REV) {
    if (currentStep == 0) currentStep = sequenceLength - 1;
    else currentStep--;
    return;
  }

  if (playMode == PLAY_PING) {
    if (sequenceLength <= 1) {
      currentStep = 0;
      return;
    }

    int next = currentStep + pingDirection;

    if (next >= sequenceLength) {
      pingDirection = -1;
      next = sequenceLength - 2;
    }

    if (next < 0) {
      pingDirection = 1;
      next = 1;
    }

    currentStep = (uint8_t)next;
    return;
  }

  if (playMode == PLAY_RAND) {
    int next = findRandomEnabledStep();

    if (next >= 0) {
      currentStep = (uint8_t)next;
    } else {
      forceGateTriggerLow();
    }
  }
}

void processSequencerStep(unsigned long stepDurationUs) {
  currentStepDurationUs = stepDurationUs;

  if (playMode == PLAY_RAND) {
    int randomStep = findRandomEnabledStep();

    if (randomStep < 0) {
      forceGateTriggerLow();
      requestLedUpdate(255, stepEnabled);
      return;
    }

    currentStep = (uint8_t)randomStep;
  }

  renderCurrentStepNow(stepDurationUs);

  if (playMode != PLAY_RAND) {
    movePlayheadAfterRender();
  }
}

void serviceGateTriggerTiming() {
  unsigned long now = micros();

  if (trigActive && (long)(now - trigOffUs) >= 0) {
    digitalWriteFast(TRIG_PIN, LOW);
    trigActive = false;
  }

  if (gateActive && (long)(now - gateOffUs) >= 0) {
    digitalWriteFast(GATE_PIN, LOW);
    gateActive = false;
  }
}

// -----------------------------------------------------------------------------
// External clock / reset interrupts
// -----------------------------------------------------------------------------

void externalClockISR() {
  if (pendingExternalClockEdges < 255) pendingExternalClockEdges++;
}

void resetInputISR() {
  pendingResetEdge = true;
}

void resetPlayhead() {
  forceGateTriggerLow();

  midiClockCounter = 0;
  pingDirection = 1;

  if (playMode == PLAY_REV) currentStep = sequenceLength - 1;
  else currentStep = 0;

  visiblePlayheadStep = currentStep;
  requestLedUpdate(visiblePlayheadStep, stepEnabled);
  requestDisplayRedraw();
}

// -----------------------------------------------------------------------------
// USB MIDI callbacks
// -----------------------------------------------------------------------------

void handleMidiStart() {
  pendingMidiStart = true;
}

void handleMidiStop() {
  pendingMidiStop = true;
}

void handleMidiContinue() {
  pendingMidiContinue = true;
}

void handleMidiClock() {
  if (pendingMidiClockTicks < 255) pendingMidiClockTicks++;
}

void handleMidiNoteOn(byte channel, byte note, byte velocity) {
  if (velocity == 0) {
    if (activeTransposeNote == note) {
      activeTransposeNote = -1;
      transposeOffset = 0;
    }
    return;
  }

  activeTransposeNote = note;
  transposeOffset = (int)note - MIDI_TRANSPOSE_REFERENCE;
}

void handleMidiNoteOff(byte channel, byte note, byte velocity) {
  if (activeTransposeNote == note) {
    activeTransposeNote = -1;
    transposeOffset = 0;
  }
}

void setupMidiCallbacks() {
  usbMIDI.setHandleStart(handleMidiStart);
  usbMIDI.setHandleStop(handleMidiStop);
  usbMIDI.setHandleContinue(handleMidiContinue);
  usbMIDI.setHandleClock(handleMidiClock);
  usbMIDI.setHandleNoteOn(handleMidiNoteOn);
  usbMIDI.setHandleNoteOff(handleMidiNoteOff);
}

// -----------------------------------------------------------------------------
// Transport / clock service
// -----------------------------------------------------------------------------

void servicePendingReset() {
  bool shouldReset = false;

  noInterrupts();
  shouldReset = pendingResetEdge;
  pendingResetEdge = false;
  interrupts();

  if (shouldReset) {
    resetPlayhead();
    showTempValue("RESET", "STEP 0");
  }
}

void serviceMidiTransport() {
  if (pendingMidiStop) {
    pendingMidiStop = false;
    transportRunning = false;
    forceGateTriggerLow();
    showTempValue("STOP", "");
  }

  if (pendingMidiStart) {
    pendingMidiStart = false;
    transportRunning = true;
    resetPlayhead();
    showTempValue("START", "");
    processSequencerStep(selectedStepDurationUs());

    if (clockSource == CLOCK_INT) {
      nextInternalStepUs = micros() + selectedStepDurationUs();
    }
  }

  if (pendingMidiContinue) {
    pendingMidiContinue = false;
    transportRunning = true;
    showTempValue("CONT", "");

    if (clockSource == CLOCK_INT) {
      nextInternalStepUs = micros() + selectedStepDurationUs();
    }
  }
}

void serviceMidiClockTicks() {
  if (clockSource != CLOCK_MIDI || !transportRunning) {
    pendingMidiClockTicks = 0;
    return;
  }

  uint8_t ticks = 0;

  noInterrupts();
  ticks = pendingMidiClockTicks;
  pendingMidiClockTicks = 0;
  interrupts();

  while (ticks--) {
    midiClockCounter++;

    if (midiClockCounter >= divClockCounts[clockDivIndex]) {
      midiClockCounter = 0;
      processSequencerStep(selectedStepDurationUs());
    }
  }
}

void serviceInternalClock() {
  if (clockSource != CLOCK_INT || !transportRunning) return;

  unsigned long now = micros();
  unsigned long duration = selectedStepDurationUs();

  if ((long)(now - nextInternalStepUs) >= 0) {
    nextInternalStepUs += duration;
    processSequencerStep(duration);
  }
}

void serviceExternalClock() {
  if (clockSource != CLOCK_EXT || !transportRunning) {
    pendingExternalClockEdges = 0;
    return;
  }

  uint8_t edges = 0;

  noInterrupts();
  edges = pendingExternalClockEdges;
  pendingExternalClockEdges = 0;
  interrupts();

  while (edges--) {
    static unsigned long previousEdgeUs = 0;
    unsigned long now = micros();

    if (previousEdgeUs != 0) {
      currentStepDurationUs = now - previousEdgeUs;
    }

    previousEdgeUs = now;
    processSequencerStep(currentStepDurationUs);
  }
}


// -----------------------------------------------------------------------------
// Front-panel analogue controls
// -----------------------------------------------------------------------------

unsigned long lastAnalogControlMs = 0;

bool potLengthInitialised = false;
bool potDivInitialised = false;

int lengthPotRaw = 0;
int divPotRaw = 0;

uint8_t mapRawToRange(int raw, uint8_t count) {
  raw = clampInt(raw, 0, 1023);

  uint8_t value = (uint32_t)raw * count / 1024UL;
  if (value >= count) value = count - 1;

  return value;
}

bool updatePotMappedValue(uint8_t pin, uint8_t count, bool& initialised, int& heldRaw, uint8_t& mappedValue) {
  int raw = analogRead(pin);
  raw = clampInt(raw, 0, 1023);

  if (!initialised) {
    heldRaw = raw;
    mappedValue = mapRawToRange(raw, count);
    initialised = true;
    return true;
  }

  uint8_t candidate = mapRawToRange(raw, count);
  if (candidate == mappedValue) {
    heldRaw = raw;
    return false;
  }

  int currentLow = ((int)mappedValue * 1024) / count;
  int currentHigh = (((int)mappedValue + 1) * 1024) / count;

  if (candidate > mappedValue && raw >= currentHigh + POT_HYSTERESIS_ADC_COUNTS) {
    heldRaw = raw;
    mappedValue = candidate;
    return true;
  }

  if (candidate < mappedValue && raw <= currentLow - POT_HYSTERESIS_ADC_COUNTS) {
    heldRaw = raw;
    mappedValue = candidate;
    return true;
  }

  return false;
}

void serviceAnalogControls() {
  unsigned long now = millis();
  if (now - lastAnalogControlMs < POT_READ_INTERVAL_MS) return;
  lastAnalogControlMs = now;

  uint8_t lengthIndex = sequenceLength - 1;
  uint8_t divIndex = clockDivIndex;

  bool lengthChanged = updatePotMappedValue(
    LENGTH_POT_PIN,
    NUM_STEPS,
    potLengthInitialised,
    lengthPotRaw,
    lengthIndex
  );

  bool divChanged = updatePotMappedValue(
    CLOCK_DIV_POT_PIN,
    NUM_DIVS,
    potDivInitialised,
    divPotRaw,
    divIndex
  );

  bool changed = false;

  if (lengthChanged) {
    uint8_t newLength = lengthIndex + 1;

    if (newLength != sequenceLength) {
      sequenceLength = newLength;
      if (currentStep >= sequenceLength) currentStep = 0;
      changed = true;
    }
  }

  if (divChanged) {
    if (divIndex != clockDivIndex) {
      clockDivIndex = divIndex;
      midiClockCounter = 0;

      if (clockSource == CLOCK_INT) {
        nextInternalStepUs = micros() + selectedStepDurationUs();
      }

      changed = true;
    }
  }

  if (changed) {
    requestDisplayRedraw();
  }
}

// -----------------------------------------------------------------------------
// Setup / loop
// -----------------------------------------------------------------------------

void setup() {

  pinMode(GATE_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  digitalWriteFast(GATE_PIN, LOW);
  digitalWriteFast(TRIG_PIN, LOW);

  pinMode(LANE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RUN_BUTTON_PIN, INPUT_PULLUP);

  pinMode(PIN_DAC1_SYNC, OUTPUT);
  pinMode(PIN_DAC2_SYNC, OUTPUT);
  pinMode(PIN_DAC_LDAC, OUTPUT);

  digitalWriteFast(PIN_DAC1_SYNC, HIGH);
  digitalWriteFast(PIN_DAC2_SYNC, HIGH);
  digitalWriteFast(PIN_DAC_LDAC, HIGH);

  pinMode(EXT_CLOCK_PIN, INPUT_PULLDOWN);
  pinMode(RESET_IN_PIN, INPUT_PULLDOWN);

  pinMode(GLOBAL_ENC_A_PIN, INPUT_PULLUP);
  pinMode(GLOBAL_ENC_B_PIN, INPUT_PULLUP);
  pinMode(GLOBAL_BUTTON_PIN, INPUT_PULLUP);

  analogReadResolution(10);
  analogReadAveraging(8);

  SPI.begin();
  SPI.beginTransaction(dacSpiSettings);
  configureDacs();

  PicoTopSerial.begin(PICO_UART_BAUD);
  PicoBottomSerial.begin(PICO_UART_BAUD);

  I2CBus.begin();
  I2CBus.setClock(400000);

  oledInit();
  mcpInitAll();

  globalLastAB = readGlobalAB();

  attachInterrupt(digitalPinToInterrupt(GLOBAL_ENC_A_PIN), globalEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(GLOBAL_ENC_B_PIN), globalEncoderISR, CHANGE);

  attachInterrupt(digitalPinToInterrupt(EXT_CLOCK_PIN), externalClockISR, RISING);
  attachInterrupt(digitalPinToInterrupt(RESET_IN_PIN), resetInputISR, RISING);

  setupMidiCallbacks();

  randomSeed(analogRead(A9));

  writeStepCVs(0);

  requestLedUpdate(currentStep, stepEnabled);

  displayScreen = SCREEN_SPLASH;
  splashEndsAtMs = millis() + SPLASH_MS;
  requestDisplayRedraw();

}

void loop() {
  // Highest priority: receive MIDI and service time-critical output events.
  usbMIDI.read();
  serviceGateTriggerTiming();

  servicePendingReset();
  serviceMidiTransport();

  serviceMidiClockTicks();
  serviceInternalClock();
  serviceExternalClock();

  // Medium priority: human input.
  servicePicoSerialsLimited();
  serviceGlobalEncoder();
  serviceGlobalButton();
  serviceLocalButtons();
  serviceAnalogControls();

  // Lowest priority: I2C feedback.
  serviceLedUpdate();
  serviceDisplay();
}
