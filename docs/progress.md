# Fortschritt — laufende Inbetriebnahme

Arbeitsliste für die aktuelle Phase (Master-PCB + Daughter Cards am Gerät,
seit 19.09.2026). Die vollständige historische Aufgabenliste T1–T20 steht in
`docs/projektstand.md`, die Firmware-Versionshistorie in
`firmware/CHANGELOG.md`. Diese Datei ist der schnelle Überblick für die
laufende Fehlersuche — bei jeder Sitzung oben ergänzen, nicht umschreiben.

**Stand 25.09.2026:** Master v1.15, Modul-Firmware im Versionsgleichstand.
Vier Daughter Cards körperlich angeschlossen (Ziel: zehn). Grundfunktionen
(Bus, Enumeration, Web-UI, MQTT, OTA) laufen; mehrere Zuverlässigkeitsfragen
bei mehr als zwei Karten sind offen (siehe unten).

## Erledigt seit der Master-PCB-Inbetriebnahme

**Root-Cause-Funde (Hardware-nah)**
- DI-Pin-Bug im Modul: `gpio_init()` setzte `TXD` (PB2) nie als Ausgang,
  USART0 konnte `DI` nie treiben. Fix `f24b861`. Derselbe Bug unabhängig im
  residenten Bootloader gefunden und behoben, `3d65464`.
- Issue #16 (Mehrkarten-Instabilität): `web.handleClient()` blockierte
  `loop()` gelegentlich 100–130 ms, `busmaster` rechnete mit veraltetem
  Zeitstempel → Master sendete dieselbe Anfrage doppelt, per Logic Analyzer
  bewiesen. Fix `3374dc0` (frischer Zeitstempel für den Bus-Pfad) +
  `8aa33e7` (`WiFi.setSleep(false)`, größerer UART-Puffer). Danach keine
  beschädigte Antwort mehr beobachtet, **Issue bleibt offen bis
  Langzeit-Stabilität über mehr Karten bestätigt ist**.
- BODCFG-Fuse (ATtiny1616) war nirgends konfiguriert (Werksvorgabe `0x00` =
  Brown-out-Detection komplett aus) — Verdacht nach einem Fall, in dem eine
  Karte nach längerer Stromlosigkeit softwareseitig tot war. Gegen das
  Microchip-Datenblatt (DS40002204A) geprüft und auf `0xE5` gesetzt
  (`LVL=BODLEVEL7` 4,2 V + `ACTIVE`/`SLEEP=Enabled`), `1e26e7e`.

**Firmware-Update-Mechanismus**
- Firmware-Versionsschema eingeführt (`firmware/CHANGELOG.md`,
  Master+Modul gemeinsam `MAJOR.MINOR`), `3ee3d9f`.
- Modul-Version in der UI blieb nach erfolgreichem Bus-Update stehen
  (`ver_known` wurde nie zurückgesetzt), `d73b2dd`.
- Eine Karte, die mitten in einem Bus-Update im Bootloader hängen bleibt,
  galt als „offline" und war über „Alle aktualisieren"/„Veraltete
  aktualisieren" nie wieder erreichbar — neuer Button „Offline erneut
  versuchen", `15d4065`. **Noch nicht am Gerät verifiziert.**
- GitHub-Update: Master kann die neueste `.kota` direkt aus dem Repo laden
  (`POST /api/update/github`), `943d796`. Kostet spürbar Flash (79,6 % →
  90,6 %), bewusst in Kauf genommen.
- `updi.js` (Werksflasher): EEPROM[8]-Diagnose (Bootloader-Marker „App
  gültig") direkt nach Chip-Erase und vor dem Neustart im Log, `1fc7227` —
  **Ergebnis vom nächsten Werksflash noch nicht ausgewertet.**

**Web-UI**
- Marke/Titel verschwand beim horizontalen Scrollen der Mobil-Navigation,
  `afe15e8`.
- Moduswahl „Text" sprang vor dem Absenden zurück (Status-Poll glich zu
  früh mit dem unveränderten Server-Modus ab), `d9439a8`.
