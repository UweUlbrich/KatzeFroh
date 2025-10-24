#include <Arduino.h>

// Blink the onboard LED of the AZ-Delivery / Wemos D1 Mini ESP32
// On many ESP32 D1 mini boards the onboard LED is connected to GPIO 2.

const uint8_t LED_PIN = 2;      // change if your board uses a different pin
const uint8_t SWITCH_PIN = 32;   // change to the GPIO where your switch is connected
// const uint8_t RELAY_PIN = 22;   // relay pin set to GPIO22

void setup() {
  Serial.begin(115200);
  delay(10);
  pinMode(LED_PIN, OUTPUT);
  // Switch is wired to 3.3V when closed, use internal pull-down so the pin reads LOW when open
  pinMode(SWITCH_PIN, INPUT_PULLDOWN);
  // Relay pin (disabled for testing)
  // pinMode(RELAY_PIN, OUTPUT);
  // digitalWrite(RELAY_PIN, HIGH); // start with relay ON (HIGH)
  Serial.println("Example started");
}

void loop() {
  static int lastReading = LOW;
  static int stableState = LOW;
  static int pressCount = 0;
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 50; // ms

  int reading = digitalRead(SWITCH_PIN);

  // Debug raw changes
  if (reading != lastReading) {
    Serial.printf("Switch changed raw -> %d\n", reading);
    lastDebounceTime = millis();
  }

  // If the reading has been stable for longer than debounceDelay, take it as the actual state
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != stableState) {
      stableState = reading;
      Serial.printf("Switch stable -> %d\n", stableState);
      if (stableState == HIGH) {
        pressCount++;
        Serial.printf("Switch rising edge detected, count=%d\n", pressCount);
      }
    }
  }

  lastReading = reading;

  // If we reached 3 presses, report and reset counter (relay disabled for test)
  if (pressCount >= 3) {
    Serial.println("3 presses reached (relay disabled for test)");
    pressCount = 0;
  }

  // Show stable switch state on LED
  digitalWrite(LED_PIN, stableState == HIGH ? HIGH : LOW);
}