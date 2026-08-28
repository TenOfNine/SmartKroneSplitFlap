# Projektstand

Kurzer Einstieg für eine neue Arbeitssitzung. Details in `docs/backlog.md`.

## Erledigt

| Task | Ergebnis | Commit |
|---|---|---|
| T1 | Repo initialisiert, Branch `main`, `reference/` ignoriert, auf `github.com/TenOfNine/SmartKroneSplitFlap` (privat) gepusht | `16eaedf`, `d1c6ba8` |
| T2 | `tools/setup.sh` (KiCad 9, venv mit kicad-sch-api + kicad-skip, PlatformIO, Verifikation inkl. Roundtrip-Test). `.gitattributes` für LF. Recherchestand in `docs/toolchain.md`. | `3f440eb` |
| T2-Nachtrag | Toolchain auf Ubuntu 24.04 verifiziert: KiCad 9.0.9, Roundtrip erfolgreich, Backend `kicad-sch-api`. Probe in `setup.sh` korrigiert (`.add` statt `add_component`). Details in `docs/toolchain.md`. | `74cc386` |
| T3 | `docs/symbolpruefung.md` — Pinprüfung ATtiny1616-S und TP8485E-SR (via MAX3485) gegen Datenblätter, vom Betreiber freigegeben. | `2d67fd3`, `bfd19d5` |
| T4 | `hardware/daughtercard/daughtercard.kicad_sch` aus der Netzliste generiert (`tools/gen_daughtercard_sch.py`), **ERC 0 Fehler / 0 Warnungen**, PDF in `docs/`. Projektbibliothek `krone.kicad_sym` mit allen 15 Symbolen (`tools/build_krone_symbols.py`). Zwei Netzlisten-Korrekturen offen zur zweiten Prüfung: `docs/pruefpunkte-t4.md`. | `c023799`, `8500aa9`, `<dieser>` |

## Nächster Schritt: T5

Footprints zuordnen und PCB-Netzliste exportieren, siehe `docs/backlog.md` T5.
`.kicad_sch` liegt vor, Symbole tragen bereits Footprint-Vorschläge
(SOIC-20W, SOIC-8, SOT-23, 0805 …) aus der KiCad-Bibliothek — gegen Kapitel 5
des Schaltplandokuments prüfen.

**Vorher:** `docs/pruefpunkte-t4.md` mit dem Betreiber klären. P-1 (Testpads) und
P-2 (BAT54S-Pinbelegung) sind vorläufig aufgelöst; eine Korrektur würde die
Netzliste und damit `gen_daughtercard_sch.py` ändern.

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
