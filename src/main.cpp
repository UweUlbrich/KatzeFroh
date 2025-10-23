#include <Arduino.h>
#include <Arduino.h>

// Blink the onboard LED of the AZ-Delivery / Wemos D1 Mini ESP32
// On many ESP32 D1 mini boards the onboard LED is connected to GPIO 2.

const uint8_t LED_PIN = 2;      // change if your board uses a different pin
const uint8_t RELAY_PIN = 22;   // relay pin set to GPIO22
const unsigned long INTERVAL = 1000; // milliseconds

unsigned long previousMillis = 0;
bool ledState = LOW;

void setup() {
  Serial.begin(115200);
  delay(10);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(LED_PIN, ledState);
  Serial.println("Blink example started");
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= INTERVAL) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    digitalWrite(RELAY_PIN, ledState); // also toggle the relay pin
    Serial.printf("LED is now %s\n", ledState ? "ON" : "OFF");
  }
}