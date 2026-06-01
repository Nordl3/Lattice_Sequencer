/*
  Lattice Sequencer - Teensy 4.1 Main Controller
  Version 0.3.4

  Timing-safe 8-step sequencer core.

  Main timing priority:
    1. USB MIDI / internal / external clock events
    2. DAC CV writes, gate and trigger timing
    3. Pico panel UART input and local controls
    4. MCP23017 LEDs and OLED updates

  Teensy 4.1 pin map:
    Gate Out                  2
    Trigger Out               3
    EDIT button               4
    PLAY button               5

    Pico Serial5 TX          20
    Pico Serial5 RX          21
    Pico RUN                 22

    Wire2 SCL                24
    Wire2 SDA                25

    DAC Left CS              30
    DAC Right CS             31

    External Clock In        32
    Global encoder button    33
    Reserved Serial8 RX      34
    Reserved Serial8 TX      35
    Global encoder A         36
    Global encoder B         37
    Reset In                 38

    SPI MOSI                 11
    SPI SCK                  13

  I2C:
    MCP23017                 0x20
    OLED                     0x3C

  MCP23017 LED map:
    GPA0-GPA7 = active playhead LEDs, step 0-7
    GPB7-GPB0 = enabled-state LEDs, step 0-7
*/

#include <SPI.h>
#include <Wire.h>
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

const char* FIRMWARE_VERSION = "0.3.4";
const bool DEBUG_SERIAL = false;

// -----------------------------------------------------------------------------
// Main pins
// -----------------------------------------------------------------------------

const uint8_t GATE_PIN = 2;
const uint8_t TRIG_PIN = 3;

const uint8_t EDIT_BUTTON_PIN = 4;
const uint8_t PLAY_BUTTON_PIN = 5;

const uint8_t PICO_RUN_PIN = 22;

const uint8_t DAC_LEFT_CS = 30;
const uint8_t DAC_RIGHT_CS = 31;

const uint8_t EXT_CLOCK_PIN = 32;
const uint8_t GLOBAL_BUTTON_PIN = 33;

// Keep 34 and 35 spare for Serial8 RX/TX.
const uint8_t GLOBAL_ENC_A_PIN = 36;
const uint8_t GLOBAL_ENC_B_PIN = 37;

const uint8_t RESET_IN_PIN = 38;

// -----------------------------------------------------------------------------
// Serial / UART
// -----------------------------------------------------------------------------

const unsigned long USB_BAUD = 115200;
const unsigned long PICO_UART_BAUD = 115200;

HardwareSerial& PicoSerial = Serial5;

// -----------------------------------------------------------------------------
// I2C devices
// -----------------------------------------------------------------------------

TwoWire& I2CBus = Wire2;

const uint8_t OLED_ADDR = 0x3C;
const uint8_t MCP_ADDR = 0x20;

// MCP23017 register addresses in BANK=0 mode.
const uint8_t MCP_IODIRA = 0x00;
const uint8_t MCP_IODIRB = 0x01;
const uint8_t MCP_GPIOA = 0x12;
const uint8_t MCP_GPIOB = 0x13;

// -----------------------------------------------------------------------------
// Raw SSD1306 OLED driver
// -----------------------------------------------------------------------------

#define OLED_W 128
#define OLED_H 64
#define OLED_BUF_SIZE 1024

uint8_t oledBuf[OLED_BUF_SIZE];

bool oledFlushActive = false;
uint16_t oledFlushPos = 0;

