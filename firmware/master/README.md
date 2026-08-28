# firmware/master

Zentralsteuerung der Fallblattanzeige (ESP32, PlatformIO, Arduino-ESP32).

## Aufbau

| Teil | Inhalt | Bezug |
|---|---|---|
| `lib/charmap/` | Text → Fallblatt-Positionen, Umlaute, Ausrichtung, Kürzung | Spez. 7.4, Anhang A |
| `lib/clocktext/` | Uhrzeit → `HH.MM` / `HH.MM.SS` | Spez. 7.6 / 7.7 |
| `lib/busmaster/` | Master-Protokollseite: Frames, Enumeration, Modul-Statustabelle, Timeout/Retry | Spez. 4.5, 5 |
| `lib/masterapp/` | Betriebsarten, Anzeige-Update bei Änderung, Auto-Rückfall der Sekundenanzeige, Status-JSON | Spez. 7.3, 7.5, 7.7 |
| `lib/hadiscovery/` | Home-Assistant-MQTT-Auto-Discovery (Config-Topic + Payload je Entity) | Spez. 7.6 |
| `src/main.cpp` | ESP32-Glue: UART2-RS485, WiFiManager, WebServer/REST, PubSubClient/MQTT, NTP, OTA |

`lib/protocol/` wird über `lib_extra_dirs = ../module/lib` mit der Modul-Firmware
geteilt. Die fünf `lib/`-Bausteine sind hardwareunabhängig und auf dem Host getestet.

## Tests (Host)

```bash
source ../../.venv/bin/activate
pio test -e native
```

`test_charmap`, `test_clocktext`, `test_busmaster`, `test_masterapp`,
`test_hadiscovery`. `test_masterapp` und `test_busmaster` treiben die Logik gegen
einen **simulierten Bus** (aufgezeichnete Sende-Frames, eingespeiste Antworten)
— das deckt „REST-Endpunkte antworten gegen einen simulierten Bus" aus Backlog T8 ab.

## Firmware bauen

```bash
pio run -e esp32                  # ~950 KB Flash
pio run -e esp32 -t upload
```

Beim Erststart öffnet die Karte einen Access-Point (`krone_anzeige`) mit Captive
Portal für die WLAN-Zugangsdaten. MQTT-Broker und Modulzahl danach unter
`/api/config` bzw. in der Web-UI.

## REST

| Methode | Pfad | Body |
|---|---|---|
| GET | `/api/status` | — |
| POST | `/api/text` | `{"text":"HALLO"}` |
| POST | `/api/mode` | `{"mode":"clock_hm","sep":"."}` |
| POST | `/api/home` | `{}` oder `{"addr":3}` |
| POST | `/api/selftest` | `{}` |
| GET/POST | `/api/config` | `{"mqtt_host":"...","mqtt_port":1883,"modules":10,"hms_timeout_s":600}` |

## Abweichungen von Spezifikation 7.2

Eingebauter `WebServer` statt `ESPAsyncWebServer`, `Preferences` (NVS) statt
`LittleFS` — dependency-arm und mit arduino-esp32 3.x ohne Patches lauffähig.
Begründung in `docs/toolchain.md` Abschnitt 4.
