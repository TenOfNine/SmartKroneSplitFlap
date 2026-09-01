#!/usr/bin/env python3
"""Erzeugt das Fertigungspaket der Zentralsteuerung aus der committeten
hardware/master/master.kicad_pcb nach hardware/master/manufacturing/:

    manufacturing/
      gerber/                 alle Gerber + Excellon-Bohrdatei
      master-gerbers.zip      dieselben Dateien gezippt (Upload)
      BOM.csv                 Bestueckliste (JLCPCB-SMT-Format)
      CPL.csv                 Bestueckungsplan (Pick&Place), JLCPCB-Format
      README.md               Hinweise zur Bestellung

    /usr/bin/python3 tools/gen_master_manufacturing.py [--no-gerber]

Der Master ist ein Einzelstueck. BOM/CPL enthalten die SMD-Kleinteile; falls
gewuenscht kann JLCPCB sie bestuecken. **Von Hand geloetet werden** die
Steckverbinder J1-J4, die Buchsenleisten fuer das ESP32-C3-Modul (U1) und die
Loetbruecke JP1. Handbestueckung der gesamten Platine ist ebenfalls zumutbar
(alle SMD in 0805 / SOIC / SOT-23).
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
PROJ = REPO / "hardware" / "master"
PCB = PROJ / "master.kicad_pcb"
OUT = PROJ / "manufacturing"
GERBER = OUT / "gerber"

sys.path.insert(0, str(REPO / "tools"))
import gen_master_sch as sch_gen   # noqa: E402
import gen_master_pcb as gp        # noqa: E402  (_assembled, _csv_field, LCSC ...)

GERBER_LAYERS = ",".join([
    "F.Cu", "B.Cu", "F.Paste", "B.Paste", "F.Silkscreen", "B.Silkscreen",
    "F.Mask", "B.Mask", "Edge.Cuts",
])


def _cli():
    return (["xvfb-run", "-a", "kicad-cli"] if which("xvfb-run") else ["kicad-cli"])


def export_gerbers():
    GERBER.mkdir(parents=True, exist_ok=True)
    for f in GERBER.iterdir():
        f.unlink()
    subprocess.run([*_cli(), "pcb", "export", "gerbers", "--layers", GERBER_LAYERS,
                    "--no-protel-ext", "--precision", "6",
                    "-o", str(GERBER) + "/", str(PCB)], check=True, capture_output=True)
    subprocess.run([*_cli(), "pcb", "export", "drill", "--format", "excellon",
                    "--excellon-units", "mm", "--drill-origin", "absolute",
                    "--excellon-zeros-format", "decimal", "--generate-map",
                    "--map-format", "gerberx2", "-o", str(GERBER) + "/", str(PCB)],
                   check=True, capture_output=True)
    files = sorted(p.name for p in GERBER.iterdir())
    print(f"gerber/ : {len(files)} Dateien")
    return files


def zip_gerbers(files):
    zp = OUT / "master-gerbers.zip"
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
        for name in files:
            z.write(GERBER / name, name)
    print(f"{zp.name} : {zp.stat().st_size // 1024} KB")


def export_bom_cpl():
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        pos = Path(td) / "pos.csv"
        subprocess.run([*_cli(), "pcb", "export", "pos", "--format", "csv", "--units", "mm",
                        "--side", "both", "--exclude-dnp", "-o", str(pos), str(PCB)],
                       check=True, capture_output=True)
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
        bom.append(",".join(gp._csv_field(x) for x in (value, " ".join(refs), fp_short, lcsc)))
        if not lcsc:
            missing.extend(refs)
    (OUT / "BOM.csv").write_text("\n".join(bom) + "\n", encoding="utf-8")

    print(f"BOM.csv : {len(groups)} Positionen ({len(placed)} SMD-Bauteile)")
    print(f"CPL.csv : {len(placed)} Platzierungen")
    return placed, missing


_HAND = {
    "J1": "Schraubklemme 2-polig, RM 5,08 mm (+5V IN)",
    "J2": "Wannenstecker 2x5, 2,54 mm, gerade (Bus zur ersten Daughter Card)",
    "J3": "Stiftleiste 1x4, 2,54 mm (Reserve GPIO0/1/7 + GND)",
    "J4": "Aufwaertswandler-Modul 5 V -> 15 V (nur falls O-2 Deutung 1), DNP",
    "JP1": "Loetbruecke 3-Wege: Ader 9 = offen / +5V / +15V",
    "U1": "ESP32-C3 Super Mini, gesteckt in 2x 1x8 Buchsenleisten 2,54 mm",
}


def write_readme(placed, missing):
    lines = [
        "# Fertigungspaket Zentralsteuerung (Master)",
        "",
        "Erzeugt von `tools/gen_master_manufacturing.py` aus der committeten",
        "`master.kicad_pcb`. Bei jeder Layoutaenderung neu erzeugen.",
        "",
        "| Datei | Zweck |",
        "|---|---|",
        "| `master-gerbers.zip` | Gerber + Excellon-Bohrdatei, komplett. Bei JLCPCB hochladen. |",
        "| `gerber/` | dieselben Dateien einzeln (Kontrolle im Gerber-Viewer) |",
        "| `BOM.csv` | Bestueckliste fuer den SMT-Dienst (JLCPCB-Format) |",
        "| `CPL.csv` | Bestueckungsplan / Pick&Place (JLCPCB-Format) |",
        "",
        "## Platine",
        "",
        f"- {gp.BOARD_W:g} x {gp.BOARD_H:g} mm, 2 Lagen, 1,6 mm FR4, 35 um Kupfer.",
        "- **Loetstoppmaske schwarz, Bestueckungsdruck weiss** (im Lagenaufbau gesetzt;",
        "  bei JLCPCB die Option \"Black solder mask, White silkscreen\" waehlen).",
        "- Oberflaeche: HASL bleifrei genuegt.",
        "- Maker-Kennzeichnung (GitHub-Marke + \"TenOfNine\") auf der Rueckseiten-Silkscreen.",
        "- **Antennenbereich unter U1** (Unterkante des Moduls): Massefläche ist dort",
        "  ausgespart (Footprint-Keepout). Wenn moeglich das Modul so einbauen, dass",
        "  die Antenne ueber die Platinenkante ragt.",
        "",
        "## Bestueckung",
        "",
        f"`BOM.csv` / `CPL.csv` enthalten **{len(placed)} SMD-Bauteile** (alle Oberseite,",
        "0805 / SOIC-8 / SOT-23-5). Der Master ist ein Einzelstueck -- die komplette",
        "Handbestueckung ist zumutbar; JLCPCB-SMT ist optional.",
        "",
        "**Immer von Hand** zu bestuecken (nicht im SMT-Auftrag):",
        "",
        "| Ref | Teil |",
        "|---|---|",
    ]
    for ref, desc in _HAND.items():
        lines.append(f"| {ref} | {desc} |")
    lines += [
        "",
        "> Die `CPL.csv`-Drehungen kommen unveraendert aus KiCad. JLCPCB rechnet",
        "> fuer SOT-23 / SOIC / LED eine eigene Referenzdrehung an -- im",
        "> JLC-Vorschaufenster **U2, U3 und D1 einzeln auf Pin 1 / Polaritaet pruefen**.",
        "",
    ]
    if missing:
        lines += [
            "### BOM-Positionen ohne LCSC-Nummer",
            "",
            "Fuer folgende Positionen ist keine gepruefte LCSC-Nummer hinterlegt",
            "(Projektregel 1). Im JLCPCB-Warenkorb zuordnen oder von Hand bestuecken:",
            "",
        ]
        lines += [f"- **{r}** -- {sch_gen.COMPONENTS[r][1]} ({sch_gen.FOOTPRINTS[r].split(':', 1)[1]})"
                  for r in missing]
        lines += [""]

    lines += [
        "## Bauteil-Hinweise",
        "",
        "- **U1 ESP32-C3 Super Mini:** Aftermarket-Modul, Pinbelegung + Einbaulage in",
        "  `docs/symbolpruefung-master.md`. USB-C/BOOT/RST an der Oberkante, Antenne",
        "  (Aufdruck \"ESP32-C3 Super Mini\") an der Unterkante. 5V-Pin = rechts oben.",
        "- **U3 74LVC1G17** hebt CHAIN von 3,3 V auf 5 V. Zeigt der Test, dass 3,3 V",
        "  direkt genuegen (M-3), wird U3 weggelassen und **R7 (0 Ohm)** bestueckt.",
        "- **J4 / Step-up** bleibt unbestueckt, bis O-2 zeigt, dass die Anzeige an",
        "  Pin 9 mehr als 5 V braucht. Dann Modul auf 15,0 V einstellen, JP1 auf +15V.",
        "- **JP1** bei Auslieferung offen (Ader 9 unbeschaltet).",
    ]
    (OUT / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("README.md geschrieben")


def main() -> int:
    if not PCB.is_file():
        sys.exit(f"{PCB} fehlt")
    OUT.mkdir(parents=True, exist_ok=True)
    no_gerber = "--no-gerber" in sys.argv
    if no_gerber:
        print("--no-gerber: nur BOM/CPL/README")
    else:
        files = export_gerbers()
        zip_gerbers(files)
    placed, missing = export_bom_cpl()
    write_readme(placed, missing)
    print(f"\nFertigungspaket: {OUT.relative_to(REPO)}")
    if missing:
        print(f"[!] BOM: LCSC-Nummer fehlt fuer {', '.join(missing)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