void oledCommand(uint8_t cmd) {
  I2CBus.beginTransmission(OLED_ADDR);
  I2CBus.write(0x00);
  I2CBus.write(cmd);
  I2CBus.endTransmission();
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

void oledPixel(int x, int y, bool on) {
  if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;

  uint16_t index = x + (y / 8) * OLED_W;
  uint8_t mask = 1 << (y & 7);

  if (on) oledBuf[index] |= mask;
  else oledBuf[index] &= ~mask;
}

void getGlyph(char c, uint8_t out[5]) {
  for (uint8_t i = 0; i < 5; i++) out[i] = 0;

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
    case '/': { uint8_t g[5] = {0x20,0x10,0x08,0x04,0x02}; memcpy(out,g,5); break; }
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

void oledInt(int x, int y, int value, uint8_t scale) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", value);
  oledText(x, y, buf, scale);
}

void oledStartFlush() {
  oledCommand(0x21);
  oledCommand(0);
  oledCommand(127);

  oledCommand(0x22);
  oledCommand(0);
  oledCommand(7);

  oledFlushPos = 0;
  oledFlushActive = true;
}

void oledFlushOneChunk() {
  if (!oledFlushActive) return;

  const uint8_t CHUNK = 16;
  uint16_t remaining = OLED_BUF_SIZE - oledFlushPos;
  uint8_t sendCount = remaining > CHUNK ? CHUNK : remaining;

  I2CBus.beginTransmission(OLED_ADDR);
  I2CBus.write(0x40);

  for (uint8_t i = 0; i < sendCount; i++) {
    I2CBus.write(oledBuf[oledFlushPos++]);
  }

  I2CBus.endTransmission();

  if (oledFlushPos >= OLED_BUF_SIZE) {
    oledFlushActive = false;
  }
}

// -----------------------------------------------------------------------------
// MCP23017 helpers
// -----------------------------------------------------------------------------

uint8_t mcpPortA = 0;
uint8_t mcpPortB = 0;

bool ledDirty = false;
unsigned long lastLedWriteMs = 0;
const unsigned long LED_WRITE_INTERVAL_MS = 4;

void mcpWriteRegister(uint8_t reg, uint8_t value) {
  I2CBus.beginTransmission(MCP_ADDR);
  I2CBus.write(reg);
  I2CBus.write(value);
  I2CBus.endTransmission();
}

void mcpInit() {
  mcpWriteRegister(MCP_IODIRA, 0x00);
  mcpWriteRegister(MCP_IODIRB, 0x00);
  mcpPortA = 0;
  mcpPortB = 0;
  mcpWriteRegister(MCP_GPIOA, mcpPortA);
  mcpWriteRegister(MCP_GPIOB, mcpPortB);
}

void requestLedUpdate(uint8_t activeStep, const bool* enabledSteps) {
  mcpPortA = (activeStep < 8) ? (1 << activeStep) : 0;

  uint8_t enabledMask = 0;
  for (uint8_t step = 0; step < 8; step++) {
    if (enabledSteps[step]) {
      enabledMask |= (1 << (7 - step));
    }
  }

  mcpPortB = enabledMask;
  ledDirty = true;
}

void serviceLedUpdate() {
  if (!ledDirty) return;

  unsigned long now = millis();
  if (now - lastLedWriteMs < LED_WRITE_INTERVAL_MS) return;

  lastLedWriteMs = now;
  mcpWriteRegister(MCP_GPIOA, mcpPortA);
  mcpWriteRegister(MCP_GPIOB, mcpPortB);
  ledDirty = false;
}

// -----------------------------------------------------------------------------
// DAC helpers
// -----------------------------------------------------------------------------

const uint8_t DAC_CHANNEL_A = 0;
const uint8_t DAC_CHANNEL_B = 1;
const uint16_t DAC_MAX = 4095;

SPISettings dacSpiSettings(8000000, MSBFIRST, SPI_MODE0);

void writeMCP4822(uint8_t csPin, uint8_t channel, uint16_t value) {
  value &= 0x0FFF;

  uint16_t command = 0;
  if (channel == DAC_CHANNEL_B) command |= 0x8000;

  command |= 0x4000;
  command |= 0x2000;
  command |= 0x1000;
  command |= value;

  SPI.beginTransaction(dacSpiSettings);
  digitalWriteFast(csPin, LOW);
  SPI.transfer16(command);
  digitalWriteFast(csPin, HIGH);
  SPI.endTransaction();
}

void setDacOutput(uint8_t outputIndex, uint16_t value) {
  switch (outputIndex) {
    case 0: writeMCP4822(DAC_LEFT_CS,  DAC_CHANNEL_A, value); break;
    case 1: writeMCP4822(DAC_LEFT_CS,  DAC_CHANNEL_B, value); break;
    case 2: writeMCP4822(DAC_RIGHT_CS, DAC_CHANNEL_A, value); break;
    case 3: writeMCP4822(DAC_RIGHT_CS, DAC_CHANNEL_B, value); break;
  }
}

uint16_t percentToDac(uint8_t value0to100) {
  if (value0to100 > 100) value0to100 = 100;
  return (uint16_t)((uint32_t)value0to100 * DAC_MAX / 100);
}

// -----------------------------------------------------------------------------
// Sequencer state
// -----------------------------------------------------------------------------

const uint8_t NUM_STEPS = 8;

// Default chromatic notes: C2 to G2.
int stepNote[NUM_STEPS] = {36, 37, 38, 39, 40, 41, 42, 43};
uint8_t stepVelocity[NUM_STEPS] = {50, 50, 50, 50, 50, 50, 50, 50};
uint8_t stepGatePct[NUM_STEPS] = {50, 50, 50, 50, 50, 50, 50, 50};
uint8_t stepCV3[NUM_STEPS] = {50, 50, 50, 50, 50, 50, 50, 50};
uint8_t stepCV4[NUM_STEPS] = {50, 50, 50, 50, 50, 50, 50, 50};
bool stepEnabled[NUM_STEPS] = {true, true, true, true, true, true, true, true};

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
int8_t pingDirection = 1;
uint8_t sequenceLength = 8;

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

  return (uint16_t)((uint32_t)semitones * DAC_MAX / maxSemitones);
}

