#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>

// forward declare server (defined later) so handlers above can use it
extern WebServer server;

// Logging helpers
String getCurrentTimestamp() {
  // Placeholder: return a formatted timestamp. If time is available via time(), format it.
  time_t now = time(nullptr);
  struct tm t;
  if (localtime_r(&now, &t)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
  }
  // Fallback dummy timestamp
  return String("1970-01-01 00:00:00");
}

void logMessage(String level, String message) {
  // Rotate if needed before writing
  const size_t MAX_LOG_SIZE = 64 * 1024; // 64 KB
  if (SPIFFS.exists("/log.txt")) {
    File fchk = SPIFFS.open("/log.txt", FILE_READ);
    if (fchk) {
      size_t sz = fchk.size();
      fchk.close();
      if (sz >= MAX_LOG_SIZE) {
        // rotate: /log.3.txt <- /log.2.txt <- /log.1.txt <- /log.txt
        for (int i = 3; i >= 1; --i) {
          String src = i == 1 ? "/log.txt" : String("/log.") + (i-1) + ".txt";
          String dst = String("/log.") + i + ".txt";
          if (SPIFFS.exists(dst)) SPIFFS.remove(dst);
          if (SPIFFS.exists(src)) SPIFFS.rename(src.c_str(), dst.c_str());
        }
      }
    }
  }
  String ts = getCurrentTimestamp();
  String line = "[" + ts + "] [" + level + "] " + message + "\n";
  // Write to serial
  Serial.print(line);
  // Append to SPIFFS file
  File f = SPIFFS.open("/log.txt", FILE_APPEND);
  if (!f) {
    Serial.println("ERROR: failed to open log file for appending");
    return;
  }
  size_t written = f.print(line);
  if (written == 0) {
    Serial.println("ERROR: failed to write to log file");
  }
  f.close();
}

// Serve log file at /log
void handleLogDownload() {
  if (!SPIFFS.exists("/log.txt")) {
    server.send(404, "text/plain", "Log not found");
    return;
  }
  File f = SPIFFS.open("/log.txt", FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Failed to open log");
    return;
  }
  server.streamFile(f, "text/plain");
  f.close();
}

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
void handleWifiRoot();
void handleWifiSave();
void setRelayActive();
void setRelayInactive();
void handleConfigRoot();
void handleConfigSave();
void loadScheduleFromPrefs();
void saveScheduleToPrefs();
String buildPage(const String &title, const String &body);

// Function prototypes
void setupPins();
bool readSwitchRisingEdge();
void startRelayPulse();
void updateRelayPulse();
void updateLed();

// Global web server instance for config portal
WebServer server(80);
static bool configPortalRunning = false;
static bool mdnsStarted = false;

void setup() {
  // Initialize relay pin as early as possible to avoid accidental activation during boot
  pinMode(RELAY_PIN, OUTPUT);
  // Ensure relay is inactive at boot (HIGH=active, LOW=inactive after polarity flip)
  setRelayInactive();
  delay(20);
  Serial.begin(115200);
  delay(200);
  // Initialize SPIFFS so we can log to file
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  } else {
    // SPIFFS ready; will log this below using logMessage
  }
  
  if (RUN_SELF_TEST) {
    logMessage("INFO", "Relay self-test: activating briefly (2 cycles)");
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
  logMessage("INFO", "Example started");
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
  logMessage("INFO", "Pins initialized");
}

// Read the switch with debounce and count rising edges. Returns true if a rising edge was detected.
bool readSwitchRisingEdge() {
  const unsigned long debounceDelay = 50; // ms
  int reading = digitalRead(SWITCH_PIN);
  // char buf[64];

  if (reading != lastReading) {
    // char buf[32];
    // snprintf(buf, sizeof(buf), "Switch changed raw -> %d", reading);
    // logMessage("DEBUG", String(buf));
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != stableState) {
      stableState = reading;
      // snprintf(buf, sizeof(buf), "Switch stable -> %d", stableState);
      // logMessage("DEBUG", String(buf));
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
    logMessage("INFO", "Manual trigger disabled in configuration");
    return;
  }
  if (motorRunActive) {
    logMessage("INFO", "Manual pulse requested but scheduled run active - ignoring");
    return;
  }
  // respect cooldown after motor stop
  if ((millis() - lastMotorStop) < MOTOR_STOP_COOLDOWN_MS) {
    Serial.println("Manual trigger ignored due to motor stop cooldown");
    return;
  }
  if (!relayPulseActive) {
    logMessage("INFO", "3 presses reached -> activating manual relay pulse (LOW for configured time)");
  setRelayActive();
    relayPulseActive = true;
    relayPulseStart = millis();
  } else {
    logMessage("INFO", "3 presses reached but manual relay pulse already active");
  }
}

