# KatzeFroh

Ein einfaches PlatformIO-Projekt für das AZ-Delivery / Wemos D1 Mini ESP32.

Dieses Repo enthält ein kleines Beispiel, das die Onboard-LED über einen externen Schalter steuert.

## Pin-Belegung (aktuell im Code)

- LED_PIN: GPIO2  (Onboard-LED)
- SWITCH_PIN: GPIO32 (Schalter verbindet GPIO32 mit 3.3V wenn geschlossen)

- RELAY_PIN: GPIO22 (aktiv LOW, wird für 2s gezogen, wenn 3 Schalter‑Betätigungen erkannt werden)

## Verdrahtung

- Schalter: einen Pin des Schalters an GPIO32, den anderen Pin an 3.3V.
- Kein direkten Anschluss an EN/BOOT oder Flash-Pins! Verwende GPIO32 wie oben.
- Der Code verwendet internen Pull‑Down (pinMode(SWITCH_PIN, INPUT_PULLDOWN)), daher liest der Pin LOW wenn der Schalter offen ist.

Relais / Shield:

- Verwende ein Relais‑Shield oder ein Treibermodul für den D1 mini. Treibe keinen Relais‑Spulendraht direkt vom GPIO.
- Dein Shield scheint die passende Elektronik zu haben (Treiber / Opto‑Isolator / separate JD‑VCC). Achte trotzdem auf gemeinsame Masse zwischen MCU und Shield, falls benötigt.
- Falls du ein nacktes Relais mit Transistor benutzt: Flyback‑Diode über die Spule einbauen (z. B. 1N4007) und die Freilaufdiode korrekt polen.
- Im Code ist das Relais "aktiv LOW": der Pin wird für 2000 ms auf LOW gezogen, danach wieder HIGH gesetzt.

## Schnellstart (PowerShell)

```powershell
# Build
platformio run -e d1_mini32

# Upload (Board angeschlossen)
platformio run --target upload -e d1_mini32

# Serieller Monitor
platformio device monitor -e d1_mini32 --baud 115200
```

Relais testen:

1. Lade die Firmware hoch.
2. Öffne den seriellen Monitor (115200).
3. Betätige den Schalter dreimal (mit kurzem Abstand; Debounce 50 ms), in der Konsole sollte erscheinen: "3 presses reached -> activating relay (LOW for 2s)".
4. Das Relais wird für 2 Sekunden anziehen (LOW), danach wird "Relay pulse ended, relay set HIGH (inactive)" geloggt.

Board-Umgebung: `d1_mini32` (WEMOS D1 MINI ESP32)

## Troubleshooting

- Wenn beim Schließen des Schalters das Board neu startet oder Boot‑Fehler wie `invalid header: 0xffffffff` auftreten, liegt das meist an einem speziellen Boot‑/Flash‑Pin oder an einer Falschverdrahtung. In diesem Fall: trenne den Schalter und prüfe, ob das Board normal bootet. Verwende einen anderen GPIO (z. B. 32) für den Schalter.
- Falls dein Schalter gegen GND arbeitet statt gegen 3.3V, passe `pinMode(SWITCH_PIN, INPUT_PULLUP)` an und invertiere die Logik (LOW = gedrückt).

## Anpassungen

- Wenn deine Onboard-LED auf einem anderen Pin ist, passe `src/main.cpp` an (`LED_PIN`).

## Lizenz

MIT