// -----------------------------------------------------------------------------
// Clock source and timing
// -----------------------------------------------------------------------------

const uint8_t CLOCK_MIDI = 0;
const uint8_t CLOCK_INT = 1;
const uint8_t CLOCK_EXT = 2;
const uint8_t NUM_CLOCK_SOURCES = 3;

uint8_t clockSource = CLOCK_MIDI;
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
const unsigned long SPLASH_MS = 1500;

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

void drawHomeBuffer() {
  oledClearBuffer();

  char line2[16];
  char line3[16];
  char line4[16];

  snprintf(line2, sizeof(line2), "%s %s", noteNames[rootNote], scales[scaleIndex].name);

  if (clockSource == CLOCK_INT) {
    snprintf(line3, sizeof(line3), "INT %d BPM", bpm);
  } else {
    snprintf(line3, sizeof(line3), "%s CLOCK", clockSourceName());
  }

  snprintf(line4, sizeof(line4), "%s LEN %u", playModeName(), sequenceLength);

  oledText(16, 0, "LATTICE", 2);
  oledText(0, 20, line2, 1);
  oledText(0, 32, line3, 1);
  oledText(0, 44, line4, 1);
  oledText(0, 56, divNames[clockDivIndex], 1);
}

void drawTempBuffer() {
  oledClearBuffer();
  oledText(0, 0, displayLine1, 2);
  oledText(0, 28, displayLine2, 4);

  if (displayLine3[0]) oledText(0, 56, displayLine3, 1);
}

void drawGlobalBuffer() {
  oledClearBuffer();
  oledText(0, 0, displayLine1, 2);
  oledText(0, 28, displayLine2, 3);

  if (displayLine3[0]) oledText(0, 56, displayLine3, 1);
}

void drawSplashBuffer() {
  oledClearBuffer();
  oledText(16, 8, "LATTICE", 2);
  oledText(34, 34, "SEQ", 2);
  oledText(36, 54, FIRMWARE_VERSION, 1);
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
    globalEncoderDelta++;
  } else if (globalRawAccum <= -4) {
    globalRawAccum = 0;
    globalEncoderDelta--;
  }
}

// -----------------------------------------------------------------------------
// Global menu
// -----------------------------------------------------------------------------

const uint8_t GLOBAL_BPM = 0;
const uint8_t GLOBAL_CLOCK = 1;
const uint8_t GLOBAL_DIV = 2;
const uint8_t GLOBAL_LENGTH = 3;
const uint8_t GLOBAL_KEY = 4;
const uint8_t GLOBAL_SCALE = 5;
const uint8_t GLOBAL_QUANT = 6;
const uint8_t NUM_GLOBAL_ITEMS = 7;

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

    case GLOBAL_DIV:
      showGlobalValue("DIV", divNames[clockDivIndex]);
      break;

    case GLOBAL_LENGTH:
      showGlobalNumber("LENGTH", sequenceLength);
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

    case GLOBAL_DIV:
      clockDivIndex = (clockDivIndex + NUM_DIVS + delta) % NUM_DIVS;
      midiClockCounter = 0;

      if (clockSource == CLOCK_INT) {
        nextInternalStepUs = micros() + selectedStepDurationUs();
      }
      break;

    case GLOBAL_LENGTH:
      sequenceLength = clampInt(sequenceLength + delta, 1, NUM_STEPS);
      if (currentStep >= sequenceLength) currentStep = 0;
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
        globalMenuItem++;
        if (globalMenuItem >= NUM_GLOBAL_ITEMS) globalMenuItem = 0;
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

