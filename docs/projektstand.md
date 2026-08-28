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
| T7 (P-3) | Befund: RS-485-Pins der Spez. passten nicht zur USART0-Belegung des ATtiny1616. Freigegeben und korrigiert: RO→PB3, DE→PB0, PB1 Reserve. Schaltplan v0.3 (ERC 0/0), Spez. v0.5, `docs/pruefpunkte-t7.md`. | `52a022c`, `<dieser>` |

## In Arbeit: T7 — Modul-Firmware

Zustandsautomat nach Spezifikation Kapitel 6 (INIT/HOMING/IDLE/MOVING/ERROR),
USART0 im RS-485-Modus über XDIR (Standard-MUX, RXD=PB3/TXD=PB2/XDIR=PB0),
Impulsauswertung auf fallende Flanke mit 20 ms Sperrzeit, Blattzahlerkennung,
EEPROM-Parameter, Watchdog + Laufzeitüberwachung. `pio run -e attiny1616`
fehlerfrei, Flash < 8 KB. Siehe `docs/backlog.md` T7.

Hardwarenahe Konstanten gehören in **genau eine** Headerdatei (CLAUDE.md).
`lib/protocol/` und `lib/enumeration/` sind fertig und getestet — T7 verdrahtet
sie mit der Hardware. megaTinyCore-Plattform ist installiert.

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
