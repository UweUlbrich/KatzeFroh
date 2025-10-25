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
const bool RUN_SELF_TEST = false; // set true to run the audible relay self-test at boot

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
struct ScheduledTime { uint8_t hour; uint8_t minute; uint8_t steps; int lastTriggeredDay; };
ScheduledTime schedule[3] = {
  {8, 0, 3, -1},
  {16, 40, 3, -1},
  {18, 0, 3, -1}
};

// Scheduler state
static bool motorRunActive = false; // true when motor running for scheduled job
static int scheduledPressCount = 0; // counts switch activations during scheduled run
static int currentScheduleIndex = -1; // which schedule entry is running
static int currentScheduleSteps = 0; // steps required for current scheduled run
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
void setRelayActive();
void setRelayInactive();
void handleConfigRoot();
void handleConfigSave();
void loadScheduleFromPrefs();
void saveScheduleToPrefs();

// Function prototypes
void setupPins();
bool readSwitchRisingEdge();
void startRelayPulse();
void updateRelayPulse();
void updateLed();

// Global web server instance for config portal
WebServer server(80);
static bool configPortalRunning = false;

void setup() {
  // Initialize relay pin as early as possible to avoid accidental activation during boot
  pinMode(RELAY_PIN, OUTPUT);
  // Ensure relay is inactive at boot (HIGH=active, LOW=inactive after polarity flip)
  setRelayInactive();
  delay(20);
  Serial.begin(115200);
  delay(100);
  if (RUN_SELF_TEST) {
    Serial.println("Relay self-test: activating briefly (2 cycles)");
    setRelayActive();
    delay(2000);
    setRelayInactive();
    delay(2000);
    setRelayActive();
    delay(2000);
    setRelayInactive();
  }
  setupPins();
  connectToWiFi();
  initTime();
  // Load any saved schedule from Preferences before starting portal
  loadScheduleFromPrefs();
  // Start the configuration portal (non-blocking) so it's always reachable
  startConfigPortal();
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
  setRelayInactive(); // ensure relay inactive after setup
  Serial.println("Pins initialized");
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
  setRelayActive();
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
  setRelayInactive();
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
      if (scheduledPressCount >= currentScheduleSteps) {
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
  // Serve config portal requests (non-blocking)
  if (configPortalRunning) server.handleClient();
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

  // If we got here, no usable credentials or connection failed -> log and continue.
  // The configuration portal is started unconditionally from setup() and will be
  // reachable on the device IP (STA when connected, AP when not).
  Serial.println("No usable WiFi connection at this time (portal runs in background)");
}

// Start the config portal in a non-blocking way. The server will be started and
// the AP will be created if the ESP is not connected to WiFi. Handlers are
// registered here and `server.handleClient()` must be called from loop().
void startConfigPortal() {
  if (configPortalRunning) return;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, handleConfigRoot);
  server.on("/config/save", HTTP_POST, handleConfigSave);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });

  server.begin();
  configPortalRunning = true;

  if (WiFi.status() != WL_CONNECTED) {
    const char* apName = "KatzeFroh-Setup";
    Serial.printf("Starting AP '%s'\n", apName);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName);
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("AP IP: %s\n", apIP.toString().c_str());
    Serial.println("Config portal started on AP. Connect and open http://192.168.4.1/");
  } else {
    Serial.printf("Config portal started on STA IP: %s\n", WiFi.localIP().toString().c_str());
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

  // --- Motor / relay control implementations ---
  void startScheduledRun() {
    if (motorRunActive) {
      Serial.println("Scheduled run requested but motor already running");
      return;
    }
    Serial.println("Starting scheduled motor run: activating relay");
    setRelayActive();
    motorRunActive = true;
    scheduledPressCount = 0;
    if (currentScheduleIndex >= 0 && currentScheduleIndex < 3) {
      currentScheduleSteps = schedule[currentScheduleIndex].steps;
    } else {
      currentScheduleSteps = STEPS_PER_RUN;
    }
    scheduledRunStart = millis();
  }

  void stopMotor() {
    Serial.println("Stopping motor (relay inactive)");
    setRelayInactive();
    motorRunActive = false;
    scheduledPressCount = 0;
    relayPulseActive = false;
    scheduledRunStart = 0;
    currentScheduleIndex = -1;
    currentScheduleSteps = 0;
    lastMotorStop = millis();
  }

  void setRelayActive() {
    digitalWrite(RELAY_PIN, HIGH); // active HIGH for this hardware
    Serial.println("Relay set ACTIVE (HIGH)");
  }

  void setRelayInactive() {
    digitalWrite(RELAY_PIN, LOW); // inactive LOW
    Serial.println("Relay set INACTIVE (LOW)");
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
        currentScheduleIndex = i;
        startScheduledRun();
        schedule[i].lastTriggeredDay = today;
      } else {
        Serial.println("Scheduled time already triggered today");
      }
    }
  }

}

void handleConfigSave() {
  Preferences prefs;
  prefs.begin("schedule", false);
  for (int i = 0; i < 3; ++i) {
    String hs = server.arg("h" + String(i));
    String ms = server.arg("m" + String(i));
    String ss = server.arg("s" + String(i));
    int h = hs.length() ? hs.toInt() : schedule[i].hour;
    int m = ms.length() ? ms.toInt() : schedule[i].minute;
    int s = ss.length() ? ss.toInt() : schedule[i].steps;
    schedule[i].hour = h;
    schedule[i].minute = m;
    schedule[i].steps = s;
    prefs.putUInt((String("h") + i).c_str(), h);
    prefs.putUInt((String("m") + i).c_str(), m);
    prefs.putUInt((String("s") + i).c_str(), s);
  }
  prefs.end();
  server.send(200, "text/html", "Saved schedule. Reloading...<script>setTimeout(()=>location='/config',500);</script>");
}

void loadScheduleFromPrefs() {
  Preferences prefs;
  prefs.begin("schedule", true);
  for (int i = 0; i < 3; ++i) {
    unsigned int h = prefs.getUInt((String("h") + i).c_str(), schedule[i].hour);
    unsigned int m = prefs.getUInt((String("m") + i).c_str(), schedule[i].minute);
    unsigned int s = prefs.getUInt((String("s") + i).c_str(), schedule[i].steps);
    schedule[i].hour = h;
    schedule[i].minute = m;
    schedule[i].steps = s;
  }
  prefs.end();
}

void handleConfigRoot() {
  String page = "<html><body><h2>Feeding schedule</h2><form method='POST' action='/config/save'>";
  for (int i = 0; i < 3; ++i) {
    page += "Zeit " + String(i+1) + ": <input name='h" + String(i) + "' size=2 value='" + String(schedule[i].hour) + "'>:";
    page += "<input name='m" + String(i) + "' size=2 value='" + String(schedule[i].minute) + "'> ";
    page += "Schritte: <input name='s" + String(i) + "' size=2 value='" + String(schedule[i].steps) + "'><br>";
  }
  page += "<input type='submit' value='Save'></form></body></html>";
  server.send(200, "text/html", page);
}