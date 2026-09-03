#include <Keyboard.h>

// -----------------------------
// Configuration
// -----------------------------

const int NUM_INPUTS = 5;
const int BUFFER_SIZE = 32;

int pressThreshold = 24;
int releaseThreshold = 14;

const unsigned long MAX_HOLD_TIME = 1800;
const unsigned long PRINT_INTERVAL = 150;

// -----------------------------
// Input mapping
// -----------------------------

const int inputPins[NUM_INPUTS] = {
  2, // SPACE
  3, // LEFT
  4, // RIGHT
  5, // UP
  6  // DOWN
};

const char* inputNames[NUM_INPUTS] = {
  "SPACE",
  "LEFT",
  "RIGHT",
  "UP",
  "DOWN"
};

const uint8_t inputKeys[NUM_INPUTS] = {
  ' ',
  KEY_LEFT_ARROW,
  KEY_RIGHT_ARROW,
  KEY_UP_ARROW,
  KEY_DOWN_ARROW
};

// -----------------------------
// State
// -----------------------------

int buffers[NUM_INPUTS][BUFFER_SIZE];
int bufferIndex[NUM_INPUTS];
int touchSum[NUM_INPUTS];

bool isPressed[NUM_INPUTS];
unsigned long pressedAt[NUM_INPUTS];

bool keyboardEnabled = true; // Starts ON by default

unsigned long lastPrintTime = 0;

// -----------------------------
// Setup
// -----------------------------

void setup() {
  Serial.begin(115200);

  // Safety window before keyboard starts
  delay(5000);

  Keyboard.begin();
  Keyboard.releaseAll();

  pinMode(LED_BUILTIN, OUTPUT);

  for (int i = 0; i < NUM_INPUTS; i++) {
    pinMode(inputPins[i], INPUT); // External high-value pull-up to 5V
  }

  clearAllFilters();

  Serial.println();
  Serial.println("=== Makey Makey-style 5 INPUT KEYBOARD ===");
  Serial.println("D2 = SPACE");
  Serial.println("D3 = LEFT");
  Serial.println("D4 = RIGHT");
  Serial.println("D5 = UP");
  Serial.println("D6 = DOWN");
  Serial.println();
  Serial.println("Keyboard starts ON by default.");
  Serial.println("Commands:");
  Serial.println("k = keyboard on/off");
  Serial.println("r = reset/release all");
  Serial.println("+ = increase sensitivity");
  Serial.println("- = decrease sensitivity");
  Serial.println();
}

// -----------------------------
// Main loop
// -----------------------------

void loop() {
  handleSerialCommands();

  bool anyPressed = false;

  for (int i = 0; i < NUM_INPUTS; i++) {
    updateInput(i);

    if (isPressed[i]) {
      anyPressed = true;
    }
  }

  digitalWrite(LED_BUILTIN, anyPressed ? HIGH : LOW);

  printDebug();

  delay(3);
}

// -----------------------------
// Input logic
// -----------------------------

void updateInput(int i) {
  int raw = digitalRead(inputPins[i]);

  // Expected:
  // HIGH = untouched
  // LOW  = touched through body/object to GND
  int touched = (raw == LOW) ? 1 : 0;

  updateFilter(i, touched);

  if (!isPressed[i] && touchSum[i] >= pressThreshold) {
    pressInput(i);
  }

  if (isPressed[i] && touchSum[i] <= releaseThreshold) {
    releaseInput(i, "normal release");
  }

  if (isPressed[i] && millis() - pressedAt[i] > MAX_HOLD_TIME) {
    releaseInput(i, "safety timeout");
    clearFilter(i);
  }
}

void updateFilter(int i, int touched) {
  touchSum[i] -= buffers[i][bufferIndex[i]];
  buffers[i][bufferIndex[i]] = touched;
  touchSum[i] += touched;

  bufferIndex[i]++;

  if (bufferIndex[i] >= BUFFER_SIZE) {
    bufferIndex[i] = 0;
  }
}

void pressInput(int i) {
  isPressed[i] = true;
  pressedAt[i] = millis();

  if (keyboardEnabled) {
    Keyboard.press(inputKeys[i]);
  }

  Serial.print(">>> ");
  Serial.print(inputNames[i]);
  Serial.println(" PRESSED");
}

void releaseInput(int i, String reason) {
  if (keyboardEnabled) {
    Keyboard.release(inputKeys[i]);
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

      Keyboard.releaseAll();
      clearAllFilters();
      digitalWrite(LED_BUILTIN, LOW);

      Serial.print("Keyboard: ");
      Serial.println(keyboardEnabled ? "ON" : "OFF");
    }

    if (c == 'r' || c == 'R') {
      Keyboard.releaseAll();
      clearAllFilters();
      digitalWrite(LED_BUILTIN, LOW);
      Serial.println("RESET: released all keys and cleared all filters.");
    }

    // More sensitive: easier to press
    if (c == '+') {
      pressThreshold -= 2;
      if (pressThreshold < 8) pressThreshold = 8;

      Serial.print("pressThreshold: ");
      Serial.println(pressThreshold);
    }

    // Less sensitive: harder to press
    if (c == '-') {
      pressThreshold += 2;
      if (pressThreshold > BUFFER_SIZE) pressThreshold = BUFFER_SIZE;

      Serial.print("pressThreshold: ");
      Serial.println(pressThreshold);
    }
  }
}

// -----------------------------
// Debug
// -----------------------------

void printDebug() {
  if (millis() - lastPrintTime > PRINT_INTERVAL) {
    lastPrintTime = millis();

    for (int i = 0; i < NUM_INPUTS; i++) {
      int raw = digitalRead(inputPins[i]);

      Serial.print(inputNames[i]);
      Serial.print(": raw=");
      Serial.print(raw == HIGH ? "HIGH" : "LOW");

      Serial.print(" sum=");
      Serial.print(touchSum[i]);
      Serial.print("/");
      Serial.print(BUFFER_SIZE);

      Serial.print(" state=");
      Serial.print(isPressed[i] ? "PRESSED" : "released");

      Serial.print("  ");
    }

    Serial.print("| keyboard=");
    Serial.print(keyboardEnabled ? "ON" : "OFF");

    Serial.print(" | pressTh=");
    Serial.print(pressThreshold);

    Serial.print(" releaseTh=");
    Serial.println(releaseThreshold);
  }
}