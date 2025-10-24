WIRING
======

Kurze Verdrahtungsanleitung für das Projekt "KatzeFroh" (Wemos D1 Mini ESP32)

Ziel: Schalter steuert Onboard-LED (LED an bei geschlossenem Schalter gegen 3.3V)

Benutzte Pins (Code):
- LED_PIN: GPIO2
- SWITCH_PIN: GPIO32

ASCII-Skizze (Seitenansicht des Boards, vereinfachte Darstellung):

    [3.3V] -----+        +---- [GPIO32 SWITCH]
                |        |
               (o) switch |
                |        |
    [GND] ------------- [GND]

Anleitung:
1. Verbinde einen Pin des Schalters mit 3.3V.
2. Verbinde den anderen Pin des Schalters mit GPIO32 auf dem Board.
3. Lass GND des Boards mit der Schaltung verbunden (normalerweise bereits verbunden).
4. Nicht an EN, IO0 oder die Pins 6..11 (Flash SPI) anschließen.

Test:
1. Build und Upload mit PlatformIO:

   platformio run --target upload -e d1_mini32

2. Seriellen Monitor öffnen:

   platformio device monitor -e d1_mini32 --baud 115200

3. Schließe den Schalter; die LED sollte an sein. Öffne den Schalter; die LED sollte aus.

Hinweis:
- Falls dein Schalter gegen GND arbeitet, ändere in `src/main.cpp` die Zeile:

    pinMode(SWITCH_PIN, INPUT_PULLDOWN);

  zu

    pinMode(SWITCH_PIN, INPUT_PULLUP);

  und invertiere die Logik (LOW = gedrückt).
