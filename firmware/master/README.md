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
| `lib/moduleupdate/` | Sende-Seite der Firmware-Verteilung über den Bus (Warteschlange + Zustandsautomat, host-getestet) | Spez. 5.7 |
| `src/main.cpp` | ESP32-C3-Glue: UART1-RS485 (Halbduplex), WiFiManager, WebServer/REST, PubSubClient/MQTT, NTP, OTA, mDNS, Status-LED, Web-UI |

`lib/protocol/` wird über `lib_extra_dirs = ../module/lib` mit der Modul-Firmware
geteilt. Die fünf `lib/`-Bausteine sind hardwareunabhängig und auf dem Host getestet.

## Tests (Host)

```bash
source ../../.venv/bin/activate
pio test -e native
```

`test_charmap`, `test_clocktext`, `test_busmaster`, `test_masterapp`,
`test_hadiscovery`, `test_eventlog`, `test_otaverify`, `test_moduleupdate` (56 Fälle). `test_masterapp` und
`test_busmaster` treiben die Logik gegen einen **simulierten Bus** (aufgezeichnete
Sende-Frames, eingespeiste Antworten) — das deckt „REST-Endpunkte antworten gegen
einen simulierten Bus" aus Backlog T8 ab.

## Firmware bauen und aufspielen

```bash
pio run  -e esp32c3                     # ~1 MB Flash
pio run  -e esp32c3 -t upload           # USB-C
python tools/ota_keys.py init           # einmalig: OTA-Signaturschluessel
python tools/build_master_firmware.py   # -> prebuilt/ (factory.bin + signierte .kota)
```

**Erst-Flash (USB):** Browser-Flasher **<https://tenofnine.github.io/SmartKroneSplitFlap/>**
(`prebuilt/index.html`, per GitHub Actions veröffentlicht — lädt immer den zuletzt
committeten Stand), esptool-js oder `esptool.py`. Datei: `prebuilt/krone-master-esp32c3.factory.bin`.

**Spätere Updates (OTA):** In der Web-UI unter *Einstellungen › Firmware
aktualisieren* den **signierten Container** `prebuilt/krone-master-esp32c3.kota`
hochladen (nicht die `.factory.bin`). Das Modul prüft ECDSA-P-256-Signatur und
SHA-256, schreibt dann in die zweite App-Partition und startet neu; fremde oder
beschädigte Dateien werden abgelehnt, die laufende Firmware bleibt aktiv. Kein
Toolchain, jeder Browser. In den *Schnittstellen* abschaltbar; ein Netzwerk-OTA
à la ArduinoOTA/espota gibt es bewusst nicht (offener Port ohne Passwort).
Schlüsselverwaltung: `docs/firmware-signing.md`.

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
feste IP, NTP-Server + Zeitzone als Städteliste mit Sommerzeit-Schalter + Uhr
manuell, MQTT, Anzeige, Schnittstellen-Schalter, Zugriffsschutz, System,
Firmware aktualisieren). Der Quelltext ist `INDEX_HTML` in `src/main.cpp`.

## REST

| Methode | Pfad | Body / Zweck |
|---|---|---|
| GET | `/api/status` | Anzeige + Module (Ist/Ziel/Zustand/Fehler/Korr./Blattzahl/FW/verpasst) |
| GET | `/api/system` | Uptime, Heap (frei/gesamt/min), **CPU-Last, Chiptemperatur**, Sketch/OTA-Platz, Hostname, WLAN, Uhr, MQTT/OTA/mDNS, Bus-CRC/Timeouts, FW-Build |
| GET | `/api/log` | `?sev=info\|warn\|err` — Ereignis-Ringpuffer |
| GET/POST | `/api/backup` | Vollsicherung **inkl. WLAN-Zugangsdaten** (Download / Restore). POST übernimmt und startet neu. |
| POST | `/api/update` | Signiertes OTA: Container `.kota` als multipart (Feld `firmware`) → Signatur-/Hash-Prüfung → `Update`-Bibliothek → Neustart |
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
| GET/POST | `/api/config` | Hostname, MQTT, NTP-Server, TZ, feste IP, Ausrichtung, Trennzeichen, Modulzahl, hh:mm:ss-Timeout, `net_scope`, `admin_user`/`admin_pass` (nur schreibend), Schalter MQTT/REST-Schreib-API/OTA/mDNS |
| GET | `/api/module/firmware` | gebündelte Modul-Version + je Modul installierte Version/Status (experimentell, Spez. 5.7) |
| POST | `/api/module/update` | `{"all":true}` oder `{"addr":[2,3]}` — Firmware über den Bus verteilen |
| GET | `/api/module/update/status` | Fortschritt/Ergebnis der laufenden Verteilung |

