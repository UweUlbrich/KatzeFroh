#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

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
const unsigned long RELAY_PULSE_MS = 5000UL; // relay active time in ms (2s)
const uint8_t REQUIRED_PRESSES = 3; // how many rising edges trigger the relay
const uint8_t STEPS_PER_RUN = 3; // how many switch activations per scheduled motor run

// WiFi / NTP (fill these)
const char* WIFI_SSID = "FRITZ6.3";
const char* WIFI_PASS = "Cool2:home::";
const long GMT_OFFSET_SEC = 7200; // adjust to your timezone (seconds)
const int DAYLIGHT_OFFSET_SEC = 0;

// Scheduled times (hour, minute)
struct ScheduledTime { uint8_t hour; uint8_t minute; int lastTriggeredDay; };
ScheduledTime schedule[3] = {
  {8, 0, -1},
  {13, 0, -1},
  {18, 0, -1}
};

// Scheduler state
static bool motorRunActive = false; // true when motor running for scheduled job
static int scheduledPressCount = 0; // counts switch activations during scheduled run
static int lastCheckedMinute = -1;

// Function prototypes (extended)
void connectToWiFi();
void initTime();
void checkSchedule();

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
  connectToWiFi();
  initTime();
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
  // Periodically check schedule at a resolution of 1 minute
  checkSchedule();

  // Read switch and handle rising edge counter
  if (readSwitchRisingEdge()) {
    // If a scheduled motor run is active, count towards scheduledPressCount
    if (motorRunActive) {
      scheduledPressCount++;
      Serial.printf("Scheduled run: switch rising edge, scheduled count=%d\n", scheduledPressCount);
      // When enough presses during a scheduled run are detected, stop motor
      if (scheduledPressCount >= STEPS_PER_RUN) {
        Serial.println("Scheduled run completed: stopping motor/relay");
        // Ensure relay is deactivated
        digitalWrite(RELAY_PIN, HIGH);
        motorRunActive = false;
        scheduledPressCount = 0;
      }
    } else {
      pressCount++;
      Serial.printf("Switch rising edge detected, count=%d\n", pressCount);
    }
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

// Connect to WiFi (non-blocking wait)
void connectToWiFi() {
  if (strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "YOUR_SSID") == 0) {
    Serial.println("WiFi SSID not configured. Please set WIFI_SSID and WIFI_PASS in src/main.cpp");
    return;
  }
  Serial.printf("Connecting to WiFi SSID=%s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 10000) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi not connected (continuing without time sync)");
    // Do a quick scan and print nearby SSIDs to help diagnose
    int n = WiFi.scanNetworks();
    Serial.printf("Found %d networks:\n", n);
    for (int i = 0; i < n; ++i) {
      Serial.printf("  %d: %s (RSSI=%d) %s\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "SECURE");
    }
    WiFi.scanDelete();
  }
}

// Initialize time via SNTP (if WiFi is connected)
void initTime() {
  if (WiFi.status() != WL_CONNECTED) return;
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for time sync...");
  time_t now = time(nullptr);
  unsigned long start = millis();
  while (now < 8 * 3600 * 2 && (millis() - start) < 10000) {
    delay(200);
    now = time(nullptr);
  }
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    Serial.printf("Current time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }
}

// Check the schedule once per minute and start motor run when scheduled time is reached
void checkSchedule() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (!localtime_r(&now, &timeinfo)) return;
  int curMinute = timeinfo.tm_min;
  int curHour = timeinfo.tm_hour;

  // Only check when minute changed to avoid repeated triggers within the same minute
  if (curMinute == lastCheckedMinute) return;
  lastCheckedMinute = curMinute;

  for (int i = 0; i < 3; ++i) {
    if (schedule[i].hour == curHour && schedule[i].minute == curMinute) {
      int today = timeinfo.tm_yday; // day of year
      if (schedule[i].lastTriggeredDay != today) {
        Serial.printf("Scheduled time reached: %02d:%02d -> starting motor run\n", curHour, curMinute);
        // Start motor: activate relay
        digitalWrite(RELAY_PIN, LOW); // active LOW
        motorRunActive = true;
        scheduledPressCount = 0;
        schedule[i].lastTriggeredDay = today;
      } else {
        Serial.println("Scheduled time already triggered today");
      }
    }
  }
}