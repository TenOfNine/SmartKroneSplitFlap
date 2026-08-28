# hardware/daughtercard

KiCad-Projekt der Modulsteuerung (eine je Anzeigenmodul).

## Dateien

| Datei | Herkunft |
|---|---|
| `symbols/krone.kicad_sym` | **generiert** von `tools/build_krone_symbols.py` aus der KiCad-9-Bibliothek. Alle 15 im Schaltplan verwendeten Symbole, abgeflacht. Nicht von Hand bearbeiten. |
| `sym-lib-table` | projektlokale Bibliothekstabelle, bindet `krone` über `${KIPRJMOD}` ein |
| `daughtercard.kicad_sch` | **generiert** von `tools/gen_daughtercard_sch.py` aus der Netzliste in `docs/schaltplan-daughtercard.md` Kapitel 6 |
| `daughtercard.kicad_pro` | minimales Projektfile, damit `kicad-cli` die projektlokale `sym-lib-table` findet |

## Neu erzeugen

```bash
source .venv/bin/activate
python tools/build_krone_symbols.py          # nur nötig, wenn sich die Symbolauswahl ändert
python tools/gen_daughtercard_sch.py --erc --pdf
```

`--erc` schreibt `docs/erc-daughtercard.rpt`, `--pdf` schreibt `docs/daughtercard.pdf`.
Beide Skripte haben einen `--check`- bzw. `--check-only`-Modus für die CI (T10).

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

## Offene Prüfpunkte

Zwei Netzlisten-Entscheidungen aus T4 stehen noch zur zweiten Prüfung durch den
Betreiber, siehe [`docs/pruefpunkte-t4.md`](../../docs/pruefpunkte-t4.md).
