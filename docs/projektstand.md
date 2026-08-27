# Projektstand

Kurzer Einstieg für eine neue Arbeitssitzung. Details in `docs/backlog.md`.

## Erledigt

| Task | Ergebnis | Commit |
|---|---|---|
| T1 | Repo initialisiert, Branch `main`, `reference/` ignoriert, auf `github.com/TenOfNine/SmartKroneSplitFlap` (privat) gepusht | `16eaedf`, `d1c6ba8` |
| T2 | `tools/setup.sh` (KiCad 9, venv mit kicad-sch-api + kicad-skip, PlatformIO, Verifikation inkl. Roundtrip-Test). `.gitattributes` für LF. Recherchestand in `docs/toolchain.md`. | `3f440eb` |
| T3 | `docs/symbolpruefung.md` — Pinprüfung ATtiny1616-S und TP8485E-SR (via MAX3485) gegen Datenblätter, vom Betreiber freigegeben. | `2d67fd3`, `<dieser>` |

## Nächster Schritt: T4

Aus der Netzliste in `docs/schaltplan-daughtercard.md` Kapitel 6 eine `.kicad_sch`
erzeugen, ERC bis fehlerfrei, PDF nach `docs/`.

Vorarbeiten aus T3, die in T4 einfließen:

- Projekt-Symbolbibliothek `hardware/daughtercard/symbols/krone.kicad_sym` anlegen.
- `ATtiny1616-S` (mit Basis `ATtiny406-S`) aus der offiziellen KiCad-Bibliothek kopieren.
- Symbol `TP8485E-SR` als unveränderte Kopie von `Interface_UART:MAX3485`,
  Footprint `Package_SO:SOIC-8_3.9x4.9mm_P1.27mm`, Feld `LCSC = C94206`.
- Auf `+5V` und `GND` je ein `PWR_FLAG` setzen (beide Bausteine haben `power_in`-Pins).

## Umgebung

- T4–T10 brauchen die Linux-Toolchain (`kicad-cli`, `kicad-sch-api`, `pio`).
  Zielumgebung Ubuntu 24.04, Einrichtung über `bash tools/setup.sh`.
- Verbindliche Reihenfolge laut `docs/backlog.md`. Bei Widerspruch Spezifikation ↔
  Schaltplan gilt die Netzliste (`docs/schaltplan-daughtercard.md`, Kap. 6).
- Offene Messungen O-2, O-5, O-6 bleiben Parameter, nichts davon fest einkompilieren.

## Offene organisatorische Punkte

- Git-Identität ist nur lokal gesetzt (`user.name = TenOfNine`,
  `user.email = phi.hoffmann@hotmail.de`). Bei Bedarf anpassen.
- `docs/spezifikation.md` Version 0.3: vor jeder Änderung die Änderungshistorie
  im Anhang D fortschreiben.
