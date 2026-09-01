#!/usr/bin/env python3
"""Erzeugt das vollstaendige Fertigungspaket aus der committeten
daughtercard.kicad_pcb nach hardware/daughtercard/manufacturing/:

    manufacturing/
      gerber/                    alle Gerber + Excellon-Bohrdatei
      daughtercard-gerbers.zip   dieselben Dateien gezippt (Upload)
      BOM.csv                    Bestueckliste, JLCPCB-SMT-Format
      CPL.csv                    Bestueckungsplan (Pick&Place), JLCPCB-Format
      README.md                  Hinweise zur Bestellung

    /usr/bin/python3 tools/gen_manufacturing.py

BOM/CPL enthalten nur die maschinell bestueckten SMD-Teile. **J1-J6 werden von
Hand geloetet** und stehen deshalb nicht darin (nur im README als Handloet-Liste).
DNP (Q3, R10), Loetjumper, Testpunkte und Bohrungen sind ebenfalls ausgenommen.
"""
from __future__ import annotations

import csv
import io
import re
import subprocess
import sys
import zipfile
from pathlib import Path
from shutil import which

REPO = Path(__file__).resolve().parent.parent
PROJ = REPO / "hardware" / "daughtercard"
PCB = PROJ / "daughtercard.kicad_pcb"
OUT = PROJ / "manufacturing"
GERBER = OUT / "gerber"

sys.path.insert(0, str(REPO / "tools"))
import gen_daughtercard_sch as sch_gen  # noqa: E402
import gen_daughtercard_pcb as gp       # noqa: E402  (_assembled, _csv_field, LCSC ...)

# Gerber-Lagen fuer eine 2-lagige Platine mit Bestueckungsdruck beidseitig.
GERBER_LAYERS = ",".join([
    "F.Cu", "B.Cu",
    "F.Paste", "B.Paste",
    "F.Silkscreen", "B.Silkscreen",
    "F.Mask", "B.Mask",
    "Edge.Cuts",
])


def _cli():
    base = ["kicad-cli"]
    return ["xvfb-run", "-a", *base] if which("xvfb-run") else base


def export_gerbers() -> None:
    GERBER.mkdir(parents=True, exist_ok=True)
    for f in GERBER.iterdir():
        f.unlink()
    subprocess.run([*_cli(), "pcb", "export", "gerbers",
                    "--layers", GERBER_LAYERS,
                    "--no-protel-ext",           # KiCad-Endungen (.gbr) -- JLC versteht beide
                    "--precision", "6",
                    "-o", str(GERBER) + "/", str(PCB)],
                   check=True, capture_output=True)
    subprocess.run([*_cli(), "pcb", "export", "drill",
                    "--format", "excellon", "--excellon-units", "mm",
                    "--drill-origin", "absolute",
                    "--excellon-zeros-format", "decimal",
                    "--generate-map", "--map-format", "gerberx2",
                    "-o", str(GERBER) + "/", str(PCB)],
                   check=True, capture_output=True)
    files = sorted(p.name for p in GERBER.iterdir())
    print(f"gerber/ : {len(files)} Dateien")
    return files


def zip_gerbers(files) -> None:
    zp = OUT / "daughtercard-gerbers.zip"
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
        for name in files:
            z.write(GERBER / name, name)
    print(f"{zp.name} : {zp.stat().st_size // 1024} KB")


def export_bom_cpl():
    with_tmp = Path(__import__("tempfile").mkdtemp())
    pos = with_tmp / "pos.csv"
    subprocess.run([*_cli(), "pcb", "export", "pos", "--format", "csv",
                    "--units", "mm", "--side", "both", "--exclude-dnp",
                    "-o", str(pos), str(PCB)], check=True, capture_output=True)
    rows = list(csv.DictReader(io.StringIO(pos.read_text(encoding="utf-8"))))

    cpl = ["Designator,Mid X,Mid Y,Layer,Rotation"]
    placed: list[str] = []
    for r in rows:
        ref = r["Ref"]
        if not gp._assembled(ref):
            continue
        layer = "Top" if r["Side"].lower().startswith("t") else "Bottom"
        cpl.append(f'{ref},{float(r["PosX"]):.4f}mm,{float(r["PosY"]):.4f}mm,'
                   f'{layer},{float(r["Rot"]):.4f}')
        placed.append(ref)
    (OUT / "CPL.csv").write_text("\n".join(cpl) + "\n", encoding="utf-8")

    groups: dict[tuple[str, str, str], list[str]] = {}
    for ref in sorted(placed, key=lambda s: (s[0], int(re.sub(r"\D", "", s) or 0))):
        _, value, _ = sch_gen.COMPONENTS[ref]
        fp_short = sch_gen.FOOTPRINTS[ref].split(":", 1)[1]
        lcsc = sch_gen.LCSC.get(ref, "")
        groups.setdefault((value, fp_short, lcsc), []).append(ref)

    bom = ["Comment,Designator,Footprint,LCSC Part #"]
    missing: list[str] = []
    for (value, fp_short, lcsc), refs in sorted(groups.items()):
        bom.append(",".join(gp._csv_field(x) for x in
                            (value, " ".join(refs), fp_short, lcsc)))
        if not lcsc:
            missing.extend(refs)
    (OUT / "BOM.csv").write_text("\n".join(bom) + "\n", encoding="utf-8")

    print(f"BOM.csv : {len(groups)} Positionen ({len(placed)} SMD-Bauteile)")
    print(f"CPL.csv : {len(placed)} Platzierungen")
    return placed, missing


def hand_solder_rows():
    out = []
    for ref, (lib, value, dnp) in sorted(sch_gen.COMPONENTS.items()):
        if not re.match(r"J\d+$", ref):
            continue
        out.append((ref, value, sch_gen.FOOTPRINTS[ref].split(":", 1)[1]))
    return out


