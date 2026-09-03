// =============================================
// Maze Circuit - ESP32-S3 Super Mini
// BLE HID Keyboard
//
// Circuit per input:
//   10M resistor  GPIO -> 3.3V
//   10nF cap     GPIO -> GND
//   Body closes   GPIO -> GND
//
// Measured on GPIO2:
//   untouched      ~4095  (3.30V)
//   partial touch  ~3850  (3.10V)
//   full contact   ~560   (0.45V)
//
// Requires:
//   ESP32 Arduino Core 3.x
//   NimBLE-Arduino >= 2.3.8
//   HijelHID_BLEKeyboard
//
// Remove the old ESP32-BLE-Keyboard (T-vK) folder first.
// =============================================

#include <HijelHID_BLEKeyboard.h>

HijelHID_BLEKeyboard keyboard("Maze Circuit - Zoe", "Lina Lopes", 100);

// -----------------------------
// Configuration
// -----------------------------

const int NUM_INPUTS = 5;
const int BUFFER_SIZE = 32;
const int ADC_SAMPLES = 16;

// Analog thresholds (12-bit, 0..4095) with hysteresis gap
int pressLevel   = 1800;   // ~1.45V - below this counts as touching
int releaseLevel = 2600;   // ~2.10V - above this counts as released

int pressThreshold = 24;   // samples out of BUFFER_SIZE to confirm press
int releaseThreshold = 14; // samples to confirm release

const unsigned long MAX_HOLD_TIME = 1800;
const unsigned long PRINT_INTERVAL = 150;

// -----------------------------
// Input mapping
// -----------------------------

// ADC1 pins on ESP32-S3 - no WiFi/USB conflict
const int inputPins[NUM_INPUTS] = {
  2, // SPACE  ADC1_CH1
  4, // LEFT   ADC1_CH2
  5, // RIGHT  ADC1_CH3
  6, // UP     ADC1_CH4
  7  // DOWN   ADC1_CH5
};

const char* inputNames[NUM_INPUTS] = {
  "SPACE", "LEFT", "RIGHT", "UP", "DOWN"
};

// HijelHID key constants from src/BLEHIDKeys.h
const uint8_t inputKeys[NUM_INPUTS] = {
  KEY_SPACE,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_UP,
  KEY_DOWN
};

// -----------------------------
// State
// -----------------------------

int buffers[NUM_INPUTS][BUFFER_SIZE];
int bufferIndex[NUM_INPUTS];
int touchSum[NUM_INPUTS];
int lastRaw[NUM_INPUTS];
bool aboveRelease[NUM_INPUTS];

bool isPressed[NUM_INPUTS];
unsigned long pressedAt[NUM_INPUTS];

bool keyboardEnabled = true;
bool wasReady = false;

unsigned long lastPrintTime = 0;

// -----------------------------
// Setup
// -----------------------------

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_BUILTIN, OUTPUT);

  analogReadResolution(12);

  for (int i = 0; i < NUM_INPUTS; i++) {
    pinMode(inputPins[i], INPUT);   // no internal pullup
    aboveRelease[i] = true;
  }

  clearAllFilters();

  keyboard.setLogLevel(HIDLogLevel::Normal);
  keyboard.begin();

  Serial.println();
  Serial.println("=== MAZE CIRCUIT - ESP32-S3 BLE ===");
  Serial.println("GPIO2=SPACE  GPIO3=LEFT  GPIO4=RIGHT  GPIO5=UP  GPIO6=DOWN");
  Serial.println();
  Serial.println("Pair from your computer: 'Maze Circuit'");
  Serial.println();
  Serial.println("k = keyboard on/off");
  Serial.println("r = reset/release all");
  Serial.println("+ = more sensitive   - = less sensitive");
  Serial.println("t = show thresholds");
  Serial.println("b = clear bonds (forces re-pairing)");
  Serial.println();
}

// -----------------------------
// Main loop
// -----------------------------

void loop() {
  handleSerialCommands();

  // isPaired() is the reliable ready signal - isConnected() goes true
  // before encryption finishes and the first report can be dropped
  bool ready = keyboard.isPaired();

  if (ready != wasReady) {
    wasReady = ready;
    Serial.println(ready ? ">>> PAIRED AND READY" : "<<< DISCONNECTED");
    if (!ready) clearAllFilters();
  }

  bool anyPressed = false;

  for (int i = 0; i < NUM_INPUTS; i++) {
    updateInput(i, ready);
    if (isPressed[i]) anyPressed = true;
  }

  if (!ready) {
    digitalWrite(LED_BUILTIN, (millis() / 500) % 2);  // blink = waiting
  } else {
    digitalWrite(LED_BUILTIN, anyPressed ? HIGH : LOW);
  }

  printDebug(ready);

  delay(3);
}

// -----------------------------
// ADC reading
// -----------------------------

int readAveraged(int pin) {
  long total = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    total += analogRead(pin);
  }
  return total / ADC_SAMPLES;
}

// -----------------------------
// Input logic
// -----------------------------

