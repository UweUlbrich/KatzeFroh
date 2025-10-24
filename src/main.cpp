#include <Arduino.h>

// Blink the onboard LED of the AZ-Delivery / Wemos D1 Mini ESP32
// On many ESP32 D1 mini boards the onboard LED is connected to GPIO 2.

const uint8_t LED_PIN = 2;      // change if your board uses a different pin
const uint8_t SWITCH_PIN = 32;   // change to the GPIO where your switch is connected

void setup() {
  Serial.begin(115200);
  delay(10);
  pinMode(LED_PIN, OUTPUT);
  // Switch is wired to 3.3V when closed, use internal pull-down so the pin reads LOW when open
  pinMode(SWITCH_PIN, INPUT_PULLDOWN);
  Serial.println("Example started");
}

void loop() {
  // Read the switch state and mirror it to the LED
  int switchState = digitalRead(SWITCH_PIN);
  if (switchState == HIGH) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}