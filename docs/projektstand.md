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
| T8 | Master-Firmware (`firmware/master/`). Libs `charmap`/`clocktext`/`busmaster`/`masterapp`/`hadiscovery` — hardwareunabhängig, host-getestet. `src/main.cpp`: RS-485, WiFiManager, WebServer/REST (7.5), MQTT + HA-Discovery (7.6), NTP, OTA. *(Zielhardware und Web-UI seither überarbeitet → T12 / T13.)* | `1a0e3c0` |
| T9 | `tools/busctl.py` — Bus-Kommandozeilenwerkzeug (enum/status/set/show/home/stop/ping/uid/config/sniff/selftest), `--sim N` / `--loopback` ohne Hardware. `tools/test_busctl.py`: 13 Tests. | `9d931cc` |
| T10 | `.github/workflows/ci.yml` — Jobs `host-tests` (module+master `pio test -e native`, `test_busctl.py`), `firmware` (`attiny1616`-Build + `check_flash.py` < 8 KB, `esp32`-Build), `hardware` (KiCad 9, `build_krone_symbols.py --check`, `gen_daughtercard_sch.py --check-only`, ERC 0/0), `release` (bei Tag `v*`: PDF, Gerber sobald ein `.kicad_pcb` vorliegt). Alle Schritte lokal verifiziert. | `<dieser>` |

## Nachträge nach T10 — Daughter Card fertig geroutet und bestellt

