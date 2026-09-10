#!/usr/bin/env python3
"""Setzt die Maker-Kennzeichnung auf die Rueckseiten-Silkscreen und stellt den
Lagenaufbau auf schwarze Loetstoppmaske / weisse Bestueckungsdruck (JLCPCB:
"Black solder mask, White silkscreen").

    /usr/bin/python3 tools/add_silk_marks.py

Idempotent: vorhandene Logo-Footprints / Texte mit denselben Kennungen werden
zuerst entfernt. Beruehrt keine Leiterbahnen.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import pcbnew

REPO = Path(__file__).resolve().parent.parent

# Vorgabe: Daughter Card. Mit --board <pfad> auch fuer das Master-Board nutzbar;
# die Logo-Bibliothek wird relativ zur .kicad_pcb gesucht.
PCB = REPO / "hardware" / "daughtercard" / "daughtercard.kicad_pcb"
LOGO_LIB = REPO / "hardware" / "daughtercard" / "footprints" / "logos.pretty"

OWNER = "TenOfNine"
# B.SilkS, mittleres Feld -- klar von den THT-Steckerpads
LOGO_POS = (34.0, 25.0)
TEXT_POS = (34.0, 35.0)


STACKUP = """\t\t(stackup
\t\t\t(layer "F.SilkS" (type "Top Silk Screen") (color "White"))
\t\t\t(layer "F.Paste" (type "Top Solder Paste"))
\t\t\t(layer "F.Mask" (type "Top Solder Mask") (color "Black") (thickness 0.01))
\t\t\t(layer "F.Cu" (type "copper") (thickness 0.035))
\t\t\t(layer "dielectric 1" (type "core") (thickness 1.51) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
\t\t\t(layer "B.Cu" (type "copper") (thickness 0.035))
\t\t\t(layer "B.Mask" (type "Bottom Solder Mask") (color "Black") (thickness 0.01))
\t\t\t(layer "B.Paste" (type "Bottom Solder Paste"))
\t\t\t(layer "B.SilkS" (type "Bottom Silk Screen") (color "White"))
\t\t\t(copper_finish "HASL lead free")
\t\t\t(dielectric_constraints no)
\t\t)
"""


def set_black_white_stackup_text(pcb_path: Path) -> None:
    """Lagenaufbau schwarze Maske / weisser Druck. pcbnew exportiert den
    Stackup in diesem Build nicht ueber Python -> direkt im Text setzen."""
    txt = pcb_path.read_text()
    import re
    txt = re.sub(r"\n\t\t\(stackup\n.*?\n\t\t\)\n", "\n", txt, flags=re.S)
    txt = txt.replace("\t(setup\n", "\t(setup\n" + STACKUP, 1)
    pcb_path.write_text(txt)
    print("Lagenaufbau: Loetstoppmaske schwarz / Bestueckungsdruck weiss")


_POLARITY = {"+", "−", "-"}


def clear_existing(board: "pcbnew.BOARD") -> None:
    for f in list(board.GetFootprints()):
        if f.GetFPID().GetLibItemName() == "Logo_GitHub":
            board.Delete(f)
    for d in list(board.GetDrawings()):
        if d.GetClass() == "PCB_TEXT" and d.GetText() in ({OWNER} | _POLARITY):
            board.Delete(d)


def add_power_terminal_polarity(board: "pcbnew.BOARD", ref: str = "J1") -> None:
    """+ / - auf F.SilkS neben eine 2-polige Versorgungsklemme.

    Findet das Footprint <ref>, ordnet den Pad mit +5V*-Netz "+" und den mit
    GND "-" zu und setzt die Markierung aussen neben den jeweiligen Pad.
    No-op, wenn <ref> fehlt oder die Netze nicht passen (z. B. Daughter Card).
    """
    fp = next((f for f in board.GetFootprints() if f.GetReference() == ref), None)
    if fp is None or len(list(fp.Pads())) != 2:
        return                               # nur 2-polige Klemme (nicht die
                                             # 2x5-Buchsenleiste der Daughter Card)
    plus = minus = None
    for pad in fp.Pads():
        n = pad.GetNetname().lstrip("/")
        if n.startswith("+5V") or n.startswith("+3V") or n == "VCC":
            plus = pad
        elif n == "GND":
            minus = pad
    if plus is None or minus is None:
        return

    py = plus.GetPosition().y
    my = minus.GetPosition().y
    bb = fp.GetBoundingBox()                 # Klemmenkoerper inkl. Silk
    gap = pcbnew.FromMM(1.1)
    left_x, right_x = bb.GetLeft() - gap, bb.GetRight() + gap
    # "+" auf die Seite des Plus-Pads legen
    if plus.GetPosition().x <= minus.GetPosition().x:
        plus_x, minus_x = left_x, right_x
    else:
        plus_x, minus_x = right_x, left_x
    for label, pos in (("+", (plus_x, py)), ("−", (minus_x, my))):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(label)
        t.SetLayer(pcbnew.F_SilkS)
        t.SetPosition(pcbnew.VECTOR2I(int(pos[0]), int(pos[1])))
        t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(1.4), pcbnew.FromMM(1.4)))
        t.SetTextThickness(pcbnew.FromMM(0.25))
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
        board.Add(t)
    print(f"Polaritaet + / − auf F.SilkS neben {ref}")


def add_marks(board: "pcbnew.BOARD") -> None:
    fp = pcbnew.FootprintLoad(str(LOGO_LIB), "Logo_GitHub")
    if fp is None:
        raise SystemExit("Logo_GitHub.kicad_mod nicht ladbar")
    board.Add(fp)
    fp.SetReference("GH1")
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    fp.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(LOGO_POS[0]), pcbnew.FromMM(LOGO_POS[1])))
    fp.Flip(fp.GetPosition(), False)          # auf B.SilkS
    print(f"Logo GitHub (~8,5 mm) auf B.SilkS @ {LOGO_POS[0]:g},{LOGO_POS[1]:g}")

    txt = pcbnew.PCB_TEXT(board)
    txt.SetText(OWNER)
    txt.SetLayer(pcbnew.B_SilkS)
    txt.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(TEXT_POS[0]), pcbnew.FromMM(TEXT_POS[1])))
    txt.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(2.2), pcbnew.FromMM(2.2)))
    txt.SetTextThickness(pcbnew.FromMM(0.35))
    txt.SetMirrored(True)
    txt.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
    board.Add(txt)
    print(f"Text \"{OWNER}\" auf B.SilkS @ {TEXT_POS[0]:g},{TEXT_POS[1]:g}")


def main() -> int:
    global PCB, LOGO_LIB
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--board", type=Path, default=PCB,
                    help="Ziel-.kicad_pcb (Vorgabe: Daughter Card)")
    ap.add_argument("--logo-pos", nargs=2, type=float, metavar=("X", "Y"))
    ap.add_argument("--text-pos", nargs=2, type=float, metavar=("X", "Y"))
    args = ap.parse_args()
    PCB = args.board.resolve()
    LOGO_LIB = PCB.parent / "footprints" / "logos.pretty"
    if args.logo_pos:
        globals()["LOGO_POS"] = tuple(args.logo_pos)
    if args.text_pos:
        globals()["TEXT_POS"] = tuple(args.text_pos)

    board = pcbnew.LoadBoard(str(PCB))
    clear_existing(board)
    add_marks(board)
    add_power_terminal_polarity(board)
    pcbnew.SaveBoard(str(PCB), board)
    set_black_white_stackup_text(PCB)
    print(f"geschrieben: {PCB}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
