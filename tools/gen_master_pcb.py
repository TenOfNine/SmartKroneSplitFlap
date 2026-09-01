#!/usr/bin/env python3
"""Erzeugt hardware/master/master.kicad_pcb mit vorplatzierten Bauteilen.
Backlog T11, analog zu tools/gen_daughtercard_pcb.py.

Alle Bauteile liegen mit Footprint, Netz und grober Position auf der Platine.
Im PCB-Editor sind nur noch Feinkorrekturen und das Routen noetig (bzw.
tools/route_master.py). Platzierung folgt docs/layout-master.md.

Laeuft mit dem System-Python (pcbnew), nicht in der venv:

    /usr/bin/python3 tools/gen_master_pcb.py [--drc] [--png] [--jlc]
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
    sys.exit("pcbnew nicht gefunden - dieses Skript mit /usr/bin/python3 starten.")

REPO = Path(__file__).resolve().parent.parent
PROJ = REPO / "hardware" / "master"
SCH = PROJ / "master.kicad_sch"
PCB = PROJ / "master.kicad_pcb"
NET = PROJ / "master.net"

FP_ROOT = Path("/usr/share/kicad/footprints")
LOCAL_FP = {
    "modules": PROJ / "footprints" / "modules.pretty",
    "logos": PROJ / "footprints" / "logos.pretty",
}

sys.path.insert(0, str(REPO / "tools"))
import gen_master_sch as sch_gen  # noqa: E402  (nur Datenstrukturen)

FOOTPRINTS = sch_gen.FOOTPRINTS
LCSC = sch_gen.LCSC
JLC_DIR = PROJ / "jlc"  # Bauartefakt, .gitignore

BOARD_W, BOARD_H = 68.0, 54.0
HOLE_INSET = 4.0

# Grobe Platzierung, nach Funktionsbloecken. Ursprung oben links, y nach unten.
# U1 (ESP32-C3) belegt das linke Drittel: Koerper x ~9..27, USB-C ueberragt die
# Oberkante, Antenne + Cu-Keepout an der Unterkante (y ~21..27). Alle uebrigen
# Bauteile liegen rechts davon bzw. unterhalb. docs/layout-master.md.
PLACEMENT: dict[str, tuple[float, float, float]] = {
    # U1: Modulkoerper endet ~2,7 mm vor der Oberkante, USB-C-Buchse ragt knapp
    # daran. Fuer klaren Ueberstand im GUI eine kleine Edge.Cuts-Aussparung unter
    # der USB-C-Buchse einfuegen -- weiter hochsetzen sprengt den Routingkanal
    # (Leiterbahnen an der Oberkante). Siehe docs/layout-master.md.
    "U1": (18, 14, 0),
    "C4": (32, 6, 0),        # 100n direkt am 5V-Pin von U1
    # --- RS-485 (Mitte, nahe Bus) ---
    "U2": (40, 26, 0),
    "C1": (40, 21, 90),
    "C5": (44, 15, 0),       # 10u +3V3
    "R1": (33, 22, 90),      # Abschluss 120R
    "R2": (33, 26, 90),      # Bias A
    "R3": (33, 30, 90),      # Bias B
    "R4": (44, 30, 0),       # DE-Pulldown
    # --- CHAIN-Pegelwandler ---
    "U3": (34, 38, 0),
    "C2": (34, 34, 0),
    "R5": (40, 38, 0),
    "R7": (30, 42, 0),       # Bypass (DNP)
    # --- Versorgung 5V (Unterkante links) ---
    "J1": (11, 48, 0),       # Schraubklemme, Draehte nach unten/links
    "FB1": (24, 48, 90),
    "C3": (29, 48, 90),      # 47u Bulk
    # --- Status-LED (Unterkante Mitte, sichtbar) ---
    "R6": (37, 48, 0),
    "D1": (42, 48, 0),
    # --- Ader 9 / Step-up (rechts unten) ---
    "JP1": (44, 42, 0),
    "J4": (46, 50, 90),      # Boost-Modul (DNP), 1x4 quer an der Unterkante
    # --- Bus / Reserve ---
    "J2": (50, 9, 0),        # Wannenstecker 2x5, Flachband nach oben
    "J3": (62, 20, 0),       # Reserve-Header 1x4 senkrecht an der rechten Kante
    # --- Testpunkte (freies Feld Mitte-rechts) ---
    "TP1": (50, 30, 0), "TP2": (55, 30, 0), "TP3": (50, 35, 0),
    "TP4": (55, 35, 0), "TP5": (50, 40, 0), "TP6": (55, 40, 0),
    "TP7": (55, 44, 0),
}

# Netzklassen nach docs/schaltplan-master.md 8. Kein AC-Netz auf dem Master.
# Alle Signalbahnen 0,5 mm; Versorgung (+5V/+3V3/+15V/ADER9) 0,8 mm.
NETCLASSES = [
    {"name": "Default", "clearance": 0.2, "track_width": 0.5},
    {"name": "Power", "clearance": 0.2, "track_width": 0.8,
     "via_diameter": 0.8, "via_drill": 0.4},
    {"name": "GND", "clearance": 0.2, "track_width": 0.5},
]
NETCLASS_PATTERNS = [
    {"netclass": "Power", "pattern": "/+5V"},
    {"netclass": "Power", "pattern": "/+5V_IN"},
    {"netclass": "Power", "pattern": "/+3V3"},
    {"netclass": "Power", "pattern": "/+15V"},
    {"netclass": "Power", "pattern": "/ADER9"},
    {"netclass": "GND", "pattern": "/GND"},
]


def patch_project_netclasses() -> None:
    """Schreibt NETCLASSES/NETCLASS_PATTERNS in die .kicad_pro (pcbnew.SaveBoard
    legt dort nur die Default-Klasse an)."""
    import json

    pro_path = PROJ / "master.kicad_pro"
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


def _have(prog: str) -> bool:
    from shutil import which
    return which(prog) is not None


def run_netlist() -> str:
    NET.parent.mkdir(parents=True, exist_ok=True)
    cli = ["xvfb-run", "-a", "kicad-cli"] if _have("xvfb-run") else ["kicad-cli"]
    subprocess.run([*cli, "sch", "export", "netlist", "--format", "kicadsexpr",
                    "-o", str(NET), str(SCH)], check=True, capture_output=True)
    return NET.read_text(encoding="utf-8")


def parse_netlist(text: str):
    comps = re.findall(r'\(comp\s+\(ref "([^"]+)"\)', text)
    nets: dict[str, list[tuple[str, str]]] = {}
    chunks = re.split(r'\(net \(code "\d+"\) \(name "', text)[1:]
    for chunk in chunks:
        name = chunk[: chunk.index('"')]
        nodes = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', chunk)
        nets[name] = nodes
    return comps, nets


def parse_sch_uuids(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for blk in re.finditer(r'\(symbol\s+(?:\(lib_id[^\n]*\n)?.*?\(uuid "([0-9a-f-]{36})"\).*?'
                           r'\(property "Reference" "([^"]+)"', text, re.S):
        uuid, ref = blk.group(1), blk.group(2)
        if ref not in out and not ref.startswith("#"):
            out[ref] = uuid
    return out


def fp_dir_and_name(fpid: str) -> tuple[Path, str]:
    lib, name = fpid.split(":", 1)
    if lib in LOCAL_FP:
        return LOCAL_FP[lib], name
    return FP_ROOT / f"{lib}.pretty", name


def mm(v: float) -> int:
    return pcbnew.FromMM(v)


def build() -> "pcbnew.BOARD":
    text = run_netlist()
    comps, nets = parse_netlist(text)
    uuids = parse_sch_uuids(SCH.read_text(encoding="utf-8"))

    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(mm(1.6))
    board.SetCopperLayerCount(2)

    netinfo: dict[str, pcbnew.NETINFO_ITEM] = {}
    for name in nets:
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        netinfo[name] = ni

    pin_net: dict[tuple[str, str], str] = {}
    for name, nodes in nets.items():
        for ref, pin in nodes:
            pin_net[(ref, pin)] = name

    missing_place = []
    for ref in comps:
        if ref.startswith("#"):
            continue
        fpid = FOOTPRINTS.get(ref)
        if fpid is None:
            raise SystemExit(f"{ref}: kein Footprint in FOOTPRINTS")
        d, n = fp_dir_and_name(fpid)
        fp = pcbnew.FootprintLoad(str(d), n)
        if fp is None:
            raise SystemExit(f"{ref}: Footprint {fpid} nicht ladbar ({d})")
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
            x, y = 80.0, 5.0 + 3.0 * len(missing_place)
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
        h.Reference().SetVisible(False)   # randnah -> Silk-Warnung vermeiden

    if missing_place:
        print(f"[!] ohne Platzierung, neben die Platine gelegt: {missing_place}", file=sys.stderr)
    return board


def render_png() -> None:
    from shutil import which
    if not (which("pdftoppm") and which("kicad-cli")):
        return
    import tempfile
    cli = ["xvfb-run", "-a", "kicad-cli"] if which("xvfb-run") else ["kicad-cli"]

    with tempfile.TemporaryDirectory() as td:
        preview_pcb = Path(td) / "preview.kicad_pcb"
        b = pcbnew.LoadBoard(str(PCB))
        for z in list(b.Zones()):
            b.Delete(z)
        pcbnew.SaveBoard(str(preview_pcb), b)

        sides = [
            ("Oberseite (F.Cu)", "Edge.Cuts,F.Cu,F.Silkscreen,F.Fab", []),
            ("Unterseite (B.Cu, gespiegelt)", "Edge.Cuts,B.Cu,B.Silkscreen,B.Fab", ["--mirror"]),
        ]
        try:
            from PIL import Image, ImageDraw, ImageOps
        except ImportError:
            print("[!] Pillow fehlt -- keine Vorschau")
            return

        pics = []
        for _, layers, extra in sides:
            pdf = Path(td) / "s.pdf"
            subprocess.run([*cli, "pcb", "export", "pdf", "--mode-single",
                            "--layers", layers, *extra, "-o", str(pdf), str(preview_pcb)],
                           check=True, capture_output=True)
            raw = Path(td) / "s.png"
            subprocess.run(["pdftoppm", "-png", "-r", "500", "-singlefile",
                            str(pdf), str(raw.with_suffix(""))], check=True)
            im = Image.open(raw).convert("RGB")
            bbox = ImageOps.invert(im).getbbox()
            pics.append(im.crop(bbox) if bbox else im)

        h = max(p.height for p in pics)
        gap, pad, head = 60, 40, 46
        w = sum(p.width for p in pics) + gap + 2 * pad
        canvas = Image.new("RGB", (w, h + 2 * pad + head), "white")
        draw = ImageDraw.Draw(canvas)
        x = pad
        for (label, _, _), p in zip(sides, pics):
            canvas.paste(p, (x, pad + head))
            draw.text((x, pad + 12), label, fill="black")
            x += p.width + gap
        out = REPO / "docs" / "pcb-master.png"
        canvas.save(out)
        print(f"PNG: {out.relative_to(REPO)}  ({canvas.width}x{canvas.height})")


def render_3d() -> None:
    """3D-Ansicht (kicad-cli pcb render) von oben und unten -> docs/render-master-*.png.
    Fuer die Sichtpruefung der U1-Einbaulage (USB-C oben, Antenne unten,
    5V-Pad rechts oben). Das ESP32-C3-Modul hat kein 3D-Modell -> als Pad-Feld
    sichtbar, der Bestueckungsdruck (USB-C / ANT / Pin-1) traegt die Aussage."""
    from shutil import which
    if not which("kicad-cli"):
        return
    cli = ["xvfb-run", "-a", "kicad-cli"] if which("xvfb-run") else ["kicad-cli"]
    for side in ("top", "bottom"):
        out = REPO / "docs" / f"render-master-{side}.png"
        r = subprocess.run([*cli, "pcb", "render", "--side", side,
                            "--quality", "high", "--background", "opaque",
                            "-w", "1600", "-h", "1200", "--floor",
                            "-o", str(out), str(PCB)], capture_output=True, text=True)
        if r.returncode == 0:
            print(f"Render: {out.relative_to(REPO)}")
        else:
            print(f"[!] Render {side} fehlgeschlagen: {r.stderr.strip()[:200]}")


# --- JLCPCB-Export (--jlc) --------------------------------------------------

_HANDSOLDER_RE = re.compile(r"J\d+$")


def _assembled(ref: str) -> bool:
    if ref.startswith(("#", "H", "TP", "JP")):
        return False
    if _HANDSOLDER_RE.match(ref):
        return False
    if ref == "U1":            # ESP32-C3-Modul steckt in Buchsenleisten (Handmontage)
        return False
    _, _, dnp = sch_gen.COMPONENTS.get(ref, ("", "", False))
    return not dnp


def _csv_field(v: str) -> str:
    return f'"{v}"' if ("," in v or '"' in v) else v


def export_jlc() -> None:
    if not PCB.is_file():
        raise SystemExit(f"{PCB.name} fehlt -- erst ohne --jlc erzeugen.")
    JLC_DIR.mkdir(parents=True, exist_ok=True)
    cli = ["xvfb-run", "-a", "kicad-cli"] if _have("xvfb-run") else ["kicad-cli"]
    import csv
    import io
    import tempfile

    with tempfile.TemporaryDirectory() as td:
        pos = Path(td) / "pos.csv"
        subprocess.run([*cli, "pcb", "export", "pos", "--format", "csv", "--units", "mm",
                        "--side", "both", "--exclude-dnp", "--use-drill-file-origin",
                        "-o", str(pos), str(PCB)], check=True, capture_output=True)
        rows = list(csv.DictReader(io.StringIO(pos.read_text(encoding="utf-8"))))

    cpl_lines = ["Designator,Mid X,Mid Y,Layer,Rotation"]
    placed: set[str] = set()
    for r in rows:
        ref = r["Ref"]
        if not _assembled(ref):
            continue
        layer = "Top" if r["Side"].lower().startswith("t") else "Bottom"
        cpl_lines.append(f'{ref},{float(r["PosX"]):.4f}mm,{float(r["PosY"]):.4f}mm,'
                         f'{layer},{float(r["Rot"]):.4f}')
        placed.add(ref)
    (JLC_DIR / "CPL.csv").write_text("\n".join(cpl_lines) + "\n", encoding="utf-8")

    groups: dict[tuple[str, str, str], list[str]] = {}
    for ref in sorted(placed, key=lambda s: (s[0], int(re.sub(r"\D", "", s) or 0))):
        _, value, _ = sch_gen.COMPONENTS[ref]
        fp_short = FOOTPRINTS[ref].split(":", 1)[1]
        lcsc = LCSC.get(ref, "")
        groups.setdefault((value, fp_short, lcsc), []).append(ref)

    bom_lines = ["Comment,Designator,Footprint,LCSC Part #"]
    missing: list[str] = []
    for (value, fp_short, lcsc), refs in sorted(groups.items()):
        bom_lines.append(",".join(_csv_field(x) for x in (value, " ".join(refs), fp_short, lcsc)))
        if not lcsc:
            missing.extend(refs)
    (JLC_DIR / "BOM.csv").write_text("\n".join(bom_lines) + "\n", encoding="utf-8")

    print(f"jlc/BOM.csv  : {len(groups)} Positionen, {len(placed)} Bauteile bestueckt")
    print(f"jlc/CPL.csv  : {len(placed)} Platzierungen")
    if missing:
        print(f"[!] ohne LCSC-Nummer (im JLCPCB-Cart nachtragen / von Hand): {', '.join(missing)}")
    print("    Ausgabe ist ein Bauartefakt (jlc/ ist in .gitignore).")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--drc", action="store_true")
    ap.add_argument("--png", action="store_true")
    ap.add_argument("--jlc", action="store_true",
                    help="nur jlc/BOM.csv + jlc/CPL.csv aus der vorhandenen .kicad_pcb")
    ap.add_argument("--force", action="store_true",
                    help="Neuaufbau auch dann, wenn die .kicad_pcb bereits Leiterbahnen hat")
    ap.add_argument("--render", action="store_true",
                    help="nur 3D-Ansicht (oben/unten) aus der vorhandenen .kicad_pcb")
    args = ap.parse_args()

    if args.jlc:
        export_jlc()
        return 0

    if args.render:
        render_3d()
        return 0

    if PCB.is_file() and not args.force:
        b = pcbnew.LoadBoard(str(PCB))
        if list(b.GetTracks()) or list(b.Zones()):
            sys.exit(f"{PCB.name} ist bereits verdrahtet -- Neuaufbau wuerde das Routing "
                     f"verwerfen. Vorschau der gerouteten Platine: tools/route_master.py "
                     f"erzeugt docs/pcb-master.png selbst. Neuaufbau erzwingen: --force.")

    board = build()
    pcbnew.SaveBoard(str(PCB), board)
    patch_project_netclasses()
    n = sum(1 for f in board.GetFootprints() if not f.GetReference().startswith("H"))
    print(f"geschrieben: {PCB.relative_to(REPO)}  ({n} Bauteile + 4 Bohrungen, "
          f"{BOARD_W:g} x {BOARD_H:g} mm)")

    if args.png:
        render_png()

    if args.drc:
        cli = ["xvfb-run", "-a", "kicad-cli"] if _have("xvfb-run") else ["kicad-cli"]
        rep = REPO / "docs" / "drc-master.rpt"
        r = subprocess.run([*cli, "pcb", "drc", "--exit-code-violations",
                            "--severity-error", "-o", str(rep), str(PCB)])
        print(f"DRC: {'keine Fehler' if r.returncode == 0 else f'siehe {rep.name}'} "
              f"(unverdrahtete Netze sind vor dem Routen erwartbar)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
