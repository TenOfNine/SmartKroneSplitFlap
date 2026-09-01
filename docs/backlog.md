# Backlog

Die Reihenfolge ist bindend, weil spätere Aufgaben auf früheren aufbauen.

---

## T1 — Repository initialisieren

Die Verzeichnisstruktur und die Dokumente sind bereits vorhanden. Zu tun bleibt: `git init`, prüfen dass `reference/` nicht erfasst wird, Initial-Commit.

**Fertig, wenn:** `git log` einen Initial-Commit zeigt und `git status --ignored` bestätigt, dass `reference/` ignoriert wird.

---

## T2 — Toolchain einrichten

`tools/setup.sh` schreiben, das KiCad, die Python-Umgebung und PlatformIO installiert und die Versionen verifiziert. Details in `docs/toolchain.md`.

Dabei klären, ob `kicad-sch-api` mit der installierten KiCad-Version arbeitet. Die Dokumentation der Bibliothek nennt KiCad 7 und 8; ob KiCad 9 unterstützt wird, ist zu verifizieren. Rückfallebene ist `kicad-skip`.

**Fertig, wenn:** Das Skript auf der frischen VM durchläuft und `kicad-cli version` sowie `pio --version` eine Ausgabe liefern.

---

## T3 — Symbolprüfung ATtiny1616 und TP8485E

Prüfen, ob die KiCad-Bibliothek ein Symbol für den ATtiny1616 im SOIC-20 enthält. Die Pinbelegung des Symbols gegen das Datenblatt gegenprüfen und als Tabelle vorlegen. Fehlt das Symbol, ein eigenes anlegen.

Dasselbe für den TP8485E-SR. Für den existiert vermutlich kein Symbol; er ist pinkompatibel zum MAX485, dessen Symbol sich verwenden lässt.

> Dieser Schritt endet mit einer **menschlichen Bestätigung**. ERC läuft auch bei vertauschtem VDD und GND fehlerfrei durch. Die Prüfung ist nicht automatisierbar.

**Fertig, wenn:** Eine Tabelle Symbol-Pin gegen Datenblatt-Pin vorliegt und bestätigt wurde.

---

## T4 — Schaltplan generieren

Aus der Netzliste in `docs/schaltplan-daughtercard.md` (Kapitel 6) mit `kicad-sch-api` eine `.kicad_sch` erzeugen. ERC laufen lassen und bis zur Fehlerfreiheit iterieren. PDF exportieren nach `docs/`.

**Fertig, wenn:** `kicad-cli sch erc` ohne Fehler durchläuft, das PDF im Repository liegt und die Datei sich in der KiCad-GUI öffnen lässt.

---

## T5 — Footprints und PCB-Netzliste

Footprints zuordnen: 0805 für Passive, SOIC-20 im 300-mil-Gehäuse, SOIC-8, SOT-23, Wannenstecker 2×5 im 2,54-mm-Raster, Schraubklemme 5,08 mm.

Netzliste für den PCB-Editor exportieren. Platzierungsvorschlag als Text erstellen, unter Beachtung von Kapitel 8 des Schaltplandokuments — insbesondere die AC-Führung entlang einer Platinenkante mit mindestens 2 mm Abstand zu allen Logiknetzen und 1,0 mm Leiterbahnbreite.

**Fertig, wenn:** Die Netzliste importierbar ist und jedes Bauteil einen Footprint hat.

---

## T6 — Protokollbibliothek mit Host-Tests

Hardwareunabhängige C-Bibliothek unter `firmware/module/lib/`:

- Rahmenaufbau und -zerlegung nach Abschnitt 5.3 der Spezifikation
- CRC16/MODBUS
- Kommandotabelle nach 5.4
- Zustandsautomat der Enumeration einschließlich Rückfallverhalten nach 4.5.2

**Fertig, wenn:** `pio test -e native` durchläuft und Tests für Kollisionserkennung, Rückfall auf die EEPROM-Adresse und Rückfall auf die Serviceadresse 250 enthält.

---

## T7 — Modul-Firmware ATtiny1616

