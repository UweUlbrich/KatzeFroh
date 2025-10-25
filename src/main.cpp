#include <Arduino.h>

// Blink the onboard LED of the AZ-Delivery / Wemos D1 Mini ESP32
// On many ESP32 D1 mini boards the onboard LED is connected to GPIO 2.

const uint8_t LED_PIN = 2;      // change if your board uses a different pin
const uint8_t SWITCH_PIN = 32;   // change to the GPIO where your switch is connected
// Relay: active LOW pulse for a configurable time after N presses
// Hardware note: drive the relay with a driver transistor/MOSFET or use a relay module with separate JD-VCC
// and opto-isolation. Do NOT drive a relay coil directly from a GPIO pin. Use a flyback diode if you use
// a bare coil and ensure a common ground between driver and MCU.
const uint8_t RELAY_PIN = 22;   // relay pin set to GPIO22 (active LOW)

// Configuration
const unsigned long RELAY_PULSE_MS = 2000UL; // relay active time in ms (2s)
const uint8_t REQUIRED_PRESSES = 3; // how many rising edges trigger the relay

// Function prototypes
void setupPins();
bool readSwitchRisingEdge();
void startRelayPulse();
void updateRelayPulse();
void updateLed();

void setup() {
  Serial.begin(115200);
  delay(10);
  setupPins();
  // Relay pin (disabled for testing)
  // pinMode(RELAY_PIN, OUTPUT);
  // digitalWrite(RELAY_PIN, HIGH); // start with relay ON (HIGH)
  Serial.println("Example started");
}

// --- State used across functions ---
static int lastReading = LOW;
static int stableState = LOW;
static int pressCount = 0;
static unsigned long lastDebounceTime = 0;
static bool relayPulseActive = false;
static unsigned long relayPulseStart = 0;

// Setup pins
void setupPins() {
  pinMode(LED_PIN, OUTPUT);
  // Switch is wired to 3.3V when closed, use internal pull-down so the pin reads LOW when open
  pinMode(SWITCH_PIN, INPUT_PULLDOWN);
  // Relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // start with relay HIGH (inactive). Relay is driven LOW for activation.
}

// Read the switch with debounce and count rising edges. Returns true if a rising edge was detected.
bool readSwitchRisingEdge() {
  const unsigned long debounceDelay = 50; // ms
  int reading = digitalRead(SWITCH_PIN);

  if (reading != lastReading) {
    Serial.printf("Switch changed raw -> %d\n", reading);
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != stableState) {
      stableState = reading;
      Serial.printf("Switch stable -> %d\n", stableState);
      if (stableState == HIGH) {
        // rising edge
        lastReading = reading;
        return true;
      }
    }
  }

  lastReading = reading;
  return false;
}

// Start a relay pulse if not already active
void startRelayPulse() {
  if (!relayPulseActive) {
    Serial.println("3 presses reached -> activating relay (LOW for configured time)");
    digitalWrite(RELAY_PIN, LOW); // active LOW
    relayPulseActive = true;
    relayPulseStart = millis();
  } else {
    Serial.println("3 presses reached but relay pulse already active");
  }
}

// Check and update relay pulse (non-blocking)
void updateRelayPulse() {
  if (relayPulseActive) {
    if ((millis() - relayPulseStart) >= RELAY_PULSE_MS) {
      digitalWrite(RELAY_PIN, HIGH); // deactivate relay
      relayPulseActive = false;
      Serial.println("Relay pulse ended, relay set HIGH (inactive)");
    }
  }
}

// Update LED to reflect stable switch state
void updateLed() {
  digitalWrite(LED_PIN, stableState == HIGH ? HIGH : LOW);
}

void loop() {
  // Read switch and handle rising edge counter
  if (readSwitchRisingEdge()) {
    pressCount++;
    Serial.printf("Switch rising edge detected, count=%d\n", pressCount);
  }

  // If enough presses, start relay pulse and reset counter
  if (pressCount >= REQUIRED_PRESSES) {
    startRelayPulse();
    pressCount = 0;
  }

  // Update relay pulse state and LED
  updateRelayPulse();
  updateLed();
}