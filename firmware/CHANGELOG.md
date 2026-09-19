# Firmware-Änderungshistorie

Eine gemeinsame Versionsnummer für Master- (`firmware/master/`) und
Modul-Firmware (`firmware/module/`) — beide werden im Gleichschritt
entwickelt und ausgeliefert.

**Schema:** `MAJOR.MINOR`.

- **MINOR** steigt bei jeder ausgelieferten Änderung an einer der beiden
  Firmwares (Fix, neues Feature, Aufräumen).
- **MAJOR** steigt nur bei großen Änderungen — z. B. einem Bruch im
  Busprotokoll, einem grundlegenden Architekturwechsel, oder dem Übergang
  von der Inbetriebnahme-Phase zum dauerhaften Betrieb mit allen zehn
  Anzeigenmodulen. Bisher noch nicht eingetreten — die gesamte Historie
  unten ist `1.x`.

**Wo die Version steckt:**

- Master: `FW_VERSION_MAJOR`/`FW_VERSION_MINOR` in `firmware/master/src/main.cpp`,
  ausgeliefert über `/api/system` (`fw_version`) und in der Web-UI-Fußzeile.
- Modul: `APP_VERSION_MAJOR`/`APP_VERSION_MINOR` in `firmware/module/src/board.h`,
  ausgeliefert über `CMD_GET_VERSION` — die Web-UI zeigt sie im Panel
  „Modul-Firmware" je Karte an, sobald die Version abgefragt wurde.

**Pflicht bei jeder Firmware-Änderung:** Zeile unten anhängen und die
beiden `*VERSION_MINOR`-Konstanten hochzählen (siehe CLAUDE.md
Arbeitsweise).

## Historie

| Version | Commits | Änderung |
|---|---|---|
| 1.0 | `f01190b`, `175b709`, `8ad7f10`, `07cd236` | Grundarchitektur: hardwareunabhängige Protokoll-/Enumerationsbibliothek (host-getestet), Modul-Firmware ATtiny1616 bare metal, Master-Firmware ESP32, erste Repo-Review-Fixes (Sendeecho-Bug, IDENTIFY) |
| 1.1 | `23a0a93` | Master auf ESP32-C3 Super Mini portiert (UART1, GPIO-Pins, natives USB) |
| 1.2 | `fe3a97b`, `4261649` | Eventlog, Bus-Zähler, erweiterte Status-JSON; neue Web-Oberfläche (Dark-Theme, Log-Tab, Einstellungen) |
| 1.3 | `4bd3a7e`, `5b0a262` | Hostname editierbar, Systemdiagnose, Voll-Backup; Zeitzonen-Auswahlliste + Sommerzeit-Schalter |
| 1.4 | `e682f31`, `2a96a88`, `0ec7cbc`, `5dc4db5`, `c1488d7` | Browser-OTA, ArduinoOTA entfernt, `.kota`-Signaturprüfung, Zugriffsschutz (Herkunft + Basic-Auth), Betreiberschlüssel |
| 1.5 | `1d01cf6`, `b232303` | `charmap_char()` (Blattnummer → Zeichen); MQTT Last-Will/`availability_topic`, Discovery-Cleanup |
| 1.6 | `4c6c2b2`, `e847da6`, `60b721a`, `346e7a1` | Firmware-Verteilung an Module über den Bus (Bootloader, `lib/fwupdate`, Panel „Modul-Firmware"); automatische Modulzahl |
| 1.7 | `dc7bddc`, `c6ac2cd`, `01dcdd0`, `206afd8` | Web-UI-Fix (Syntaxfehler), Modul-Konfiguration über die UI, Einstellungen thematisch gruppiert, Projekt-Logo |
| 1.8 | `f24b861`, `bfe736c`, `57ed779`, `892c67f`, `7a32284`, `a2965e4`, `67dc876`, `2a55e10`, `3cda3a6`, `cd244dd`, `3374dc0`, `8aa33e7`, `1dd87dc` | Master-PCB-Inbetriebnahme: DI-Pin-Bug im Modul gefunden+behoben, LED-Blinkraten/-Helligkeit korrigiert, Bus-Debug-Einstellung + Live-Log in der Web-UI, Versionsabfrage-Fix, Issue #16 (Mehrkarten-Instabilität: Root Cause + Fix) |
| 1.9 | `3ee3d9f`, `3d65464` | Firmware-Versionsnummer eingeführt (dieses Schema); Bootloader hatte denselben DI-Pin-Bug wie die App (eigene, unabhängige USART0-Initialisierung) — erster realer Test der Firmware-Verteilung über den Bus deckte es auf, beide Karten blieben im Bootloader hängen, Browser-Werksflash nötig zur Wiederherstellung |
| 1.10 | `943d796` | Master: Firmware-Update direkt aus dem GitHub-Repository (`POST /api/update/github`, lädt das neueste committete `.kota` über HTTPS, gleiche Signatur-/Hash-Prüfung wie der Browser-Upload). Braucht Internetzugriff der Steuerung selbst. Modul-Version mitgezogen (kein Funktionsunterschied, nur Versionsgleichstand) |
| 1.11 | *(dieser Commit)* | Mobile Web-UI: Marke/Titel blieb beim horizontalen Scrollen der Navigationsleiste unter 820 px Breite nicht sichtbar (`.side` scrollte als ein Block samt `.brand`) — Marke jetzt fest, nur die Tab-Leiste scrollt. Modul-Version mitgezogen (kein Funktionsunterschied, nur Versionsgleichstand) |