Zustandsautomat nach Kapitel 6 der Spezifikation. USART im RS-485-Modus über XDIR. Impulsauswertung auf **fallende** Flanke mit 20 ms Sperrzeit. Automatische Blattzahlerkennung. Abschaltvorhalt, Blatt-Offset und Triac-Polarität als EEPROM-Parameter. Watchdog und Laufzeitüberwachung.

**Fertig, wenn:** `pio run` fehlerfrei kompiliert und der Flash-Verbrauch unter 8 KB liegt.

---

## T8 — Master-Firmware ESP32

WLAN-Einrichtung über Captive Portal, Web-UI, REST nach Abschnitt 7.5, MQTT mit Home-Assistant-Auto-Discovery nach 7.6, NTP-Uhr mit Zeitzone `CET-1CEST,M3.5.0,M10.5.0/3`, Zeichenabbildung nach 7.4, Betriebsarten einschließlich des Auto-Timeouts der Sekundenanzeige.

**Fertig, wenn:** `pio run` fehlerfrei kompiliert und die REST-Endpunkte gegen einen simulierten Bus antworten.

---

## T9 — Bus-Testwerkzeug

`tools/busctl.py` für einen USB-RS485-Adapter: Enumeration auslösen, Status abfragen, Zielblatt setzen, Rohrahmen mitschneiden. Das ist das Werkzeug für die Inbetriebnahme nach Kapitel 10 der Spezifikation.

**Fertig, wenn:** Das Werkzeug gegen die Modul-Firmware in einer Schleife (TX auf RX) plausible Rahmen erzeugt und zerlegt.

---

## T10 — CI

GitHub Action: ERC, beide Firmware-Builds, Host-Tests. Bei einem Tag zusätzlich PDF und Gerber exportieren und als Release-Artefakt anhängen.

**Fertig, wenn:** Der Workflow auf einem Push grün durchläuft.

---

## T11 — Master-Hardware (Zentralsteuerung)

Trägerboard für das steckbare **ESP32-C3-Super-Mini**-Modul, das alle
Logikspannungen und Bussignale außer der 42 V~ erzeugt (Spez. 7.1). Aufbau
analog zur Daughter Card: Projektbibliothek + Generatoren + FreeRouting.

- `docs/schaltplan-master.md` als verbindliche Quelle der Netzliste.
- `tools/build_krone_master_symbols.py`, `tools/gen_master_sch.py`,
  `tools/gen_master_pcb.py`, `tools/route_master.py`,
  `tools/gen_master_manufacturing.py`.
- Symbolprüfung `docs/symbolpruefung-master.md` (Regel 5): ESP32-C3-Modul
  (M-1), 74LVC1G17 (geprüft), TP8485E (Verweis).
- Offene Punkte: M-1 (Modul-Pinbelegung/Einbaulage), M-2 (Aufwärtswandler DNP
  bis O-2), M-3 (CHAIN 3,3 V → 5 V: 74LVC1G17 oder 0-Ω-Brücke).

**Fertig, wenn:** `gen_master_sch.py --check-only` sauber, ERC 0/0, DRC 0 Fehler,
jedes Bauteil hat einen Footprint, das Fertigungspaket liegt in
`hardware/master/manufacturing/`. **Erledigt 01.09.2026** — Symbolprüfung vom
Betreiber freigegeben (M-1). Offen: Sichtkontrolle der U1-Einbaulage am Render,
Bench-Test M-3 vor der Bestellung.

---

## T12 — Master-Firmware auf ESP32-C3 portieren

Die Master-Firmware lief zunächst gegen `esp32dev` (WROOM-32, UART2, GPIO 16/17/5/4).
Portierung auf den ESP32-C3 Super Mini:

- `platformio.ini`: `board = esp32-c3-devkitm-1`, `env` `esp32` → `esp32c3`.
- `src/main.cpp`: RS-485 auf **UART1** (der C3 hat nur zwei UARTs, UART0 =
  USB-Konsole), GPIO-Konstanten nach `docs/schaltplan-master.md` Kap. 6
  (TX GPIO3, RX GPIO4, DE GPIO10, CHAIN GPIO5, LED GPIO6), Pins über `build_flags`.
- CHAIN-Polarität: bei bestücktem 74LVC1G17 nicht invertiert.

