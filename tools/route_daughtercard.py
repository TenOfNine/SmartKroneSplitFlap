#!/usr/bin/env python3
"""Autoroutet hardware/daughtercard/daughtercard.kicad_pcb mit FreeRouting.

    /usr/bin/python3 tools/route_daughtercard.py [--passes N] [--dry-run]

Ablauf:
  1. Specctra-.dsn aus der .kicad_pcb exportieren (pcbnew)
  2. FreeRouting headless laufen lassen  (tools/vendor/, siehe setup_freerouting.sh)
  3. .ses zurueck in die .kicad_pcb importieren und speichern

Vor dem ersten Lauf: bash tools/setup_freerouting.sh

Achtung: ueberschreibt die Leiterbahnen in der .kicad_pcb. Fuer den finalen
Planungslauf gedacht, nachdem die Bauteilpositionen feststehen.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import pcbnew
except ImportError:
    sys.exit("pcbnew nicht gefunden -- mit /usr/bin/python3 starten.")

REPO = Path(__file__).resolve().parent.parent
PCB = REPO / "hardware" / "daughtercard" / "daughtercard.kicad_pcb"
VENDOR = REPO / "tools" / "vendor"


def java_bin() -> str:
    jp = VENDOR / "java-path"
    if jp.is_file():
        return jp.read_text().strip()
    return "java"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--passes", type=int, default=100,
                    help="Obergrenze der Auto-Routing-Durchlaeufe (Vorgabe 100)")
    ap.add_argument("--dry-run", action="store_true",
                    help="nur .dsn exportieren und FreeRouting laufen lassen, "
                         "die .ses NICHT importieren")
    args = ap.parse_args()

    jar = next(VENDOR.glob("freerouting-*.jar"), None)
    if jar is None:
        sys.exit("FreeRouting fehlt -- bash tools/setup_freerouting.sh")

    board = pcbnew.LoadBoard(str(PCB))

    with tempfile.TemporaryDirectory() as td:
        dsn = Path(td) / "board.dsn"
        ses = Path(td) / "board.ses"

        if not pcbnew.ExportSpecctraDSN(board, str(dsn)):
            sys.exit("Specctra-DSN-Export fehlgeschlagen")
        print(f"DSN exportiert ({dsn.stat().st_size} B)")

        cmd = [java_bin(), "-jar", str(jar), "-de", str(dsn), "-do", str(ses),
               "-mp", str(args.passes), "-l", "en"]
        print("  $", " ".join(cmd))
        r = subprocess.run(cmd)
        if r.returncode != 0 or not ses.is_file():
            sys.exit(f"FreeRouting fehlgeschlagen (rc={r.returncode})")

        if args.dry_run:
            keep = REPO / "hardware" / "daughtercard" / "daughtercard.ses"
            keep.write_bytes(ses.read_bytes())
            print(f"--dry-run: Session nach {keep.relative_to(REPO)} kopiert, "
                  f"kein Import")
            return 0

        if not pcbnew.ImportSpecctraSES(board, str(ses)):
            sys.exit("Specctra-SES-Import fehlgeschlagen")

    board.BuildConnectivity()
    pcbnew.SaveBoard(str(PCB), board)
    tracks = len(list(board.GetTracks()))
    print(f"geschrieben: {PCB.relative_to(REPO)}  ({tracks} Leiterbahn-Segmente/Vias)")
    print("Danach im PCB-Editor DRC laufen lassen und die Massef"
          "äche (Zone) fuellen.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
