# hardware/daughtercard

KiCad-Projekt der Modulsteuerung (eine je Anzeigenmodul).

## Schnellcheck

![Schaltplan-Vorschau](../../docs/daughtercard.png)

Vollauflösung: [`docs/daughtercard.pdf`](../../docs/daughtercard.pdf) ·
ERC-Bericht: [`docs/erc-daughtercard.rpt`](../../docs/erc-daughtercard.rpt) (0 Fehler, 0 Warnungen)

## Dateien

| Datei | Herkunft |
|---|---|
| `symbols/krone.kicad_sym` | **generiert** von `tools/build_krone_symbols.py` aus der KiCad-9-Bibliothek. Alle 15 im Schaltplan verwendeten Symbole, abgeflacht. Nicht von Hand bearbeiten. |
| `sym-lib-table` | projektlokale Bibliothekstabelle, bindet `krone` über `${KIPRJMOD}` ein |
| `daughtercard.kicad_sch` | **generiert** von `tools/gen_daughtercard_sch.py` aus Netzliste (`docs/schaltplan-daughtercard.md` Kap. 6) und Footprint-Tabelle (T5) |
| `daughtercard.kicad_pro` | minimales Projektfile, damit `kicad-cli` die projektlokale `sym-lib-table` findet |
| `daughtercard.net` | **generiert** (`--netlist`), nicht versioniert. PCB-Netzliste für den Import in den Board-Editor. |

## Neu erzeugen

```bash
source .venv/bin/activate
python tools/build_krone_symbols.py                            # nur bei Änderung der Symbolauswahl
python tools/gen_daughtercard_sch.py --erc --pdf --png --netlist
```

| Flag | Ausgabe |
|---|---|
| `--erc` | `docs/erc-daughtercard.rpt` |
| `--pdf` | `docs/daughtercard.pdf` |
| `--png` | `docs/daughtercard.png` (Vorschau ohne Rahmen, für GitHub) |
| `--netlist` | `daughtercard.net` (PCB-Netzliste, nicht versioniert) |

`--check-only` (bzw. `--check` bei `build_krone_symbols.py`) prüft ohne zu schreiben —
für die CI (T10). Der Check umfasst: jeder Pin an genau einem Netz oder NC, jedes
Bauteil mit vorhandenem Footprint.

Netznamen tragen im Netlist-Export das KiCad-übliche Wurzelblatt-Präfix (`/+5V`,
`/GND` …), weil die Rails über lokale Labels laufen. Für den PCB-Import ist das
unerheblich; wer es ohne Präfix will, stellt die Rails auf Power-Symbole um.

## Zum Schaltplan

Die Verbindungen werden über **gleichnamige lokale Labels an den Pins** hergestellt,
nicht über gezeichnete Leitungen. Die Bauteile liegen in einem groben Raster, nicht
handverlegt. Das ist bewusst so:

- Die menschenlesbare Darstellung der Schaltung ist `docs/schaltplan-daughtercard.md`
  Kapitel 5 (Prinzipschaltbilder) und Kapitel 6 (verbindliche Netzliste).
- Die `.kicad_sch` ist das maschinell erzeugte Abbild von Kapitel 6 für ERC,
  Netzlistenexport und – nach dem Footprint-Schritt T5 – das Layout.

Eine handverlegte, ablesbare Schaltplanseite kann später folgen; sie ist nicht
Teil von T4.

## Layout (T5)

Footprints sind allen 48 Bauteilen zugeordnet (`FOOTPRINTS` in
`tools/gen_daughtercard_sch.py`). Platzierungsvorschlag als Text:
[`docs/layout-daughtercard.md`](../../docs/layout-daughtercard.md).

## Prüfpunkte

Die zwei Netzlisten-Entscheidungen aus T4 (P-1 Testpads, P-2 BAT54S) sind vom
Betreiber freigegeben, siehe [`docs/pruefpunkte-t4.md`](../../docs/pruefpunkte-t4.md).
