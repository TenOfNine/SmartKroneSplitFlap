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