// Check and update relay pulse (non-blocking)
void updateRelayPulse() {
  // Only auto-manage manual pulses when not in a scheduled run
  if (relayPulseActive && !motorRunActive) {
    if ((millis() - relayPulseStart) >= RELAY_PULSE_MS) {
  setRelayInactive();
      relayPulseActive = false;
      logMessage("INFO", "Manual relay pulse ended, relay set HIGH (inactive)");
      lastMotorStop = millis();
    }
  }
  // Scheduled run timeout check
  if (motorRunActive && scheduledRunStart > 0) {
    if ((millis() - scheduledRunStart) >= SCHEDULED_RUN_MAX_MS) {
      logMessage("WARN", "Scheduled run timeout reached - stopping motor as failsafe");
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
  char buf[64];
  // Periodically check schedule at a resolution of 1 minute
  checkSchedule();

  // Read switch and handle rising edge counter
  if (readSwitchRisingEdge()) {
    // If a scheduled motor run is active, count towards scheduledPressCount
    if (motorRunActive) {
      scheduledPressCount++;
  snprintf(buf, sizeof(buf), "Scheduled run: switch rising edge, scheduled count=%d", scheduledPressCount);
  logMessage("DEBUG", String(buf));
      // When enough presses during a scheduled run are detected, stop motor
      if (scheduledPressCount >= currentScheduleSteps) {
        logMessage("INFO", "Scheduled run completed: stopping motor/relay");
        // Ensure relay is deactivated
        stopMotor();
      }
    } else {
      if (ENABLE_MANUAL_TRIGGER) {
        pressCount++;
  snprintf(buf, sizeof(buf), "Switch rising edge detected, count=%d", pressCount);
  logMessage("DEBUG", String(buf));
      } else {
        logMessage("DEBUG", "Switch rising edge ignored (manual trigger disabled)");
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
    logMessage("DEBUG", String("Found stored credentials SSID=") + storedSsid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(storedSsid.c_str(), storedPass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 10000) {
      delay(200);
    // progress dot while connecting
    logMessage("DEBUG", ".");
    }
  logMessage("DEBUG", "");
    if (WiFi.status() == WL_CONNECTED) {
      logMessage("INFO", "WiFi connected (stored credentials)");
      logMessage("INFO", String("IP: ") + WiFi.localIP().toString());
      // start mDNS responder
      if (!mdnsStarted) {
        if (MDNS.begin("katzefroh")) {
          logMessage("INFO", "mDNS responder started: http://katzefroh.local");
          mdnsStarted = true;
        } else {
          logMessage("WARN", "mDNS responder failed to start");
        }
      }
      return;
    } else {
      logMessage("WARN", "Stored credentials didn't connect");
    }
  }

  // Next try compile-time credentials if provided
  if (strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_SSID") != 0) {
  logMessage("DEBUG", String("Trying compile-time credentials SSID=") + WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long start2 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start2) < 10000) {
      delay(200);
    logMessage("DEBUG", ".");
    }
  logMessage("DEBUG", "");
    if (WiFi.status() == WL_CONNECTED) {
      logMessage("INFO", "WiFi connected (compile-time credentials)");
      logMessage("INFO", String("IP: ") + WiFi.localIP().toString());
      // start mDNS responder
      if (!mdnsStarted) {
        if (MDNS.begin("katzefroh")) {
          logMessage("INFO", "mDNS responder started: http://katzefroh.local");
          mdnsStarted = true;
        } else {
          logMessage("WARN", "mDNS responder failed to start");
        }
      }
      return;
    } else {
      logMessage("WARN", "Compile-time credentials didn't connect");
    }
  }

  // If we got here, no usable credentials or connection failed -> log and continue.
  // The configuration portal is started unconditionally from setup() and will be
  // reachable on the device IP (STA when connected, AP when not).
  logMessage("WARN", "No usable WiFi connection at this time (portal runs in background)");
}

// Start the config portal in a non-blocking way. The server will be started and
// the AP will be created if the ESP is not connected to WiFi. Handlers are
// registered here and `server.handleClient()` must be called from loop().
void startConfigPortal() {
  if (configPortalRunning) return;

  server.on("/", HTTP_GET, handleRoot);
  // Root should show the schedule page; keep /config as alias
  server.on("/", HTTP_GET, handleConfigRoot);
  server.on("/config", HTTP_GET, handleConfigRoot);
  server.on("/config/save", HTTP_POST, handleConfigSave);
  // WiFi setup moved to /wifi
  server.on("/wifi", HTTP_GET, handleWifiRoot);
  server.on("/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/log", HTTP_GET, handleLogDownload);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });

  server.begin();
  configPortalRunning = true;

  if (WiFi.status() != WL_CONNECTED) {
    const char* apName = "KatzeFroh-Setup";
  logMessage("INFO", String("Starting AP '") + apName + "'");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName);
    IPAddress apIP = WiFi.softAPIP();
  logMessage("INFO", String("AP IP: ") + apIP.toString());
  logMessage("INFO", "Config portal started on AP. Connect and open http://192.168.4.1/");
    // start mDNS responder on AP IP as well if possible
    if (!mdnsStarted) {
      if (MDNS.begin("katzefroh")) {
        logMessage("INFO", "mDNS responder started on AP: http://katzefroh.local");
        mdnsStarted = true;
      } else {
        logMessage("WARN", "mDNS responder failed to start on AP");
      }
    }
  } else {
  logMessage("INFO", String("Config portal started on STA IP: ") + WiFi.localIP().toString());
  }
}

// Handlers use Preferences to save credentials.
void handleWifiRoot() {
  String body = "<form method='POST' action='/wifi/save'>";
  body += "SSID: <input name='ssid' length=32><br>";
  body += "Password: <input name='pass' length=64><br>";
  body += "<input type='submit' value='Save'>";
  body += "</form><p><a href='/'>Home</a></p>";
  String page = buildPage("WLAN konfigurieren", body);
  server.send(200, "text/html", page);
}

  // --- Motor / relay control implementations ---
  void startScheduledRun() {
    if (motorRunActive) {
      logMessage("WARN", "Scheduled run requested but motor already running");
      return;
    }
    logMessage("INFO", "Starting scheduled motor run: activating relay");
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
    logMessage("INFO", "Stopping motor (relay inactive)");
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
    logMessage("INFO", "Relay set ACTIVE (HIGH)");
  }

  void setRelayInactive() {
    digitalWrite(RELAY_PIN, LOW); // inactive LOW
    logMessage("INFO", "Relay set INACTIVE (LOW)");
  }

void handleWifiSave() {
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
    char tbuf[64];
    snprintf(tbuf, sizeof(tbuf), "Current time: %02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    logMessage("INFO", String(tbuf));
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
  char schedBuf[64];
  snprintf(schedBuf, sizeof(schedBuf), "Scheduled time reached: %02d:%02d -> starting motor run", curHour, curMinute);
  logMessage("INFO", String(schedBuf));
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
    String t = server.arg("t" + String(i)); // expected HH:MM
    String ss = server.arg("s" + String(i));
    int h = schedule[i].hour;
    int m = schedule[i].minute;
    int s = ss.length() ? ss.toInt() : schedule[i].steps;
    if (t.length() >= 4) {
      int colon = t.indexOf(':');
      if (colon > 0) {
        h = t.substring(0, colon).toInt();
        m = t.substring(colon + 1).toInt();
      }
    }
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
  String body = "<form method='POST' action='/config/save'>";
  for (int i = 0; i < 3; ++i) {
    char defaultTime[6];
    snprintf(defaultTime, sizeof(defaultTime), "%02d:%02d", schedule[i].hour, schedule[i].minute);
    body += "<label>Zeit " + String(i+1) + "</label>";
    body += "<input type='time' name='t" + String(i) + "' value='" + String(defaultTime) + "' required>";
    body += "<label>Portionen</label>";
    body += "<input type='number' name='s" + String(i) + "' min='1' max='20' value='" + String(schedule[i].steps) + "' required><br><br>";
  }
  body += "<div style='margin-top:12px'><button type='submit'>Speichern</button></div>";
  body += "</form>";
  body += "<p><a href='/'>Home</a> · <a href='/wifi'>WLAN</a> · <a href='/log'>Log</a></p>";
  String page = buildPage("KatzeFroh - Zeitplan", body);
  server.send(200, "text/html", page);
}

// Root page: links to schedule config and WiFi setup
void handleRoot() {
  String body = "<ul>";
  body += "<li><a href='/config'>Zeitplan konfigurieren</a></li>";
  body += "<li><a href='/wifi'>WLAN konfigurieren</a></li>";
  body += "<li><a href='/log'>LOG ansehen</a></li>";
  body += "</ul>";
  String page = buildPage("KatzeFroh - Home", body);
  server.send(200, "text/html", page);
}

// Small HTML helper to wrap pages with CSS
String buildPage(const String &title, const String &body) {
  String css = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  css += "<title>" + title + "</title>";
  css += "<style>body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial;margin:0;background:#f6f8fa;color:#111} ";
  css += ".container{max-width:760px;margin:24px auto;background:#fff;padding:18px;border-radius:8px;box-shadow:0 6px 20px rgba(0,0,0,0.06)}";
  css += "h2{margin-top:0}label{display:block;margin:8px 0 4px;font-weight:600}input[type=time],input[type=number],input[type=text],select{width:100%;padding:8px;border:1px solid #ddd;border-radius:6px;box-sizing:border-box} .row{display:flex;gap:8px} .row> *{flex:1} button{background:#1976d2;color:#fff;padding:10px 14px;border:none;border-radius:6px;cursor:pointer} .muted{color:#666;font-size:0.9em} a{color:#1976d2}</style>";
  css += "</head><body><div class='container'>";
  css += "<h2>" + title + "</h2>";
  css += body;
  css += "</div></body></html>";
  return css;
}