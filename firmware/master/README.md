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
| `lib/eventlog/` | Ereignis-Ringpuffer (32 Einträge) für den Log-Tab der Web-UI | — |
| `src/main.cpp` | ESP32-C3-Glue: UART1-RS485 (Halbduplex), WiFiManager, WebServer/REST, PubSubClient/MQTT, NTP, OTA, mDNS, Status-LED, Web-UI |

`lib/protocol/` wird über `lib_extra_dirs = ../module/lib` mit der Modul-Firmware
geteilt. Die fünf `lib/`-Bausteine sind hardwareunabhängig und auf dem Host getestet.

## Tests (Host)

```bash
source ../../.venv/bin/activate
pio test -e native
```

`test_charmap`, `test_clocktext`, `test_busmaster`, `test_masterapp`,
`test_hadiscovery`, `test_eventlog` (40 Fälle). `test_masterapp` und
`test_busmaster` treiben die Logik gegen einen **simulierten Bus** (aufgezeichnete
Sende-Frames, eingespeiste Antworten) — das deckt „REST-Endpunkte antworten gegen
einen simulierten Bus" aus Backlog T8 ab.

## Firmware bauen

```bash
pio run -e esp32c3               # ~936 KB Flash
pio run -e esp32c3 -t upload     # über die USB-C-Buchse des Moduls
```

Ein flash-fertiges Merged-Image für Webflasher liegt committet unter `prebuilt/`
und wird von `python tools/build_master_firmware.py` erzeugt — bei jeder
Firmware-Änderung neu ausführen. Browser-Flasher:
**<https://tenofnine.github.io/SmartKroneSplitFlap/>** (`prebuilt/index.html`,
per GitHub Actions veröffentlicht — lädt immer den zuletzt committeten Stand).

Pin-/UART-Belegung steht in `platformio.ini` (`build_flags`), damit `main.cpp`
portabel bleibt; die Vorgaben in `main.cpp` sind dieselben Werte. RS-485 auf
**UART1** (der C3 hat nur UART0 = USB-Konsole und UART1). CHAIN läuft über den
nicht invertierenden Pegelwandler 74LVC1G17 (3,3 V → 5 V), bleibt also high-aktiv.
Die Status-LED (GPIO6, D1): Dauerlicht = alles gut, langsames Blinken = ein Modul
offline/Fehler, schnelles Blinken = kein WLAN.

Beim Erststart öffnet die Karte einen Access-Point (`krone_anzeige`) mit Captive
Portal für die WLAN-Zugangsdaten. MQTT-Broker und Modulzahl danach unter
`/api/config` bzw. in der Web-UI.

## Web-UI

Eine vom ESP32-C3 ausgelieferte Seite (System-Schriften, kein CDN, ~14 KB),
Dark-Theme, Ansichten **Übersicht** (Split-Flap-Statusstreifen, Kacheln,
Schnellaktionen), **Module** (Tabelle), **Log**, **Einstellungen** (WLAN wechseln,
feste IP, NTP-Server/Zeitzone/Uhr manuell, MQTT, Anzeige, Schnittstellen-Schalter,
System). Der Quelltext ist `INDEX_HTML` in `src/main.cpp`.

## REST

| Methode | Pfad | Body / Zweck |
|---|---|---|
| GET | `/api/status` | Anzeige + Module (Ist/Ziel/Zustand/Fehler/Korr./Blattzahl/FW/verpasst) |
| GET | `/api/system` | Uptime, Heap (frei/gesamt/min), **CPU-Last, Chiptemperatur**, Sketch/OTA-Platz, Hostname, WLAN, Uhr, MQTT/OTA/mDNS, Bus-CRC/Timeouts, FW-Build |
| GET | `/api/log` | `?sev=info\|warn\|err` — Ereignis-Ringpuffer |
| GET/POST | `/api/backup` | Vollsicherung **inkl. WLAN-Zugangsdaten** (Download / Restore). POST übernimmt und startet neu. |
| POST | `/api/log/clear` | Log leeren |
| POST | `/api/text` | `{"text":"HALLO"}` |
| POST | `/api/mode` | `{"mode":"clock_hm","sep":".","align":1}` |
| POST | `/api/home` | `{}` oder `{"addr":3}` |
| POST | `/api/selftest` | `{}` |
| POST | `/api/module` | `{"addr":3,"action":"home\|stop\|identify"}` |
| POST | `/api/enumerate` | Enumeration neu starten |
| POST | `/api/time` | `{"iso":"2026-09-01T14:07:00"}` — Uhr manuell stellen |
| GET | `/api/wifi/scan` | erreichbare Netze |
| POST | `/api/wifi` | `{"ssid":"…","psk":"…"}` — Netz wechseln (Rückfall nach ~25 s) |
| POST | `/api/wifi/portal` | Konfigurationsportal öffnen |
| POST | `/api/reboot` | Neustart |
| GET/POST | `/api/config` | Hostname, MQTT, NTP-Server, TZ, feste IP, Ausrichtung, Trennzeichen, Modulzahl, hh:mm:ss-Timeout, Schalter MQTT/REST-Schreib-API/OTA/mDNS |

Die schreibenden Steuer-Endpunkte lassen sich über den Schalter
**REST-Schreib-API** sperren (`403`). Die Web-Oberfläche selbst nicht.

## Persistenz

Alle Einstellungen (inkl. WLAN- und MQTT-Zugangsdaten) liegen im NVS und
**überstehen OTA-Updates**. Nur ein USB-Flash mit „Erase" löscht sie — dann in
den Einstellungen unter *System* die zuvor gesicherte `krone-backup.json` wieder
einspielen. Deshalb setzt das Flasher-Manifest `new_install_prompt_erase: false`.

## Abweichungen von Spezifikation 7.2

Eingebauter `WebServer` statt `ESPAsyncWebServer`, `Preferences` (NVS) statt
`LittleFS` — dependency-arm und mit arduino-esp32 3.x ohne Patches lauffähig.
Begründung in `docs/toolchain.md` Abschnitt 4.
