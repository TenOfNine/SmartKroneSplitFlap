# Fortschritt — laufende Inbetriebnahme

Arbeitsliste für die aktuelle Phase (Master-PCB + Daughter Cards am Gerät,
seit 19.09.2026). Die vollständige historische Aufgabenliste T1–T20 steht in
`docs/projektstand.md`, die Firmware-Versionshistorie in
`firmware/CHANGELOG.md`. Diese Datei ist der schnelle Überblick für die
laufende Fehlersuche — bei jeder Sitzung oben ergänzen, nicht umschreiben.

**Stand 02.10.2026 — Firmware v1.16 (Review-Fixes, am Gerät noch nicht verifiziert):**
Zwei unabhängige Voll-Reviews (30.09./01.10.) haben 32 Issues ergeben (#18–#49).
Behoben in v1.16 (Details je Issue): Flag-Bit 2 hielt den Motor in IDLE/ERROR
bestromt (#18, **vor dem ersten Anlegen von 42 V~ entscheidend**), Plain-Upload
bei BOOTEND=0x0C legte die Interrupts lahm (#19), Ursache des
Positions-1-Adressverlusts (#20, Master-Timing nach Enumerationsende bzw.
`masterapp_tick` während der Enumeration – die Pegelwandler-Hypothese unten ist
damit überholt), Bus-Zeitbudget/Sendewarteschlange (#21), Enumeration (#22),
SET_CONFIG sofort wirksam (#23), Positionsring (#24), Zielpuffer (#25),
Soll/Ist-Abgleich (#26), Persistenz der Betriebsart (#27), GET_UID/250/0x06
(#28), Bootloader (#29, #30 – wirkt erst nach Werksflash), Update-Sperre (#31),
WiFiManager-Portal (#32), CSRF/Rebinding (#33), XSS (#34), WLAN-Start (#35).
Offen: Hardware-Mängel #36 (Verpolschutz Master wirkungslos – Klemme nicht
verpolen!), #37 (Bestückungsdruck), Sammel-Issues #38–#43, Inbetriebnahme
#44–#49. **Gerätetest der 1.16: #49.**

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
  ~~Hypothese: Pegelwandler 74LVC1G17 an Position 1.~~ **Ursache gefunden
  (Review 01.10., Ko-Simulation, Issue #20):** Im Durchlauf nach
  Enumerationsende sendete der Master mit veraltetem Zeitstempel noch eine
  Statusabfrage an Adresse 1; deren Wiederholung kollidierte mit der Antwort
  von Karte 1, die daraufhin dauerhaft taub wurde. Mit fester Modulzahl traf
  `masterapp_tick` schon das ENUM_ASSIGN-ACK von Karte 1. Behoben in v1.16;
  Prüfung am Gerät in #49 (im `/debug`-Log darf auf
  `TX 7: AA 55 04 52 00 0D 61` kein `await retry cmd=0x10 addr=1` mehr folgen).
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
- Ringkerntrafo (Sedlbauer RSO 825028, 100 VA, 2 × 18 V) bestellt; Netzseite
  (Sicherung 0,8 A T, NTC, Varistor 275 V~, Sekundärsicherung 3,15 A T) in
  Spez. 8.1 festgelegt, **Aufbau steht aus**. Offen: NTC-Wert und
  Sicherungsbauform vor dem Kauf bestätigen.
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
  [#17](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/17),
  Review-Befunde [#18–#43](https://github.com/TenOfNine/SmartKroneSplitFlap/issues?q=is%3Aissue+label%3Asicherheit%2Cmodul-firmware%2Cmaster-firmware%2Chardware),
  offene Inbetriebnahme-Punkte: Netzseite [#44](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/44),
  O-2/O-5/O-6 [#45](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/45),
  restliche Karten [#46](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/46),
  Selbsttest [#47](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/47),
  Reflash trotz Verify [#48](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/48),
  Gerätetest v1.16 [#49](https://github.com/TenOfNine/SmartKroneSplitFlap/issues/49)
