# KatzeFroh

Ein einfaches PlatformIO-Projekt für das AZ-Delivery / Wemos D1 Mini ESP32.

Dieses Repo enthält ein kleines Beispiel, das die Onboard-LED über einen externen Schalter steuert.

## Pin-Belegung (aktuell im Code)

- LED_PIN: GPIO2  (Onboard-LED)
- SWITCH_PIN: GPIO32 (Schalter verbindet GPIO32 mit 3.3V wenn geschlossen)

## Verdrahtung

- Schalter: einen Pin des Schalters an GPIO32, den anderen Pin an 3.3V.
- Kein direkten Anschluss an EN/BOOT oder Flash-Pins! Verwende GPIO32 wie oben.
- Der Code verwendet internen Pull‑Down (pinMode(SWITCH_PIN, INPUT_PULLDOWN)), daher liest der Pin LOW wenn der Schalter offen ist.

## Schnellstart (PowerShell)

```powershell
# Build
platformio run -e d1_mini32

# Upload (Board angeschlossen)
platformio run --target upload -e d1_mini32

# Serieller Monitor
platformio device monitor -e d1_mini32 --baud 115200
```

Board-Umgebung: `d1_mini32` (WEMOS D1 MINI ESP32)

## Troubleshooting

- Wenn beim Schließen des Schalters das Board neu startet oder Boot‑Fehler wie `invalid header: 0xffffffff` auftreten, liegt das meist an einem speziellen Boot‑/Flash‑Pin oder an einer Falschverdrahtung. In diesem Fall: trenne den Schalter und prüfe, ob das Board normal bootet. Verwende einen anderen GPIO (z. B. 32) für den Schalter.
- Falls dein Schalter gegen GND arbeitet statt gegen 3.3V, passe `pinMode(SWITCH_PIN, INPUT_PULLUP)` an und invertiere die Logik (LOW = gedrückt).

## Anpassungen

- Wenn deine Onboard-LED auf einem anderen Pin ist, passe `src/main.cpp` an (`LED_PIN`).

## Lizenz

MIT