// -----------------------------------------------------------------------------
// Local EDIT / PLAY buttons
// -----------------------------------------------------------------------------

struct DebouncedButton {
  uint8_t pin;
  bool raw;
  bool stable;
  bool lastStable;
  unsigned long changedAtMs;
};

DebouncedButton editButton = {EDIT_BUTTON_PIN, HIGH, HIGH, HIGH, 0};
DebouncedButton playButton = {PLAY_BUTTON_PIN, HIGH, HIGH, HIGH, 0};

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

void serviceLocalButtons() {
  if (pollButtonPressed(
    editButton.pin,
    editButton.raw,
    editButton.stable,
    editButton.lastStable,
    editButton.changedAtMs
  )) {
    editMode++;
    if (editMode >= NUM_EDIT_MODES) editMode = 0;
    showTempValue("EDIT", editModeName());
  }

  if (pollButtonPressed(
    playButton.pin,
    playButton.raw,
    playButton.stable,
    playButton.lastStable,
    playButton.changedAtMs
  )) {
    playMode++;
    if (playMode >= NUM_PLAY_MODES) playMode = 0;

    pingDirection = 1;

    if (playMode == PLAY_REV) currentStep = sequenceLength - 1;
    else currentStep = 0;

    requestLedUpdate(currentStep, stepEnabled);
    showTempValue("PLAY", playModeName());
  }
}

// -----------------------------------------------------------------------------
// Pico UART parser
// -----------------------------------------------------------------------------

const uint8_t PICO_LINE_BUF_SIZE = 120;
const uint8_t PICO_CHARS_PER_LOOP = 32;

char picoLineBuf[PICO_LINE_BUF_SIZE];
uint8_t picoLinePos = 0;

int parsedPanel = -1;
int parsedEnc = -1;
char parsedEvent[16] = "";
int parsedValue = 0;
long parsedPos = 0;
bool parsedHasPos = false;
bool parsedValid = false;

bool picoPosKnown[NUM_STEPS] = {false, false, false, false, false, false, false, false};
long lastPicoPos[NUM_STEPS] = {0, 0, 0, 0, 0, 0, 0, 0};

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
  if (!parsedHasPos || parsedEnc < 0 || parsedEnc >= NUM_STEPS) {
    return parsedValue;
  }

  int delta = parsedValue;

  if (picoPosKnown[parsedEnc]) {
    long recovered = parsedPos - lastPicoPos[parsedEnc];

    if (recovered >= -50 && recovered <= 50) {
      delta = (int)recovered;
    }
  } else {
    picoPosKnown[parsedEnc] = true;
  }

  lastPicoPos[parsedEnc] = parsedPos;
  return delta;
}

void showStepEdited(uint8_t step) {
  if (editMode == EDIT_PITCH) {
    char noteBuf[8];
    noteToText(stepNote[step], noteBuf, sizeof(noteBuf));
    showTempValue("PITCH", noteBuf);
  } else if (editMode == EDIT_VELOCITY) {
    showTempNumber("VEL", stepVelocity[step], true);
  } else if (editMode == EDIT_GATE) {
    showTempNumber("GATE", stepGatePct[step], true);
  } else if (editMode == EDIT_CV3) {
    showTempNumber("CV3", stepCV3[step], true);
  } else if (editMode == EDIT_CV4) {
    showTempNumber("CV4", stepCV4[step], true);
  }
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

  requestLedUpdate(currentStep, stepEnabled);
  showTempValue(stepEnabled[step] ? "STEP ON" : "STEP OFF", noteNames[step % 12]);
}

void handleParsedPicoEvent() {
  if (!parsedValid) return;
  if (parsedEnc < 0 || parsedEnc >= NUM_STEPS) return;

  if (strcmp(parsedEvent, "TURN") == 0) {
    editStepValue(parsedEnc, movementFromParsedPacket());
  } else if (strcmp(parsedEvent, "CLICK") == 0) {
    toggleStepEnabled(parsedEnc);
  }
}

void handlePicoLine(char* line) {
  char copy[PICO_LINE_BUF_SIZE];
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';

  parsePicoLine(copy);
  handleParsedPicoEvent();
}