void updateInput(int i, bool ready) {
  int raw = readAveraged(inputPins[i]);
  lastRaw[i] = raw;

  // Hysteresis: dead zone between pressLevel and releaseLevel
  int touched;

  if (aboveRelease[i]) {
    touched = (raw < pressLevel) ? 1 : 0;
    if (touched) aboveRelease[i] = false;
  } else {
    touched = (raw > releaseLevel) ? 0 : 1;
    if (!touched) aboveRelease[i] = true;
  }

  updateFilter(i, touched);

  if (!isPressed[i] && touchSum[i] >= pressThreshold) {
    pressInput(i, ready);
  }

  if (isPressed[i] && touchSum[i] <= releaseThreshold) {
    releaseInput(i, "normal release", ready);
  }

  if (isPressed[i] && millis() - pressedAt[i] > MAX_HOLD_TIME) {
    releaseInput(i, "safety timeout", ready);
    clearFilter(i);
  }
}

void updateFilter(int i, int touched) {
  touchSum[i] -= buffers[i][bufferIndex[i]];
  buffers[i][bufferIndex[i]] = touched;
  touchSum[i] += touched;

  bufferIndex[i]++;
  if (bufferIndex[i] >= BUFFER_SIZE) bufferIndex[i] = 0;
}

void pressInput(int i, bool ready) {
  isPressed[i] = true;
  pressedAt[i] = millis();

  if (keyboardEnabled && ready) {
    keyboard.press(inputKeys[i]);
  }

  Serial.print(">>> ");
  Serial.print(inputNames[i]);
  Serial.println(" PRESSED");
}

void releaseInput(int i, String reason, bool ready) {
  if (keyboardEnabled && ready) {
    keyboard.release(inputKeys[i]);
  }

  isPressed[i] = false;

  Serial.print("<<< ");
  Serial.print(inputNames[i]);
  Serial.print(" RELEASED: ");
  Serial.println(reason);
}

// -----------------------------
// Filters / reset
// -----------------------------

void clearFilter(int i) {
  touchSum[i] = 0;
  bufferIndex[i] = 0;
  isPressed[i] = false;
  pressedAt[i] = 0;
  aboveRelease[i] = true;

  for (int j = 0; j < BUFFER_SIZE; j++) {
    buffers[i][j] = 0;
  }
}

void clearAllFilters() {
  for (int i = 0; i < NUM_INPUTS; i++) {
    clearFilter(i);
  }
}

// -----------------------------
// Serial commands
// -----------------------------

void handleSerialCommands() {
  if (Serial.available() > 0) {
    char c = Serial.read();

    if (c == 'k' || c == 'K') {
      keyboardEnabled = !keyboardEnabled;
      keyboard.releaseAll();
      clearAllFilters();
      digitalWrite(LED_BUILTIN, LOW);
      Serial.print("Keyboard: ");
      Serial.println(keyboardEnabled ? "ON" : "OFF");
    }

    if (c == 'r' || c == 'R') {
      keyboard.releaseAll();
      clearAllFilters();
      digitalWrite(LED_BUILTIN, LOW);
      Serial.println("RESET: released all keys, cleared all filters.");
    }

    // More sensitive: raise the level that still counts as a touch
    if (c == '+') {
      pressLevel += 100;
      if (pressLevel > releaseLevel - 400) pressLevel = releaseLevel - 400;
      Serial.print("pressLevel: ");
      Serial.println(pressLevel);
    }

    if (c == '-') {
      pressLevel -= 100;
      if (pressLevel < 200) pressLevel = 200;
      Serial.print("pressLevel: ");
      Serial.println(pressLevel);
    }

    if (c == 't' || c == 'T') {
      Serial.print("pressLevel=");
      Serial.print(pressLevel);
      Serial.print(" (");
      Serial.print(pressLevel * 3.3 / 4095.0, 2);
      Serial.print("V)  releaseLevel=");
      Serial.print(releaseLevel);
      Serial.print(" (");
      Serial.print(releaseLevel * 3.3 / 4095.0, 2);
      Serial.println("V)");
    }

    if (c == 'b' || c == 'B') {
      keyboard.clearBonds();
      Serial.println("Bonds cleared. Remove 'Maze Circuit' from your");
      Serial.println("computer's Bluetooth settings too, then re-pair.");
    }
  }
}

// -----------------------------
// Debug
// -----------------------------

void printDebug(bool ready) {
  if (millis() - lastPrintTime > PRINT_INTERVAL) {
    lastPrintTime = millis();

    for (int i = 0; i < NUM_INPUTS; i++) {
      Serial.print(inputNames[i]);
      Serial.print(":");
      Serial.print(lastRaw[i]);
      Serial.print(" ");
      Serial.print(touchSum[i]);
      Serial.print("/");
      Serial.print(BUFFER_SIZE);
      Serial.print(isPressed[i] ? " PRESS " : " ----- ");
    }

    Serial.print("| ble=");
    Serial.print(ready ? "READY" : "  ...");
    Serial.print(" kb=");
    Serial.println(keyboardEnabled ? "ON" : "OFF");
  }
}
