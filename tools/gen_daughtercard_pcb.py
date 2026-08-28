#!/usr/bin/env python3
"""Erzeugt hardware/daughtercard/daughtercard.kicad_pcb mit vorplatzierten
Bauteilen. Backlog-Nachtrag: Layout-Vorbereitung.

Alle Bauteile liegen bereits mit ihrem Footprint, ihrem Netz und einer groben,
nach Funktionsbloecken gruppierten Position auf der Platine. Im PCB-Editor sind
nur noch Feinkorrekturen und das Routen noetig. Die Platzierung folgt
docs/layout-daughtercard.md (AC-Zone unten, Logik Mitte, Bus oben).

Laeuft mit dem System-Python (pcbnew), nicht in der venv:

    /usr/bin/python3 tools/gen_daughtercard_pcb.py [--drc]

Netz- und Footprintdaten kommen aus der von kicad-cli erzeugten Netzliste des
committeten Schaltplans; die Bauteil-UUIDs aus der .kicad_sch, damit
"Update PCB from Schematic" die Footprints ohne Warnung wiederfindet.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

try:
    import pcbnew
except ImportError:
    sys.exit("pcbnew nicht gefunden - dieses Skript mit /usr/bin/python3 starten "
             "(KiCad-9-Paket installiert?).")

REPO = Path(__file__).resolve().parent.parent
PROJ = REPO / "hardware" / "daughtercard"
SCH = PROJ / "daughtercard.kicad_sch"
PCB = PROJ / "daughtercard.kicad_pcb"
NET = PROJ / "daughtercard.net"

FP_ROOT = Path("/usr/share/kicad/footprints")

sys.path.insert(0, str(REPO / "tools"))
import gen_daughtercard_sch as sch_gen  # noqa: E402  (nur Datenstrukturen)

FOOTPRINTS = sch_gen.FOOTPRINTS

# Platinenmass aus docs/schaltplan-daughtercard.md 8.1
BOARD_W, BOARD_H = 74.0, 60.0
HOLE_INSET = 4.0

def _grid(refs, x0, y0, cols, dx, dy, rot=0):
    """Kleines Raster; liefert {ref: (x, y, rot)}."""
    out = {}
    for i, r in enumerate(refs):
        out[r] = (x0 + (i % cols) * dx, y0 + (i // cols) * dy, rot)
    return out


# Grobe Platzierung, nach Funktionsbloecken. Ursprung oben links, y nach unten.
# Bewusst grosszuegig, damit keine Courtyards ueberlappen -- im PCB-Editor nur
# noch verschieben und routen. Zonen nach docs/layout-daughtercard.md.
# Die Wannenstecker J1..J3 liegen um 90 Grad gedreht laengs der Platinenkante.
PLACEMENT: dict[str, tuple[float, float, float]] = {
    # --- Bus-Zone (Oberkante) ---
    # Wannenstecker: Courtyard 21,4 mm breit; die Eckbohrungen und die
    # Mittelbauteile bleiben eng -- im PCB-Editor R16/JP3/R14/C2/TP6 ein paar
    # Millimeter aus dem J2/J3-Bereich schieben.
    "J2": (17, 10, 90),
    "J3": (53, 10, 90),
    "U2": (41, 9, 0),
    "C2": (46, 13, 90),
    "R16": (31, 6, 90),
    "JP3": (34, 6, 90),
    "R14": (46, 5, 0),
    "TP6": (37, 16, 0),
    # --- Versorgung (links) ---
    "F1": (10, 16, 90),
    "C3": (14, 16, 90),
    "C1": (46, 25, 90),
    # --- MCU ---
    "U1": (35, 31, 0),
    # --- Impulseingaenge (links): 3 Kanaele x (Pull-up, Serie, C, Diode) ---
    **_grid(["R1", "R2", "C4", "D1"], 8, 26, 4, 5, 0),
    **_grid(["R3", "R4", "C5", "D2"], 8, 33, 4, 5, 0),
    **_grid(["R5", "R6", "C6", "D3"], 8, 40, 4, 5, 0),
    "TP1": (4, 15, 0),
    "TP2": (4, 19, 0),
    # --- Triac-Ansteuerung (rechts der Mitte) ---
    **_grid(["R8", "R7", "R9", "R10"], 50, 20, 4, 5, 0),
    **_grid(["Q2", "Q1", "Q3"], 50, 27, 3, 6, 0),
    "JP1": (51, 33, 0),
    "JP2": (56, 33, 0),
    # --- CHAIN / Enumeration ---
    **_grid(["R11", "R12", "R13"], 50, 39, 3, 5, 0),
    "TP5": (65, 39, 0),
    # --- Status-LED / UPDI (rechte Kante) ---
    "R15": (64, 15, 0),
    "D4": (69, 15, 0),
    "J6": (66, 24, 90),
    # --- AC-Zone (Unterkante) ---
    "J4": (12, 48, 0),
    "J1": (37, 48, 90),
    "J5": (58, 48, 0),
    # --- Testpunkte an J1 ---
    "TP3": (27, 39, 0),
    "TP7": (32, 39, 0),
    "TP4": (44, 39, 0),
}

# Netzklassen nach docs/schaltplan-daughtercard.md 8.2. Nach dem Speichern in die
# .kicad_pro geschrieben (pcbnew serialisiert nur die Default-Klasse selbst).
# AC-Klasse: 1,0 mm Bahn. Der Mindestabstand von 2,0 mm zu Logiknetzen
# (Schaltplan 8.2) laesst sich am Stecker nicht einhalten (Pin-Raster 2,54 mm)
# und bleibt eine Routing-Vorgabe in docs/layout-daughtercard.md, keine
# DRC-Regel -- daher hier nur der normale Abstand.
NETCLASSES = [
    {"name": "Default", "clearance": 0.2, "track_width": 0.25},
    {"name": "AC", "clearance": 0.3, "track_width": 1.0},
    {"name": "RS485", "clearance": 0.2, "track_width": 0.3, "diff_pair_gap": 0.3},
    {"name": "Power", "clearance": 0.2, "track_width": 0.5},
]
NETCLASS_PATTERNS = [
    {"netclass": "AC", "pattern": "/AC?"},
    {"netclass": "RS485", "pattern": "/RS485_?"},
    {"netclass": "Power", "pattern": "/+5V"},
    {"netclass": "Power", "pattern": "/+5V_IN"},
    {"netclass": "Power", "pattern": "/+15V"},
    {"netclass": "Power", "pattern": "/GND"},
    {"netclass": "Power", "pattern": "/VSENS"},
    {"netclass": "Power", "pattern": "/VDRV"},
]


def patch_project_netclasses() -> None:
    """Schreibt NETCLASSES/NETCLASS_PATTERNS in die .kicad_pro. pcbnew.SaveBoard
    legt dort nur die Default-Klasse an."""
    import json

    pro_path = PROJ / "daughtercard.kicad_pro"
    pro = json.loads(pro_path.read_text(encoding="utf-8"))
    ns = pro.setdefault("net_settings", {})
    full = []
    for c in NETCLASSES:
        e = {
            "bus_width": 12, "clearance": 0.2, "diff_pair_gap": 0.25,
            "diff_pair_via_gap": 0.25, "diff_pair_width": c["track_width"],
            "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1,
            "name": c["name"], "pcb_color": "rgba(0, 0, 0, 0.000)", "priority": 0,
            "schematic_color": "rgba(0, 0, 0, 0.000)", "track_width": 0.25,
            "via_diameter": 0.6, "via_drill": 0.3, "wire_width": 6,
        }
        e.update(c)
        full.append(e)
    ns["classes"] = full
    ns["netclass_patterns"] = NETCLASS_PATTERNS
    ns.setdefault("meta", {"version": 4})
    pro_path.write_text(json.dumps(pro, indent=2) + "\n", encoding="utf-8")


def run_netlist() -> str:
    NET.parent.mkdir(parents=True, exist_ok=True)
    cli = ["kicad-cli"]
    if _have("xvfb-run"):
        cli = ["xvfb-run", "-a", *cli]
    subprocess.run([*cli, "sch", "export", "netlist", "--format", "kicadsexpr",
                    "-o", str(NET), str(SCH)], check=True,
                   capture_output=True)
    return NET.read_text(encoding="utf-8")


def _have(prog: str) -> bool:
    from shutil import which
    return which(prog) is not None


def parse_netlist(text: str) -> tuple[list[str], dict[str, list[tuple[str, str]]]]:
    comps = re.findall(r'\(comp\s+\(ref "([^"]+)"\)', text)
    nets: dict[str, list[tuple[str, str]]] = {}
    # An jedem "(net (code ..." trennen; der Abschnitt reicht bis zum naechsten.
    chunks = re.split(r'\(net \(code "\d+"\) \(name "', text)[1:]
    for chunk in chunks:
        name = chunk[: chunk.index('"')]
        nodes = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', chunk)
        nets[name] = nodes
    return comps, nets


def parse_sch_uuids(text: str) -> dict[str, str]:
    """ref -> Symbol-UUID (fuer den footprint-path, damit KiCad die Zuordnung
    ueber die Schaltplansymbole findet)."""
    out: dict[str, str] = {}
    for blk in re.finditer(r'\(symbol\s+(?:\(lib_id[^\n]*\n)?.*?\(uuid "([0-9a-f-]{36})"\).*?'
                           r'\(property "Reference" "([^"]+)"', text, re.S):
        uuid, ref = blk.group(1), blk.group(2)
        if ref not in out and not ref.startswith("#"):
            out[ref] = uuid
    return out


def fp_dir_and_name(fpid: str) -> tuple[Path, str]:
    lib, name = fpid.split(":", 1)
    return FP_ROOT / f"{lib}.pretty", name


def mm(v: float) -> int:
    return pcbnew.FromMM(v)


def build() -> pcbnew.BOARD:
    text = run_netlist()
    comps, nets = parse_netlist(text)
    uuids = parse_sch_uuids(SCH.read_text(encoding="utf-8"))

    board = pcbnew.BOARD()

    ds = board.GetDesignSettings()
    ds.SetBoardThickness(mm(1.6))
    board.SetCopperLayerCount(2)

    # Netze anlegen.
    netinfo: dict[str, pcbnew.NETINFO_ITEM] = {}
    for name in nets:
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        netinfo[name] = ni

    # ref -> (netname, pin) fuer die Padzuweisung.
    pin_net: dict[tuple[str, str], str] = {}
    for name, nodes in nets.items():
        for ref, pin in nodes:
            pin_net[(ref, pin)] = name

    missing_place = []
    for ref in comps:
        if ref.startswith("#"):
            continue  # PWR_FLAG u. ae. - kein Footprint
        fpid = FOOTPRINTS.get(ref)
        if fpid is None:
            raise SystemExit(f"{ref}: kein Footprint in FOOTPRINTS")
        d, n = fp_dir_and_name(fpid)
        fp = pcbnew.FootprintLoad(str(d), n)
        if fp is None:
            raise SystemExit(f"{ref}: Footprint {fpid} nicht ladbar")
        board.Add(fp)
        fp.SetReference(ref)
        _, value, dnp = sch_gen.COMPONENTS[ref]
        fp.SetValue(value)
        if dnp:
            fp.SetDNP(True)
            fp.SetExcludedFromBOM(True)
        if ref in uuids:
            fp.SetPath(pcbnew.KIID_PATH("/" + uuids[ref]))

        x, y, rot = PLACEMENT.get(ref, (None, None, 0))
        if x is None:
            missing_place.append(ref)
            x, y = 80.0, 5.0 + 3.0 * len(missing_place)  # neben der Platine ablegen
        fp.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        if rot:
            fp.SetOrientationDegrees(rot)

        for pad in fp.Pads():
            key = (ref, pad.GetNumber())
            if key in pin_net:
                pad.SetNet(netinfo[pin_net[key]])

    # Platinenumriss.
    rect = pcbnew.PCB_SHAPE(board)
    rect.SetShape(pcbnew.SHAPE_T_RECT)
    rect.SetLayer(pcbnew.Edge_Cuts)
    rect.SetStart(pcbnew.VECTOR2I(0, 0))
    rect.SetEnd(pcbnew.VECTOR2I(mm(BOARD_W), mm(BOARD_H)))
    rect.SetWidth(mm(0.15))
    board.Add(rect)

    # Befestigungsbohrungen 3,2 mm, 4 mm von den Ecken.
    hole_d, hole_n = FP_ROOT / "MountingHole.pretty", "MountingHole_3.2mm_M3"
    for i, (hx, hy) in enumerate([
        (HOLE_INSET, HOLE_INSET), (BOARD_W - HOLE_INSET, HOLE_INSET),
        (HOLE_INSET, BOARD_H - HOLE_INSET), (BOARD_W - HOLE_INSET, BOARD_H - HOLE_INSET),
    ], start=1):
        h = pcbnew.FootprintLoad(str(hole_d), hole_n)
        board.Add(h)
        h.SetReference(f"H{i}")
        h.SetPosition(pcbnew.VECTOR2I(mm(hx), mm(hy)))
        h.SetExcludedFromBOM(True)

    if missing_place:
        print(f"[!] ohne Platzierung, neben die Platine gelegt: {missing_place}",
              file=sys.stderr)

    return board


def render_png() -> None:
    from shutil import which
    if not (which("pdftoppm") and which("kicad-cli")):
        return
    import subprocess
    import tempfile
    cli = ["xvfb-run", "-a", "kicad-cli"] if which("xvfb-run") else ["kicad-cli"]
    with tempfile.TemporaryDirectory() as td:
        pdf = Path(td) / "pcb.pdf"
        subprocess.run([*cli, "pcb", "export", "pdf",
                        "--layers", "F.Cu,F.Silkscreen,F.Fab,Edge.Cuts",
                        "-o", str(pdf), str(PCB)], check=True, capture_output=True)
        out = REPO / "docs" / "pcb-daughtercard.png"
        subprocess.run(["pdftoppm", "-png", "-r", "300", "-singlefile",
                        "-x", "0", "-y", "0", "-W", str(int(BOARD_W * 300 / 25.4) + 60),
                        "-H", str(int(BOARD_H * 300 / 25.4) + 60),
                        str(pdf), str(out.with_suffix(""))], check=True)
        print(f"PNG: {out.relative_to(REPO)}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--drc", action="store_true", help="nach dem Schreiben DRC laufen lassen")
    ap.add_argument("--png", action="store_true", help="Vorschau docs/pcb-daughtercard.png")
    args = ap.parse_args()

    board = build()
    pcbnew.SaveBoard(str(PCB), board)
    patch_project_netclasses()
    n = sum(1 for f in board.GetFootprints() if not f.GetReference().startswith("H"))
    print(f"geschrieben: {PCB.relative_to(REPO)}  ({n} Bauteile + 4 Bohrungen, "
          f"{BOARD_W:g} x {BOARD_H:g} mm)")

    if args.png:
        render_png()

    if args.drc:
        cli = ["kicad-cli"]
        if _have("xvfb-run"):
            cli = ["xvfb-run", "-a", *cli]
        rep = REPO / "docs" / "drc-daughtercard.rpt"
        r = subprocess.run([*cli, "pcb", "drc", "--exit-code-violations",
                            "--severity-error", "-o", str(rep), str(PCB)])
        print(f"DRC: {'keine Fehler' if r.returncode == 0 else f'siehe {rep.name}'} "
              f"(unverdrahtete Netze sind erwartbar)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
