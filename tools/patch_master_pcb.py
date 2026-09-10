#!/usr/bin/env python3
"""Setzt neue Bauteile in die BEREITS geroutete master.kicad_pcb ein und
schliesst nur die neuen Verbindungen -- ohne kompletten FreeRouting-Neulauf.

Hintergrund: `route_master.py` (FreeRouting 2.3.0) haengt in der aktuellen
Dev-Umgebung reproduzierbar. Fuer kleine Revisionen (hier: Rev. 0.2 = Q1/R8/D2)
ist ein Vollneuroute ohnehin unerwuenscht, weil sich dabei jede Bahn und jede
UUID aendert. Dieses Skript ist der inkrementelle Weg:

  * Basis ist die zuletzt committete `master.kicad_pcb` (aus `git show`, damit
    der Lauf unabhaengig vom Arbeitsstand reproduzierbar ist),
  * die in `gen_master_pcb.PLACEMENT` neuen Refs werden mit Footprint, Position,
    Netz und Schaltplan-UUID eingesetzt,
  * Bahnen, die durch geaenderte Netze kurzschliessen wuerden, werden gekappt,
  * `finish_routes.py` (A*-Rastersuche) schliesst die offenen Verbindungen,
  * die Masseflaechen werden neu aufgebaut (bindet die neuen SMD-GND-Pads an).

    /usr/bin/python3 tools/patch_master_pcb.py            # schreibt hardware/master/master.kicad_pcb
    /usr/bin/python3 tools/patch_master_pcb.py --base HEAD~1

Danach `tools/add_silk_marks.py --board hardware/master/master.kicad_pcb`,
`gen_master_manufacturing.py` und ein DRC-Lauf. `finish_routes.py` ist eine
A*-Rastersuche und nicht streng deterministisch -- bleiben Verbindungen offen
(DRC: unverdrahtet), den Lauf einfach wiederholen.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from shutil import which as _which

try:
    import pcbnew
except ImportError:
    sys.exit("pcbnew nicht gefunden -- mit /usr/bin/python3 starten.")

REPO = Path(__file__).resolve().parent.parent
PCB = REPO / "hardware" / "master" / "master.kicad_pcb"
SCH = REPO / "hardware" / "master" / "master.kicad_sch"
FP_ROOT = Path("/usr/share/kicad/footprints")

sys.path.insert(0, str(REPO / "tools"))
import gen_master_pcb as gpcb          # noqa: E402
import gen_master_sch as gsch          # noqa: E402
import route_master as rm              # noqa: E402
import finish_routes                   # noqa: E402

# Refs, die in dieser Revision neu dazugekommen sind, und ihre Pad->Netz-Zuordnung
# (Netznamen ohne fuehrenden "/"). Quelle: gen_master_sch.NETS.
NEW_REFS = ["Q1", "R8", "D2"]

# Feste Referenztext-Positionen (Absolutkoordinaten, mm), damit der auto-
# platzierte Text nicht in den Loetstoppbereich der Nachbarpads faellt.
# Betreiber-Wunsch 10.09.2026: U2/D2 unter das Bauteil, R4 knapp links darunter.
REF_POS_MM = {                    # (x, y, textsize_mm) -- direkt am Bauteil
    "U2": (40.0, 31.4, 1.0),      # dicht unter dem IC
    "D2": (44.4, 36.9, 0.9),      # dicht unter dem Bauteil
    "R4": (41.2, 30.0, 0.9),      # dicht links am Bauteil
}


def _pad_nets() -> dict:
    out: dict[str, dict[str, str]] = {r: {} for r in NEW_REFS}
    for net, nodes in gsch.NETS.items():
        for ref, pin in nodes:
            if ref in out:
                out[ref][pin] = net
    return out


def mm(v: float) -> int:
    return pcbnew.FromMM(v)


def _fp_load(fpid: str):
    lib, _, mod = fpid.partition(":")
    fp = pcbnew.FootprintLoad(str(FP_ROOT / f"{lib}.pretty"), mod)
    if fp is None:
        sys.exit(f"Footprint {fpid} nicht ladbar")
    return fp


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default="HEAD",
                    help="git-Ref der Basis-Platine (Vorgabe HEAD)")
    args = ap.parse_args()

    # geroutete Basis-Platine aus git holen
    raw = subprocess.check_output(
        ["git", "-C", str(REPO), "show", f"{args.base}:hardware/master/master.kicad_pcb"])
    PCB.write_bytes(raw)
    gpcb.patch_project_netclasses()

    board = pcbnew.LoadBoard(str(PCB))
    uuids = gpcb.parse_sch_uuids(SCH.read_text(encoding="utf-8"))
    pad_nets = _pad_nets()

    # Footprint-Silk-Texte, die nicht mehr gewuenscht sind (im Board-Instanz
    # eingebacken -> hier entfernen; die .kicad_mod ist bereits bereinigt).
    dropped = 0
    for fp in board.GetFootprints():
        for item in list(fp.GraphicalItems()):
            if (item.GetClass() == "PCB_TEXT"
                    and item.GetText().strip().startswith("ANT:")):
                fp.Delete(item)
                dropped += 1
    if dropped:
        print(f"  {dropped} veralteten Silk-Text entfernt (\"ANT: …\")")

    def net(name: str):
        return (board.FindNet(name) or board.FindNet("/" + name)
                or board.FindNet(name.lstrip("/")))

    already = {f.GetReference() for f in board.GetFootprints()}
    todo = [r for r in NEW_REFS if r not in already]
    if not todo:
        sys.exit("Alle neuen Refs sind schon auf der Platine -- Basis falsch?")

    # neue Netze anlegen (z. B. +5V_RAW)
    wanted = {n for r in todo for n in pad_nets[r].values()}
    for name in wanted:
        if net(name) is None:
            board.Add(pcbnew.NETINFO_ITEM(board, "/" + name))

    for ref in todo:
        fp = _fp_load(gsch.FOOTPRINTS[ref])
        board.Add(fp)
        fp.SetReference(ref)
        _, value, dnp = gsch.COMPONENTS[ref]
        fp.SetValue(value)
        if dnp:
            fp.SetDNP(True)
            fp.SetExcludedFromBOM(True)
        spec = gpcb.PLACEMENT[ref]
        fp.SetPosition(pcbnew.VECTOR2I(mm(spec[0]), mm(spec[1])))
        if len(spec) > 3 and spec[3] == "B":
            fp.SetLayerAndFlip(pcbnew.B_Cu)
        if spec[2]:
            fp.SetOrientationDegrees(spec[2])
        if ref in uuids:
            fp.SetPath(pcbnew.KIID_PATH("/" + uuids[ref]))
        for pad in fp.Pads():
            nm = pad_nets[ref].get(pad.GetNumber())
            if nm:
                nn = net(nm)
                if nn is None:
                    sys.exit(f"Netz {nm} nicht gefunden")
                pad.SetNet(nn)
        print(f"  + {ref} @ ({spec[0]},{spec[1]}) rot {spec[2]}")

    # Pads, deren Netz sich geaendert hat (J1.1: +5V_IN -> +5V_RAW), umsetzen
    # und die dadurch kurzschliessenden Bahnstuecke am Pad kappen.
    for ref, pin, new_net_name in [("J1", "1", "+5V_RAW")]:
        fp = next((f for f in board.GetFootprints() if f.GetReference() == ref), None)
        if fp is None:
            continue
        pad = next((p for p in fp.Pads() if p.GetNumber() == pin), None)
        if pad is None or net(new_net_name) is None:
            continue
        old_code = pad.GetNetCode()
        pad.SetNet(net(new_net_name))
        pos = pad.GetPosition()
        killed = 0
        for t in list(board.GetTracks()):
            if t.GetNetCode() != old_code:
                continue
            if any((pt - pos).EuclideanNorm() < mm(0.6)
                   for pt in (t.GetStart(), t.GetEnd())):
                board.Delete(t)
                killed += 1
        print(f"  {ref}.{pin} -> {new_net_name}: {killed} Bahnstueck(e) gekappt")

    # Referenztexte, die sonst vom Loetstopp beschnitten werden, fest setzen.
    for ref, (rx, ry, rsz) in REF_POS_MM.items():
        fp = next((f for f in board.GetFootprints() if f.GetReference() == ref), None)
        if fp is None:
            continue
        rt = fp.Reference()
        rt.SetPosition(pcbnew.VECTOR2I(mm(rx), mm(ry)))
        rt.SetTextAngle(pcbnew.EDA_ANGLE(0, pcbnew.DEGREES_T))
        rt.SetTextSize(pcbnew.VECTOR2I(mm(rsz), mm(rsz)))
        rt.SetTextThickness(mm(rsz * 0.15))
        print(f"  Referenztext {ref} -> ({rx}, {ry}) {rsz} mm")

    board.BuildConnectivity()
    pcbnew.SaveBoard(str(PCB), board)

    # finish_routes ist eine A*-Rastersuche und nicht streng deterministisch --
    # bis zu 4 Runden, bis nichts mehr offen ist.
    for rnd in range(1, 5):
        n = finish_routes.finish(board, PCB)
        pcbnew.SaveBoard(str(PCB), board)
        board = pcbnew.LoadBoard(str(PCB))
        left = len(finish_routes.drc_unconnected(PCB))
        print(f"  finish_routes Runde {rnd}: {n} geschlossen, {left} offen")
        if left == 0:
            break
    else:
        sys.exit(f"finish_routes: nach 4 Runden noch {left} Verbindung(en) offen "
                 "-- erneut ausfuehren oder D2-Position pruefen")

    # Masseflaechen neu (alte Zonen + GND-Bahnen weg, Stitching + Fuellung neu)
    for z in list(board.Zones()):
        board.Delete(z)
    gnet = net("GND")
    drop = [t for t in board.GetTracks()
            if t.GetNetCode() == gnet.GetNetCode() and t.GetClass() == "PCB_TRACK"]
    for t in drop:
        board.Delete(t)
    board.BuildConnectivity()
    print(f"  GND: {len(drop)} Bahnen entfernt, Zonen neu")
    rm.add_stitching_vias(board)
    rm.add_ground_zones(board)

    board.BuildConnectivity()
    pcbnew.SaveBoard(str(PCB), board)
    gpcb.patch_project_netclasses()
    print(f"  geschrieben: {PCB.relative_to(REPO)}  "
          f"({len(list(board.GetTracks()))} Segmente/Vias)")

    # Endkontrolle: DRC (nur Fehler). finish_routes ist nicht deterministisch --
    # bei Rest-Verstoessen abbrechen, damit kein unsauberes Board committet wird.
    cli = ["xvfb-run", "-a", "kicad-cli"] if _which("xvfb-run") else ["kicad-cli"]
    rep = REPO / "docs" / "drc-master.rpt"
    rc = subprocess.run([*cli, "pcb", "drc", "--exit-code-violations",
                         "--severity-error", "-o", str(rep), str(PCB)],
                        capture_output=True).returncode
    if rc != 0:
        sys.exit("DRC nach dem Patch nicht sauber -- Skript erneut ausfuehren "
                 "(finish_routes A* nicht deterministisch).")
    print("  DRC: 0 Fehler")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
