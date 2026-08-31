# Projektstand

Kurzer Einstieg für eine neue Arbeitssitzung. Details in `docs/backlog.md`.

## Erledigt

| Task | Ergebnis | Commit |
|---|---|---|
| T1 | Repo initialisiert, Branch `main`, `reference/` ignoriert, auf `github.com/TenOfNine/SmartKroneSplitFlap` (privat) gepusht | `16eaedf`, `d1c6ba8` |
| T2 | `tools/setup.sh` (KiCad 9, venv mit kicad-sch-api + kicad-skip, PlatformIO, Verifikation inkl. Roundtrip-Test). `.gitattributes` für LF. Recherchestand in `docs/toolchain.md`. | `3f440eb` |
| T2-Nachtrag | Toolchain auf Ubuntu 24.04 verifiziert: KiCad 9.0.9, Roundtrip erfolgreich, Backend `kicad-sch-api`. Probe in `setup.sh` korrigiert (`.add` statt `add_component`). Details in `docs/toolchain.md`. | `74cc386` |
| T3 | `docs/symbolpruefung.md` — Pinprüfung ATtiny1616-S und TP8485E-SR (via MAX3485) gegen Datenblätter, vom Betreiber freigegeben. | `2d67fd3`, `bfd19d5` |
| T4 | `hardware/daughtercard/daughtercard.kicad_sch` aus der Netzliste generiert (`tools/gen_daughtercard_sch.py`), **ERC 0/0**, PDF + PNG-Vorschau in `docs/`. Projektbibliothek `krone.kicad_sym` mit allen 15 Symbolen. P-1/P-2 vom Betreiber freigegeben (`docs/pruefpunkte-t4.md`). | `c023799`, `8500aa9`, `0521842`, `aebb75a` |
| T5 | Footprints für alle 48 Bauteile in `FOOTPRINTS` (`gen_daughtercard_sch.py`), gegen `/usr/share/kicad/footprints` geprüft. PCB-Netzliste `--netlist` exportierbar. Platzierungsvorschlag `docs/layout-daughtercard.md`. | `e569300` |
| T6 | `firmware/module/lib/protocol/` (Rahmen, CRC16/MODBUS, Kommandotabelle) und `lib/enumeration/` (Enumerations-Automat, Rückfall, Kollision), hardwareunabhängig. `pio test -e native`: **39/39** grün. | `b871628` |
| T7 (P-3) | Befund: RS-485-Pins der Spez. passten nicht zur USART0-Belegung des ATtiny1616. Freigegeben und korrigiert: RO→PB3, DE→PB0, PB1 Reserve. Schaltplan v0.3 (ERC 0/0), Spez. v0.5, `docs/pruefpunkte-t7.md`. | `52a022c`, `b008f8a` |
| T7 | Modul-Firmware ATtiny1616, **bare metal** (avr-libc, kein megaTinyCore). `src/board.h`, `src/main.c`, Libs `lib/motion/` + `lib/config/`. `pio run -e attiny1616`: **5303 B Flash** (< 8 KB). `pio test -e native`: **62/62** grün. | `35fad05` |
| T8 | Master-Firmware ESP32 (`firmware/master/`). Libs `charmap`/`clocktext`/`busmaster`/`masterapp`/`hadiscovery` — `pio test -e native` **33/33**. `src/main.cpp`: UART2-RS485, WiFiManager, WebServer/REST (7.5), MQTT + HA-Discovery (7.6), NTP, OTA. `pio run -e esp32`: ~950 KB Flash. | `1a0e3c0` |
| T9 | `tools/busctl.py` — Bus-Kommandozeilenwerkzeug (enum/status/set/show/home/stop/ping/uid/config/sniff/selftest), `--sim N` / `--loopback` ohne Hardware. `tools/test_busctl.py`: 13 Tests. | `9d931cc` |
| T10 | `.github/workflows/ci.yml` — Jobs `host-tests` (module+master `pio test -e native`, `test_busctl.py`), `firmware` (`attiny1616`-Build + `check_flash.py` < 8 KB, `esp32`-Build), `hardware` (KiCad 9, `build_krone_symbols.py --check`, `gen_daughtercard_sch.py --check-only`, ERC 0/0), `release` (bei Tag `v*`: PDF, Gerber sobald ein `.kicad_pcb` vorliegt). Alle Schritte lokal verifiziert. | `<dieser>` |

