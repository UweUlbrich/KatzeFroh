# KatzeFroh

Ein einfaches PlatformIO-Projekt für das AZ-Delivery / Wemos D1 Mini ESP32.

Dieses Repo enthält ein Beispielprogramm, das die Onboard-LED blinken lässt.

Schnellstart (PowerShell):

```powershell
# Build
platformio run -e d1_mini32

# Upload (Board angeschlossen)
platformio run --target upload -e d1_mini32

# Serieller Monitor
platformio device monitor -e d1_mini32 --baud 115200
```

Board-Umgebung: `d1_mini32` (WEMOS D1 MINI ESP32)

Wenn deine Onboard-LED auf einem anderen Pin ist, passe `src/main.cpp` an (LED_PIN).