def write_readme(placed, missing) -> None:
    hs = hand_solder_rows()
    lines = [
        "# Fertigungspaket Daughter Card",
        "",
        "Erzeugt von `tools/gen_manufacturing.py` aus der committeten",
        "`daughtercard.kicad_pcb`. Bei jeder Layoutaenderung neu erzeugen.",
        "",
        "| Datei | Zweck |",
        "|---|---|",
        "| `daughtercard-gerbers.zip` | Gerber + Excellon-Bohrdatei, komplett. Bei JLCPCB hochladen. |",
        "| `gerber/` | dieselben Dateien einzeln (Kontrolle im Gerber-Viewer) |",
        "| `BOM.csv` | Bestueckliste fuer den SMT-Dienst (JLCPCB-Format) |",
        "| `CPL.csv` | Bestueckungsplan / Pick&Place (JLCPCB-Format) |",
        "",
        "## Platine",
        "",
        "- 74 x 60 mm, 2 Lagen, 1,6 mm FR4, 35 um Kupfer.",
        "- **Loetstoppmaske schwarz, Bestueckungsdruck weiss** "
        "(im Lagenaufbau gesetzt; bei JLCPCB die passende Option waehlen).",
        "- Oberflaeche: HASL bleifrei genuegt.",
        "- Maker-Kennzeichnung (GitHub-Marke + \"TenOfNine\") auf der Rueckseiten-Silkscreen.",
        "",
        "## SMT-Bestueckung",
        "",
        f"`BOM.csv` / `CPL.csv` enthalten **{len(placed)} SMD-Bauteile**, alle auf der",
        "Oberseite. Nicht enthalten: DNP (Q3, R10), Loetjumper JP1-JP3, Testpunkte",
        "TP1-TP7, Bohrungen -- und **die Steckverbinder J1-J6, die von Hand geloetet",
        "werden**.",
        "",
        "> Die `CPL.csv`-Drehungen kommen unveraendert aus KiCad. JLCPCB rechnet",
        "> fuer manche Gehaeuse (SOT-23, SOIC, LED) eine eigene Referenzdrehung an.",
        "> Im JLC-Vorschaufenster **jedes SOT-23 (Q1, Q2, D1-D3), U1, U2 und die",
        "> LED D4 einzeln auf Polaritaet/Pin-1 pruefen** und die Drehung dort",
        "> korrigieren, nicht in der CSV.",
        "",
    ]
    if missing:
        lines += [
            "### Vor der Bestellung LCSC-Nummer nachtragen",
            "",
            "Folgende Positionen haben in `BOM.csv` noch keine LCSC-Nummer und",
            "muessen im JLCPCB-Warenkorb zugeordnet werden:",
            "",
        ]
        _desc = {"D1": "BAT54S", "D2": "BAT54S", "D3": "BAT54S",
                 "Q1": "BSS84 (P-MOSFET)", "D4": "LED gruen 0805"}
        lines += [f"- **{r}** -- {_desc.get(r, sch_gen.COMPONENTS[r][1])}" for r in missing]
        lines += ["",
                  "Hintergrund und Kandidaten: `docs/jlc-bestueckung.md` (D-4 fuer Q1,",
                  "Abschnitt 2 fuer D1-D3 / D4).", ""]

    lines += [
        "## Von Hand zu loeten (nicht im SMT-Auftrag)",
        "",
        "| Ref | Wert | Footprint | Referenzteil |",
        "|---|---|---|---|",
    ]
    refparts = {
        "J1": "Buchsenleiste 2x5, 2,54 mm, gerade (BKL 10120960)",
        "J2": "Wannenstecker 2x5, 2,54 mm, gerade",
        "J3": "Wannenstecker 2x5, 2,54 mm, gerade",
        "J4": "Schraubklemme 2-polig, RM 5,08 mm",
        "J5": "Schraubklemme 2-polig, RM 5,08 mm",
        "J6": "Stiftleiste 1x3, 2,54 mm",
    }
    for ref, value, fp in hs:
        lines.append(f"| {ref} | {value} | `{fp}` | {refparts.get(ref, '')} |")
    lines += [
        "",
        "> ⚠️ **J1 ist nicht kodiert** und fuehrt an Pin 2/4 die 42 V~. Vor dem",
        "> ersten Einschalten die Drehlage und den Durchgang gegen die",
        "> Anzeigenplatine pruefen -- siehe `docs/pruefpunkte-j1-buchsenleiste.md`",
        "> und Schaltplan Kapitel 4.1 / Pruefliste 9.",
        "",
        "## Netzklassen im Layout",
        "",
        "Alle Leiterbahnen 0,5 mm (an SOIC-Pins auf 0,375 mm verjuengt), nur",
        "AC1/AC2 = 1,5 mm. GND ueber die Masseflaeche auf beiden Lagen. Zum",
        "Spannungsabfall der +5V-Kette bei 0,5 mm siehe `docs/jlc-bestueckung.md` D-5.",
    ]
    (OUT / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("README.md geschrieben")


def main() -> int:
    if not PCB.is_file():
        sys.exit(f"{PCB} fehlt")
    OUT.mkdir(parents=True, exist_ok=True)
    files = export_gerbers()
    zip_gerbers(files)
    placed, missing = export_bom_cpl()
    write_readme(placed, missing)
    print(f"\nFertigungspaket: {OUT.relative_to(REPO)}")
    if missing:
        print(f"[!] BOM: LCSC-Nummer fehlt fuer {', '.join(missing)} "
              f"(im JLC-Warenkorb nachtragen)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