- LED-Helligkeit/-Blinkraten mehrfach nachjustiert, landete bei 5 % /
  exakt 1 Hz/4 Hz (`bfe736c` u. a.).
- Synchrones LED-Blinken: neuer Broadcast `CMD_LED_SYNC`, alle Karten +
  Master blinken jetzt im gleichen Takt statt phasenversetzt seit dem
  jeweils eigenen Boot, `3a72acf`.

**Diagnose-Tooling**
- Bus-Debug-Log live in der Web-UI (`/debug`, 400-Zeilen-Ringpuffer),
  `3cda3a6`/`cd244dd`.
- `busmaster_set_log()`-Hook für Zustandsübergänge, `2a55e10`.
- `tools/gen_firmware_memory_chart.py` → `firmware/speicherprofil.svg`,
  Speicherverbrauch nach Bereich für Master + Modul.

## Offene Punkte

**Bus-Zuverlässigkeit bei mehr als zwei Karten (aktuell wichtigster Block)**
- **Vier-Karten-Enumeration:** Unabhängig von der Kartenreihenfolge verliert
  häufig die *erste* Position beim „Enumeration neu starten" ihre Adresse,
  während alle folgenden Karten funktionieren und auf Identify reagieren.
  Hypothese: die einzige strukturelle Besonderheit von Position 1 ist der
  Pegelwandler 74LVC1G17 (Master → Karte 1), alle anderen Übergänge sind
  Karte-zu-Karte direkt. Noch nicht mit Logic Analyzer an `CHAIN` + A/B
  bestätigt — nächster Schritt bei den nächsten freien Kanälen.
- **Issue [#16](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/16):**
  Root Cause behoben, Langzeit-Stabilität mit mehr als zwei Karten noch
  nicht bestätigt.
- **Issue [#17](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/17):**
  Eine im Bootloader hängende Karte verschwindet nach einer Neuenumeration
  komplett aus der Modultabelle (Bootloader kennt die Enumerationsbefehle
  nicht) — dadurch hilft auch „Offline erneut versuchen" nicht mehr. Noch
  kein Fix umgesetzt, nur Ursache + mögliche Lösungsansätze dokumentiert.
- **„Reflash nötig trotz grünem Verify":** mehrfach beobachtet bei frisch
  geflashten Karten (alte + neue). Verify prüft nur Flash-Inhalt, nie
  EEPROM oder Fuses jenseits von `BOOTEND`. EEPROM[8]-Diagnose ist im
  Flasher ergänzt (`1fc7227`) — Auswertung beim nächsten Werksflash steht
  noch aus.

**Sonstige Hardware/Inbetriebnahme**
- Issue [#1](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/1)
  (J1-M): mechanische Kodierung der J1-Drehlage weiterhin offen.
- O-2, O-5, O-6 (Messungen an der Anzeigenplatine) weiterhin nicht
  gemessen — alles davon bleibt konfigurierbarer Parameter.
- Ringkerntrafo/42-V~-Netzteil noch nicht aufgebaut.
- Restliche Daughter Cards für das Zielbild von zehn Modulen noch nicht
  angeschlossen (aktuell vier).
- Selbsttest (Spez. 7.3) über eine volle Umdrehung je Modul mit
  Timing-Auswertung noch nicht durchgeführt.
- `GET_UID`-Verifikationslauf (Spez. 4.5.4/A-13) — `busmaster` hat das
  Kommando noch nicht, `tools/busctl.py` schon.

## Referenzen

- `docs/projektstand.md` — vollständige Aufgabenliste T1–T20 mit Commits
- `firmware/CHANGELOG.md` — Firmware-Versionshistorie 1.0–1.15
- `docs/module-bootloader.md` — Status der Firmware-Verteilung über den Bus
- GitHub Issues [#1](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/1),
  [#16](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/16),
  [#17](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/17)