| Nachtrag | Ergebnis | Commit |
|---|---|---|
| PCB-Vorplatzierung | `hardware/daughtercard/daughtercard.kicad_pcb` von `tools/gen_daughtercard_pcb.py` (pcbnew): alle 48 Bauteile mit Footprint + Netz + grober Position, Umriss, 4 Bohrungen, Netzklassen. | (vor PR #2) |
| FreeRouting-Anbindung | `tools/setup_freerouting.sh` (JAR + JRE 25 nach `tools/vendor/`), `tools/route_daughtercard.py` (DSN → FreeRouting → SES → `finish_routes.py`). GUI-Plugin installiert. `docs/toolchain.md` §6. | `bf70800` |
| JLCPCB-Bestückung vorbereitet | `LCSC`-Dict in `gen_daughtercard_sch.py` (Basic Parts wo möglich), `--jlc`-Export in `gen_daughtercard_pcb.py` (`jlc/BOM.csv` + `CPL.csv`, gitignored). R16 1206→0805 (Basic, D-1). Zweite Anschlussbild-Prüfung aller Symbole: ✅. Offene Punkte D-2..D-4 in `docs/jlc-bestueckung.md`. | `f79b958` |
| J1 → Buchsenleiste | J1 von Wannenstecker auf Buchsenleiste 2×5 (`PinSocket_2x05_P2.54mm_Vertical`, Referenz BKL 10120960) — Karte wird board-to-board auf die Anzeigenplatine gesteckt. Pinbelegung unverändert, ERC 0/0. Schaltplan v0.5, Spez. v0.8. **Offen J1-M**: mechanische Kodierung der Drehlage (nicht kodierter Stecker + 42 V~ an Pin 2/4), siehe `docs/pruefpunkte-j1-buchsenleiste.md`. | `8f22656` |
| PCB gerouted | Betreiber-Platzierung (J1–J5 fix) → `tools/route_daughtercard.py` (FreeRouting 2.3.0 + `finish_routes.py` für die Reste + GND-Fläche/Stitching beidseitig). **F1 → 0-Ω-Brücke** (D-3), Q1–Q3/R7–R10 aus dem AC-Korridor, J6 unter U1. **Alle Bahnen 0,5 mm außer AC 1,5 mm** (Vorgabe; +5V-Abfall bei 10 Modulen → D-5). Schaltplan v0.7 (UUID-Remapping, kein Neu-Erzeugen). **Platine schwarz / Druck weiß**, GitHub-Marke + „TenOfNine" auf B.SilkS (`tools/add_silk_marks.py`). DRC 0/0, ERC 0/0. | PR #2 |
| Fertigungspaket | `tools/gen_manufacturing.py` → `hardware/daughtercard/manufacturing/` (committet): `daughtercard-gerbers.zip` + `gerber/` + `BOM.csv` + `CPL.csv` + `README.md`. BOM/CPL ohne J1–J6 (Handlötung), ohne DNP/JP/TP/H. 5 Positionen (D1–D3, Q1, D4) ohne LCSC-Nummer → im JLC-Warenkorb nachtragen. | PR #2 |
| Daughter Card bestellt | LCSC-Fix (D4→C2297 gegen 0201-Zwang, R8/R12→C149504, Q1→C8492, D1–D3→C19726), Fertigungspaket aktualisiert, bei JLCPCB in Auftrag. | PR #4 |

## T11–T13 — Zentralsteuerung (ESP32-C3 Super Mini)

| Nachtrag | Ergebnis | Commit |
|---|---|---|
| Master-Schaltplan | `hardware/master/master.kicad_sch` aus `docs/schaltplan-master.md` Kap. 6 (`tools/gen_master_sch.py`). ESP32-C3 Super Mini (steckbar), TP8485E @ 3,3 V, Fail-Safe-Bias + fester 120-Ω-Abschluss, CHAIN-Pegelwandler 74LVC1G17 (M-3), Ader-9-Lötbrücke + Boost-Steckplatz DNP (M-2/O-2), 5-V-Eingang, **keine 42 V~**. Projektbibliothek `krone_master.kicad_sym` (13 Symbole, `tools/build_krone_master_symbols.py`), Modul-Footprint `ESP32-C3-SuperMini` von Hand. ERC 0/0. Spez. v0.9. | `c9e5006`, `4236401` |
| Master-PCB | `tools/gen_master_pcb.py` (68 × 54 mm, 2 Lagen) + `tools/route_master.py` (FreeRouting 2.3.0 + `finish_routes.py` + Masseflächen + Silk-Marks). Antennen-Keepout unter U1 im Footprint. **DRC 0/0.** Schwarz / weiß, GitHub-Marke + „TenOfNine" auf B.SilkS. | `c9e5006` |
| Master-Fertigungspaket | `tools/gen_master_manufacturing.py` → `hardware/master/manufacturing/`: Gerber + Zip + BOM (12 Positionen, alle mit geprüfter LCSC-Nummer) + CPL + README. J1–J4, U1-Sockel, JP1 = Handlötung. | `c9e5006` |
| Symbolprüfung Master | `docs/symbolpruefung-master.md` **freigegeben (01.09.2026)**: 74LVC1G17 gegen Nexperia Rev. 16.1 §6.1, ESP32-C3-Modul-Pinbelegung + Einbaulage aus Fotos, TP8485E Verweis. 3D-Renders `docs/render-master-{top,bottom}.png` für die Sichtkontrolle. | `576488c` |
| Master-Firmware T12 | `firmware/master/` auf ESP32-C3 portiert: `env:esp32c3` (`board = esp32-c3-devkitm-1`), RS-485 auf UART1, GPIO-Konstanten via `build_flags`, Status-LED an GPIO6. `pio test -e native` 40/40 (mit `test_eventlog`). | `3f03c57` |
| Master-Web-UI (T13) | Weboberfläche neu (Dark-Theme, Ansichten Übersicht/Module/Log/Einstellungen, eine `PROGMEM`-Seite ohne CDN). REST erweitert (`/api/system`, `/api/log`, `/api/module`, `/api/enumerate`, `/api/time`, `/api/wifi/*`, `/api/reboot`, `/api/backup`, `/api/update`); `/api/config` deckt Hostname, NTP-Server, TZ, feste IP und die Schalter MQTT / REST-Schreib-API / OTA / mDNS ab. System-Ansicht: Hostname editierbar, CPU-Last / RAM / Chiptemperatur, Voll-Backup inkl. WLAN, **OTA-Update aus dem Browser** (`ota.bin`; ArduinoOTA entfernt). Neues `lib/eventlog` (host-getestet). Busmaster zählt CRC-Fehler + Timeouts. `pio run -e esp32c3`: ~999 KB Flash. Spez. v0.13. Design per Mockup mit dem Betreiber abgestimmt. | `0d163bc`, `680152f`, PR #6 |
| Master-MQTT (T14) | HA-Anbindung vervollständigt: Last-Will-Topic `<base>/status` + `availability_topic` in jeder Discovery-Payload (Entities werden bei Ausfall „nicht verfügbar"), Zustands-Topics retained inkl. neu `text/state` / `mode/state`, `module/<n>/char` = dargestelltes Zeichen (`charmap_char()`, host-getestet), Discovery-Cleanup entfallener Module, MQTT-Puffer 1 KB. `pio test -e native` 43/43. Spez. v0.14. | `ef5bbe5`, `18de034` |

Nächste sinnvolle Schritte:

**Hardware Master**
- Betreiber gleicht `docs/render-master-top.png` mit einem echten ESP32-C3-Modul
  ab (Pin-1 = 5V rechts oben) und prüft `master.kicad_pcb` in der KiCad-GUI —
  ggf. `Edge.Cuts`-Aussparung unter der USB-C-Buchse (siehe `docs/layout-master.md`).
- Bench-Test **M-3** (CHAIN 3,3 V direkt vs. 74LVC1G17), danach Bestückungsvariante
  festlegen und die Master-PCB bei JLCPCB bestellen.

**Firmware am Gerät**
- Master-Firmware (`pio run -e esp32c3`) auf ein echtes Modul flashen — T12/T13
  kompilieren und die Host-Tests laufen, am Gerät ungetestet. Web-Flasher:
  <https://tenofnine.github.io/SmartKroneSplitFlap/> (nachdem Pages aktiviert ist).
- **Selbsttest** (Spez. 7.3) über eine volle Umdrehung je Modul mit
  Timing-Auswertung — bislang nur Homing-Broadcast.
- **Verifikationslauf über `GET_UID`** (Spez. 4.5.4 / A-13): `busmaster` hat noch
  kein GET_UID-Kommando; `tools/busctl.py` hat `uid <addr>` schon.

**Messungen / Inbetriebnahme**
- **O-2, O-5, O-6** an der Anzeigenplatine (Betreiber). Ergebnisse nach
  `docs/messprotokolle/`, dann in Schaltplan/Spezifikation und Firmware-Parameter
  einarbeiten.
- Stufenweise Inbetriebnahme nach `docs/spezifikation.md` Kapitel 10 mit
  `tools/busctl.py`.

## Umgebung

- Die Hardware-/Firmware-Aufgaben brauchen die Linux-Toolchain (`kicad-cli`,
  `kicad-sch-api`, `pcbnew`, `pio`, FreeRouting + JRE 25). Zielumgebung Ubuntu
  24.04, Einrichtung über `bash tools/setup.sh` (+ `tools/setup_freerouting.sh`),
  verifiziert am 28.08.2026. `kicad-cli` erc/export/render nur über `xvfb-run -a`.
- Verbindliche Reihenfolge laut `docs/backlog.md`. Bei Widerspruch Spezifikation ↔
  Schaltplan gilt die Netzliste im jeweiligen Schaltplandokument (Kap. 6).
- Offene Messungen O-2, O-5, O-6 bleiben Parameter, nichts davon fest einkompilieren.

## Offene organisatorische Punkte

- Git-Identität ist nur lokal gesetzt (`user.name = TenOfNine`,
  `user.email = phi.hoffmann@hotmail.de`). Bei Bedarf anpassen.
- **GitHub Pages** muss der Betreiber einmalig aktivieren (Settings → Pages →
  Source „GitHub Actions"), damit der Web-Flasher unter
  `tenofnine.github.io/SmartKroneSplitFlap` erreichbar wird.
- `docs/spezifikation.md` steht auf Version 0.13: vor jeder Änderung die
  Änderungshistorie im Anhang D fortschreiben.
- **J1-M** (`docs/pruefpunkte-j1-buchsenleiste.md`, Issue #1): mechanische
  Kodierung der J1-Drehlage festlegen.
- **M-1-Restpunkt:** Sichtkontrolle der U1-Einbaulage am Render durch den
  Betreiber (Symbolprüfung Master formal freigegeben).
