#include <Arduino.h>

// Blink the onboard LED of the AZ-Delivery / Wemos D1 Mini ESP32
// On many ESP32 D1 mini boards the onboard LED is connected to GPIO 2.

const uint8_t LED_PIN = 2;      // change if your board uses a different pin
const uint8_t SWITCH_PIN = 32;   // change to the GPIO where your switch is connected
// Relay: active LOW pulse for 2s after 3 presses
// Hardware note: drive the relay with a driver transistor/MOSFET or use a relay module with separate JD-VCC
// and opto-isolation. Do NOT drive a relay coil directly from a GPIO pin. Use a flyback diode if you use
// a bare coil and ensure a common ground between driver and MCU.
const uint8_t RELAY_PIN = 22;   // relay pin set to GPIO22 (active LOW)

void setup() {
  Serial.begin(115200);
  delay(10);
  pinMode(LED_PIN, OUTPUT);
  // Switch is wired to 3.3V when closed, use internal pull-down so the pin reads LOW when open
  pinMode(SWITCH_PIN, INPUT_PULLDOWN);
  // Relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // start with relay HIGH (inactive). Relay is driven LOW for activation.
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
  // Relay non-blocking pulse state
  static bool relayPulseActive = false;
  static unsigned long relayPulseStart = 0;
  const unsigned long relayPulseMs = 2000UL; // 2 seconds
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
    // Start relay pulse (non-blocking). If a pulse is already active, just reset the counter.
    if (!relayPulseActive) {
      Serial.println("3 presses reached -> activating relay (LOW for 2s)");
      digitalWrite(RELAY_PIN, LOW); // active LOW
      relayPulseActive = true;
      relayPulseStart = millis();
    } else {
      Serial.println("3 presses reached but relay pulse already active");
    }
    pressCount = 0;
  }

  // Check relay pulse timeout (non-blocking)
  if (relayPulseActive) {
    if ((millis() - relayPulseStart) >= relayPulseMs) {
      digitalWrite(RELAY_PIN, HIGH); // deactivate relay
      relayPulseActive = false;
      Serial.println("Relay pulse ended, relay set HIGH (inactive)");
    }
  }

  // Show stable switch state on LED
  digitalWrite(LED_PIN, stableState == HIGH ? HIGH : LOW);
}