Die schreibenden Steuer-Endpunkte lassen sich über den Schalter
**REST-Schreib-API** sperren (`403`). Die Web-Oberfläche selbst nicht.

## Zugriffsschutz

Ein Wrapper prüft vor **jedem** Endpunkt (`web_begin()` → `guard()`):

- **Herkunft** — `cfg.net_scope`: `0` alle · `1` private Netze (RFC 1918) +
  eigenes Subnetz *(Vorgabe)* · `2` nur eigenes Subnetz. Sonst `403`. Weil ein
  Router-Port-Forward die echte öffentliche Absender-IP durchreicht, greift ein
  Internetzugriff bei `1`/`2` nicht. SoftAP-Portal + `127.0.0.1` immer erlaubt.
- **Anmeldung** — ist `cfg.admin_pass` gesetzt: HTTP-Basic-Auth auf allen
  Endpunkten (`401` sonst), Benutzer `cfg.admin_user`. Leer = keine Anmeldung.

Beides unter *Einstellungen › Zugriffsschutz*. `admin_pass` wird nie ausgeliefert
(`/api/config` GET zeigt nur `admin_set`), steht aber in der `/api/backup`-Sicherung.

## MQTT / Home Assistant

Broker, Port, Benutzer/Passwort und Basis-Topic (Default `krone/anzeige`) unter
*Einstellungen › MQTT*. Beim Verbinden sendet die Karte die
Auto-Discovery-Configs unter `homeassistant/…` (retained) — in HA erscheint ein
Gerät „KRONE Fallblattanzeige" mit Text, Betriebsart, Homing/Selbsttest,
Sammelfehler und je Modul Zeichen + Online-Status.

- **Verfügbarkeit:** `<base>/status` = `online`/`offline` (Last Will, retained);
  jede Entity trägt es als `availability_topic` und wird bei Ausfall „nicht
  verfügbar".
- **Zustände** werden retained gesendet (`text/state`, `mode/state`,
  `module/<n>/char` = dargestelltes Zeichen, `module/<n>/online`, `error/state`),
  Aktualisierung ~1×/s.
- Broker-Reconnect alle 5 s (im Log sichtbar). Client-ID = Hostname → bei mehreren
  Mastern eindeutig halten.
- Kein TLS — nur im vertrauenswürdigen Netz betreiben.
- Broker-ACL (falls gesetzt): pub+sub auf `krone/anzeige/#`, pub auf
  `homeassistant/#`.

Details und die Topic-Tabelle: `docs/spezifikation.md` 7.6.

## Persistenz

Alle Einstellungen (inkl. WLAN-, MQTT- und Admin-Zugangsdaten) liegen im NVS und
**überstehen OTA-Updates**. Nur ein USB-Flash mit „Erase" löscht sie — dann in
den Einstellungen unter *System* die zuvor gesicherte `krone-backup.json` wieder
einspielen. Deshalb setzt das Flasher-Manifest `new_install_prompt_erase: false`.

## Abweichungen von Spezifikation 7.2

Eingebauter `WebServer` statt `ESPAsyncWebServer`, `Preferences` (NVS) statt
`LittleFS` — dependency-arm und mit arduino-esp32 3.x ohne Patches lauffähig.
Begründung in `docs/toolchain.md` Abschnitt 4. **Kein `ArduinoOTA`** — Updates
laufen über `POST /api/update` (Browser) bzw. USB; das spart einen offenen,
passwortlosen OTA-Port.