**Fertig, wenn:** `pio run -e esp32c3` fehlerfrei kompiliert und die
Host-Tests (`pio test -e native`) unverändert grün bleiben.
**Erledigt 01.09.2026** — `esp32c3`-Env, RS-485 auf UART1, Pins via `build_flags`,
Status-LED an GPIO6, `-DARDUINO_USB_CDC_ON_BOOT=1`. `pio test -e native` grün.
Am Gerät noch nicht getestet.

---

## T13 — Web-UI der Zentralsteuerung

Ablösung der minimalen Web-UI aus T8 durch eine bedienbare Oberfläche (Vorschlag
und Freigabe mit dem Betreiber über einen Design-Mockup).

- Eine `PROGMEM`-Seite (System-Schriften, kein CDN, ~14 KB), Dark-Theme,
  Sidebar-Navigation, Ansichten Übersicht / Module / Log / Einstellungen.
- Neues hardwareunabhängiges `lib/eventlog/` (Ereignis-Ringpuffer, host-getestet).
- REST erweitert (`/api/system`, `/api/log`, `/api/module`, `/api/enumerate`,
  `/api/time`, `/api/wifi/*`, `/api/reboot`); `/api/config` deckt NTP-Server,
  Zeitzone, feste IP und die Schalter MQTT / REST-Schreib-API / OTA / mDNS ab.
- `busmaster` zählt CRC-Fehler und Timeouts; neu `busmaster_identify()`.

**Fertig, wenn:** `pio run -e esp32c3` kompiliert und `pio test -e native` grün
bleibt (inkl. `test_eventlog`).
**Erledigt 01.09.2026** — `pio test -e native` 40/40, Flash ~998 KB (76 %).
Spezifikation v0.10. Am Gerät noch nicht getestet.

---

## Offene Messungen

Diese Punkte sind noch nicht geklärt. Alles, was davon abhängt, bleibt parametrierbar und blockiert die Fertigung nicht.

| Nr | Frage | Wirkung |
|---|---|---|
| O-2 | Braucht Pin 9 der Anzeige 5 V oder 12–20 V zur Ansteuerung? | Bestückungsvariante Q-5, Q-15 oder S; Wert von R9; ob Ader 9 des Buskabels belegt wird |
| O-5 | Zulässiger Senkenstrom der Hall-Sensoren; genügt 5,0 V als Sensorspannung? | Werte von R1, R3, R5; ob R14 bestückt bleibt |
| O-6 | Blatt-Offset zwischen Leerbildimpuls und Blattindex | EEPROM-Parameter, wird beim ersten Abgleich ermittelt |

Nicht blockierend: die Funktion des Nullimpulses an Pin 7 der Anzeige. Er wird verdrahtet und auf PA6 geführt, von der Firmware aber nicht ausgewertet.

Messergebnisse gehören nach `docs/messprotokolle/` und werden anschließend in `docs/spezifikation.md` und `docs/schaltplan-daughtercard.md` eingearbeitet.

---

## Faktenblatt

| Größe | Wert |
|---|---|
| Blätter je Modul | 40 |
| Zählimpulse je Blatt | exakt 1 |
| Leerbildimpulse je Umdrehung | 1 |
| Zeit je Blatt | 60 ms (= 3 Netzperioden bei 50 Hz) |
| Volle Umdrehung | 2,40 s |
| Motor | Berger RSM 42/12, 42 V, 250 U/min, 1,8 VA |
| Triac | Teccor L201E3, 200 V, 1 A, Gate ca. 3 mA in allen vier Quadranten |
| Modul-CPU | ATtiny1616, SOIC-20, 5 V |
| Bustreiber | TP8485E-SR, LCSC C94206 |
| Busadressen | 1–250, Serviceadresse 250, Broadcast 0 |
| Zeichensatz | Blatt 1–2 Leerbild, 3–12 = 0–9, 13–38 = A–Z, 39 = `-`, 40 = `.` |
| Wegberechnung | `(Ziel − Ist) mod 40`, nur vorwärts |
| Trafo | Ringkern 230 V → 2 × 18 V in Reihe, 50–80 VA |
| Zulässige Motorspannung | 35,7 … 46,2 V |
