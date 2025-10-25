#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <WebServer.h>
#include <Preferences.h>

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
const bool ENABLE_MANUAL_TRIGGER = false; // if true, 3 presses will trigger a manual pulse

// Safety / timing
const unsigned long SCHEDULED_RUN_MAX_MS = 60UL * 1000UL; // max time for a scheduled run (failsafe)
const unsigned long MOTOR_STOP_COOLDOWN_MS = 3000UL; // don't restart motor in this many ms after stopping
static unsigned long scheduledRunStart = 0;
static unsigned long lastMotorStop = 0;

// WiFi / NTP (fill these)
const char* WIFI_SSID = "FRITZ6.3";
const char* WIFI_PASS = "Cool2:home::";
const long GMT_OFFSET_SEC = 7200; // adjust to your timezone (seconds)
const int DAYLIGHT_OFFSET_SEC = 0;

// Scheduled times (hour, minute)
struct ScheduledTime { uint8_t hour; uint8_t minute; int lastTriggeredDay; };
ScheduledTime schedule[3] = {
  {8, 0, -1},
  {15, 41, -1},
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
void startScheduledRun();
void stopMotor();
void startConfigPortal();
void handleRoot();
void handleSave();

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

// Start a manual relay pulse if not already active and not in a scheduled run
void startRelayPulse() {
  if (!ENABLE_MANUAL_TRIGGER) {
    Serial.println("Manual trigger disabled in configuration");
    return;
  }
  if (motorRunActive) {
    Serial.println("Manual pulse requested but scheduled run active - ignoring");
    return;
  }
  // respect cooldown after motor stop
  if ((millis() - lastMotorStop) < MOTOR_STOP_COOLDOWN_MS) {
    Serial.println("Manual trigger ignored due to motor stop cooldown");
    return;
  }
  if (!relayPulseActive) {
    Serial.println("3 presses reached -> activating manual relay pulse (LOW for configured time)");
    digitalWrite(RELAY_PIN, LOW); // active LOW
    relayPulseActive = true;
    relayPulseStart = millis();
  } else {
    Serial.println("3 presses reached but manual relay pulse already active");
  }
}

// Check and update relay pulse (non-blocking)
void updateRelayPulse() {
  // Only auto-manage manual pulses when not in a scheduled run
  if (relayPulseActive && !motorRunActive) {
    if ((millis() - relayPulseStart) >= RELAY_PULSE_MS) {
      digitalWrite(RELAY_PIN, HIGH); // deactivate relay
      relayPulseActive = false;
      Serial.println("Manual relay pulse ended, relay set HIGH (inactive)");
      lastMotorStop = millis();
    }
  }
  // Scheduled run timeout check
  if (motorRunActive && scheduledRunStart > 0) {
    if ((millis() - scheduledRunStart) >= SCHEDULED_RUN_MAX_MS) {
      Serial.println("Scheduled run timeout reached - stopping motor as failsafe");
      stopMotor();
      lastMotorStop = millis();
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
        stopMotor();
      }
    } else {
      if (ENABLE_MANUAL_TRIGGER) {
        pressCount++;
        Serial.printf("Switch rising edge detected, count=%d\n", pressCount);
      } else {
        Serial.println("Switch rising edge ignored (manual trigger disabled)");
      }
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
  // First try stored credentials from Preferences
  Preferences prefs;
  prefs.begin("wifi", true);
  String storedSsid = prefs.getString("ssid", "");
  String storedPass = prefs.getString("pass", "");
  prefs.end();

  if (storedSsid.length() > 0) {
    Serial.printf("Found stored credentials SSID=%s\n", storedSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(storedSsid.c_str(), storedPass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 10000) {
      delay(200);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected (stored credentials)");
      Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      return;
    } else {
      Serial.println("Stored credentials didn't connect");
    }
  }

  // Next try compile-time credentials if provided
  if (strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_SSID") != 0) {
    Serial.printf("Trying compile-time credentials SSID=%s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long start2 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start2) < 10000) {
      delay(200);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected (compile-time credentials)");
      Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      return;
    } else {
      Serial.println("Compile-time credentials didn't connect");
    }
  }

  // If we got here, no usable credentials or connection failed -> start config portal
  Serial.println("Starting AP config portal to set WiFi credentials");
  startConfigPortal();
}

// Global web server instance for config portal
WebServer server(80);

// Simple captive-style AP web portal to enter WiFi credentials
void startConfigPortal() {
  const char* apName = "KatzeFroh-Setup";
  Serial.printf("Starting AP '%s'\n", apName);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("AP IP: %s\n", apIP.toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });

  // Start server and handle requests until credentials saved
  server.begin();
  Serial.println("Config portal started. Connect to the AP and open http://192.168.4.1/");

  unsigned long portalStart = millis();
  bool saved = false;
  while (!saved && (millis() - portalStart) < 300000) { // 5 minutes
    server.handleClient();
    delay(10);
    // check a flag in Preferences to see if saved
    Preferences prefs;
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    prefs.end();
    if (ssid.length() > 0) saved = true;
  }

  server.stop();
  Serial.println("Config portal stopped");
  // attempt to reconnect if saved
  Preferences prefs2;
  prefs2.begin("wifi", true);
  String newSsid = prefs2.getString("ssid", "");
  String newPass = prefs2.getString("pass", "");
  prefs2.end();
  if (newSsid.length() > 0) {
    Serial.printf("Trying new credentials SSID=%s\n", newSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(newSsid.c_str(), newPass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 10000) {
      delay(200);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected (new credentials)");
      Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println("New credentials failed to connect");
    }
  }
}

// Handlers use Preferences to save credentials.
void handleRoot() {
  String page = "<html><body><h2>KatzeFroh WiFi Setup</h2>";
  page += "<form method='POST' action='/save'>";
  page += "SSID: <input name='ssid' length=32><br>";
  page += "Password: <input name='pass' length=64><br>";
  page += "<input type='submit' value='Save'>";
  page += "</form></body></html>";
  server.send(200, "text/html", page);
}

void handleSave() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "ssid missing");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  server.send(200, "text/html", "Saved. The device will try to connect. You can close this page.");
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
        startScheduledRun();
        schedule[i].lastTriggeredDay = today;
      } else {
        Serial.println("Scheduled time already triggered today");
      }
    }
  }
}

void startScheduledRun() {
  if (motorRunActive) {
    Serial.println("Scheduled run requested but motor already running");
    return;
  }
  Serial.println("Starting scheduled motor run: activating relay");
  digitalWrite(RELAY_PIN, LOW); // active LOW keeps motor running
  motorRunActive = true;
  scheduledPressCount = 0;
  scheduledRunStart = millis();
}

void stopMotor() {
  Serial.println("Stopping motor (relay HIGH)");
  digitalWrite(RELAY_PIN, HIGH);
  motorRunActive = false;
  scheduledPressCount = 0;
  relayPulseActive = false;
  scheduledRunStart = 0;
  lastMotorStop = millis();
}