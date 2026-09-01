# firmware/master

Zentralsteuerung der Fallblattanzeige (**ESP32-C3 Super Mini**, PlatformIO,
Arduino-ESP32). Zielhardware: `hardware/master`, Pinbelegung
`docs/schaltplan-master.md` Kap. 6.

## Aufbau

| Teil | Inhalt | Bezug |
|---|---|---|
| `lib/charmap/` | Text → Fallblatt-Positionen, Umlaute, Ausrichtung, Kürzung | Spez. 7.4, Anhang A |
| `lib/clocktext/` | Uhrzeit → `HH.MM` / `HH.MM.SS` | Spez. 7.6 / 7.7 |
| `lib/busmaster/` | Master-Protokollseite: Frames, Enumeration, Modul-Statustabelle, Timeout/Retry | Spez. 4.5, 5 |
| `lib/masterapp/` | Betriebsarten, Anzeige-Update bei Änderung, Auto-Rückfall der Sekundenanzeige, Status-JSON | Spez. 7.3, 7.5, 7.7 |
| `lib/hadiscovery/` | Home-Assistant-MQTT-Auto-Discovery (Config-Topic + Payload je Entity) | Spez. 7.6 |
| `src/main.cpp` | ESP32-C3-Glue: UART1-RS485 (Halbduplex), WiFiManager, WebServer/REST, PubSubClient/MQTT, NTP, OTA, Status-LED |

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
pio run -e esp32c3               # ~936 KB Flash
pio run -e esp32c3 -t upload     # über die USB-C-Buchse des Moduls
```

Ein flash-fertiges Merged-Image für Webflasher (esptool-js / ESP Web Tools) liegt
committet unter `prebuilt/` und wird von `python tools/build_master_firmware.py`
erzeugt — bei jeder Firmware-Änderung neu ausführen.

Pin-/UART-Belegung steht in `platformio.ini` (`build_flags`), damit `main.cpp`
portabel bleibt; die Vorgaben in `main.cpp` sind dieselben Werte. RS-485 auf
**UART1** (der C3 hat nur UART0 = USB-Konsole und UART1). CHAIN läuft über den
nicht invertierenden Pegelwandler 74LVC1G17 (3,3 V → 5 V), bleibt also high-aktiv.
Die Status-LED (GPIO6, D1): Dauerlicht = alles gut, langsames Blinken = ein Modul
offline/Fehler, schnelles Blinken = kein WLAN.

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