void servicePicoSerialLimited() {
  uint8_t processed = 0;

  while (PicoSerial.available() > 0 && processed < PICO_CHARS_PER_LOOP) {
    processed++;

    char c = PicoSerial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      picoLineBuf[picoLinePos] = '\0';

      if (picoLinePos > 0) {
        handlePicoLine(picoLineBuf);
      }

      picoLinePos = 0;
      continue;
    }

    if (picoLinePos < PICO_LINE_BUF_SIZE - 1) {
      picoLineBuf[picoLinePos++] = c;
    } else {
      picoLinePos = 0;
    }
  }
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
  setDacOutput(0, noteToPitchDac(renderedPitchNote(step)));
  setDacOutput(1, percentToDac(stepVelocity[step]));
  setDacOutput(2, percentToDac(stepCV3[step]));
  setDacOutput(3, percentToDac(stepCV4[step]));
}

void forceGateTriggerLow() {
  digitalWriteFast(GATE_PIN, LOW);
  digitalWriteFast(TRIG_PIN, LOW);

  gateActive = false;
  trigActive = false;
}

void renderCurrentStepNow(unsigned long stepDurationUs) {
  unsigned long now = micros();

  writeStepCVs(currentStep);
  requestLedUpdate(currentStep, stepEnabled);

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

  requestLedUpdate(currentStep, stepEnabled);
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
// Pico reset
// -----------------------------------------------------------------------------

void resetPicoPanel() {
  digitalWrite(PICO_RUN_PIN, LOW);
  delay(50);
  digitalWrite(PICO_RUN_PIN, HIGH);
  delay(500);
}

// -----------------------------------------------------------------------------
// Setup / loop
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(USB_BAUD);
  delay(500);

  pinMode(GATE_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  digitalWriteFast(GATE_PIN, LOW);
  digitalWriteFast(TRIG_PIN, LOW);

  pinMode(EDIT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PLAY_BUTTON_PIN, INPUT_PULLUP);

  pinMode(PICO_RUN_PIN, OUTPUT);
  digitalWrite(PICO_RUN_PIN, HIGH);

  pinMode(DAC_LEFT_CS, OUTPUT);
  pinMode(DAC_RIGHT_CS, OUTPUT);
  digitalWriteFast(DAC_LEFT_CS, HIGH);
  digitalWriteFast(DAC_RIGHT_CS, HIGH);

  pinMode(EXT_CLOCK_PIN, INPUT_PULLDOWN);
  pinMode(RESET_IN_PIN, INPUT_PULLDOWN);

  pinMode(GLOBAL_ENC_A_PIN, INPUT_PULLUP);
  pinMode(GLOBAL_ENC_B_PIN, INPUT_PULLUP);
  pinMode(GLOBAL_BUTTON_PIN, INPUT_PULLUP);

  SPI.begin();

  PicoSerial.begin(PICO_UART_BAUD);

  I2CBus.begin();
  I2CBus.setClock(400000);

  oledInit();
  mcpInit();

  globalLastAB = readGlobalAB();

  attachInterrupt(digitalPinToInterrupt(GLOBAL_ENC_A_PIN), globalEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(GLOBAL_ENC_B_PIN), globalEncoderISR, CHANGE);

  attachInterrupt(digitalPinToInterrupt(EXT_CLOCK_PIN), externalClockISR, RISING);
  attachInterrupt(digitalPinToInterrupt(RESET_IN_PIN), resetInputISR, RISING);

  setupMidiCallbacks();

  randomSeed(analogRead(A9));

  setDacOutput(0, noteToPitchDac(renderedPitchNote(0)));
  setDacOutput(1, percentToDac(stepVelocity[0]));
  setDacOutput(2, percentToDac(stepCV3[0]));
  setDacOutput(3, percentToDac(stepCV4[0]));

  requestLedUpdate(currentStep, stepEnabled);

  displayScreen = SCREEN_SPLASH;
  splashEndsAtMs = millis() + SPLASH_MS;
  requestDisplayRedraw();

  resetPicoPanel();

  if (DEBUG_SERIAL) {
    Serial.println("Lattice Sequencer ready.");
  }
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
  serviceGlobalEncoder();
  serviceGlobalButton();
  serviceLocalButtons();
  servicePicoSerialLimited();

  // Lowest priority: I2C feedback.
  serviceLedUpdate();
  serviceDisplay();
}