## Backlog T1–T10 abgeschlossen

| Nachtrag | Ergebnis | Commit |
|---|---|---|
| PCB-Vorplatzierung | `hardware/daughtercard/daughtercard.kicad_pcb` von `tools/gen_daughtercard_pcb.py` (pcbnew): alle 48 Bauteile mit Footprint + Netz + grober Position, Umriss, 4 Bohrungen, Netzklassen. DRC: nur unverdrahtete Netze + ~4 enge Nachbarpaare. Vorschau `docs/pcb-daughtercard.png`. | `<dieser>` |
| FreeRouting-Anbindung | `tools/setup_freerouting.sh` (JAR + JRE 25 nach `tools/vendor/`), `tools/route_daughtercard.py` (DSN → FreeRouting → SES). GUI-Plugin installiert. `docs/toolchain.md` §6. Router-Lauf erst nach finaler Platzierung. | `bf70800` |
| JLCPCB-Bestückung vorbereitet | `LCSC`-Dict in `gen_daughtercard_sch.py` (Basic Parts wo möglich), `--jlc`-Export in `gen_daughtercard_pcb.py` (`jlc/BOM.csv` + `CPL.csv`, gitignored). R16 1206→0805 (Basic, D-1). Zweite Anschlussbild-Prüfung aller Symbole: ✅. Offene Punkte D-2..D-4 in `docs/jlc-bestueckung.md`. | `<dieser>` |

Nächste sinnvolle Schritte:

- **PCB routen** in `daughtercard.kicad_pcb`: Bauteile feinjustieren (oberer
  Streifen), Leiterbahnen ziehen (AC-Zone an der Kante!), Massefläche. Danach
  greift der Gerber-Export im CI-Release.
- Messungen **O-2, O-5, O-6** an der Anzeigenplatine (Betreiber). Ergebnisse nach
  `docs/messprotokolle/`, dann in Schaltplan und Firmware-Parameter einarbeiten.
- Master-Firmware: **Selbsttest** (7.3) über eine volle Umdrehung je Modul mit
  Timing-Auswertung — bislang nur Homing-Broadcast. Und der **Verifikationslauf
  über `GET_UID`** (4.5.4 / A-13): `busmaster` hat noch kein GET_UID-Kommando,
  die Seriennummern-Prüfung fehlt.
- Inbetriebnahme nach `docs/spezifikation.md` Kapitel 10 mit `tools/busctl.py`
  (dort ist `uid <addr>` schon vorhanden).

## Umgebung

- T4–T10 brauchen die Linux-Toolchain (`kicad-cli`, `kicad-sch-api`, `pio`).
  Zielumgebung Ubuntu 24.04, Einrichtung über `bash tools/setup.sh`, verifiziert
  am 28.08.2026. `kicad-cli` erc/export nur über `xvfb-run -a` aufrufen.
- Verbindliche Reihenfolge laut `docs/backlog.md`. Bei Widerspruch Spezifikation ↔
  Schaltplan gilt die Netzliste (`docs/schaltplan-daughtercard.md`, Kap. 6).
- Offene Messungen O-2, O-5, O-6 bleiben Parameter, nichts davon fest einkompilieren.

## Offene organisatorische Punkte

- Git-Identität ist nur lokal gesetzt (`user.name = TenOfNine`,
  `user.email = phi.hoffmann@hotmail.de`). Bei Bedarf anpassen.
- `docs/spezifikation.md` Version 0.4: vor jeder Änderung die Änderungshistorie
  im Anhang D fortschreiben.
- `docs/pruefpunkte-t4.md` P-1 und P-2 warten auf die zweite Freigabe.
