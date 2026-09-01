#!/usr/bin/env python3
"""Erzeugt die Projekt-Symbolbibliothek hardware/master/symbols/krone_master.kicad_sym.

Analog zu tools/build_krone_symbols.py (Daughter Card), damit T11 (Master-Hardware),
CI und das Layout reproduzierbar dieselben Symbole verwenden - unabhaengig von der
installierten KiCad-Version. Der Master-Schaltplan referenziert nur `krone_master:*`.

Sonderfaelle:
  * TP8485E-SR   - identische Erzeugung wie bei der Daughter Card (make_tp8485e
                   aus build_krone_symbols importiert). Pinbelegung gegen das
                   Datenblatt geprueft, siehe docs/symbolpruefung.md.
  * ESP32-C3-SuperMini - fuer das Aftermarket-Modul gibt es kein KiCad-Symbol.
                   Hier von Hand als 2x8-Modulsymbol erzeugt. Pin-Reihenfolge und
                   Einbaulage aus den Fotos des Betreibers, Gegenprobe in
                   docs/symbolpruefung-master.md (CLAUDE.md Regel 5).

Basissymbole (`extends`) werden automatisch mitkopiert und - wie bei der Daughter
Card - ueber kicad-sch-api abgeflacht.

Aufruf:
    python tools/build_krone_master_symbols.py [--check]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
OUT_PATH = REPO_ROOT / "hardware" / "master" / "symbols" / "krone_master.kicad_sym"

sys.path.insert(0, str(REPO_ROOT / "tools"))
import build_krone_symbols as bks  # noqa: E402  (find_symbol_dir, collect, make_tp8485e, wrap_lib, flatten_all, normalize)

# Verbatim zu kopierende Symbole: (Quelldatei, Symbolname). Basissymbole per
# extends loesen sich automatisch auf.
VERBATIM = [
    ("Device.kicad_sym", "R"),
    ("Device.kicad_sym", "C"),
    ("Device.kicad_sym", "LED"),
    ("Device.kicad_sym", "FerriteBead_Small"),
    ("Connector.kicad_sym", "Screw_Terminal_01x02"),
    ("Connector.kicad_sym", "TestPoint"),
    ("Connector_Generic.kicad_sym", "Conn_02x05_Odd_Even"),
    ("Connector_Generic.kicad_sym", "Conn_01x04"),
    ("Jumper.kicad_sym", "SolderJumper_3_Open"),
    ("74xGxx.kicad_sym", "74LVC1G17"),
    ("power.kicad_sym", "PWR_FLAG"),
]

# ---------------------------------------------------------------------------
# ESP32-C3 Super Mini - Modulsymbol von Hand.
#
# 16 Header-Pins (2x8, 2,54 mm, Reihenabstand 15,24 mm). Pinnummern folgen der
# Footprint-Belegung:
#   Pad 1..8  = rechte Stiftreihe der Platine, oben -> unten
#               5V, GND, 3V3, GPIO4, GPIO3, GPIO2, GPIO1, GPIO0
#   Pad 9..16 = linke Stiftreihe, oben -> unten
#               GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10, GPIO20, GPIO21
#
# BOOT-/RST-Taster, IO8-LED, USB-C und die Antenne sind KEINE Header-Pins.
# Einbaulage: USB-C/Taster an der Oberkante, Antenne an der Unterkante.
# ---------------------------------------------------------------------------

# (Nummer, Name, Typ, Seite)   Seite: "R" rechts (Pad 1..8), "L" links (Pad 9..16)
_C3_PINS = [
    ("1", "5V", "power_in", "R"),
    ("2", "GND", "power_in", "R"),
    ("3", "3V3", "power_out", "R"),
    ("4", "GPIO4", "bidirectional", "R"),
    ("5", "GPIO3", "bidirectional", "R"),
    ("6", "GPIO2", "bidirectional", "R"),
    ("7", "GPIO1", "bidirectional", "R"),
    ("8", "GPIO0", "bidirectional", "R"),
    ("9", "GPIO5", "bidirectional", "L"),
    ("10", "GPIO6", "bidirectional", "L"),
    ("11", "GPIO7", "bidirectional", "L"),
    ("12", "GPIO8", "bidirectional", "L"),
    ("13", "GPIO9", "bidirectional", "L"),
    ("14", "GPIO10", "bidirectional", "L"),
    ("15", "GPIO20", "bidirectional", "L"),
    ("16", "GPIO21", "bidirectional", "L"),
]


def make_esp32c3_supermini() -> str:
    """Vollstaendiger (symbol "ESP32-C3-SuperMini" ...) S-Ausdruck als Text."""
    half_w = 12.7          # Rechteck-Halbbreite
    pin_len = 3.81
    top_y = 12.7           # oberste Pinreihe
    dy = 2.54
    rows = 8
    body_top = top_y + 2.54
    body_bot = top_y - (rows - 1) * dy - 2.54

    def prop(name: str, value: str, y: float, hide: bool) -> str:
        h = "\n\t\t\t\t(hide yes)" if hide else ""
        return (f'\t\t(property "{name}" "{value}"\n'
                f'\t\t\t(at 0 {y:g} 0)\n'
                f'\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t){h}\n\t\t\t)\n'
                f'\t\t)\n')

    out = ['(symbol "ESP32-C3-SuperMini"\n',
           '\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n']
    out.append(prop("Reference", "U", body_top + 2.54, False))
    out.append(prop("Value", "ESP32-C3-SuperMini", body_bot - 2.54, False))
    out.append(prop("Footprint", "krone_master:ESP32-C3-SuperMini", 0, True))
    out.append(prop("Datasheet", "https://www.espressif.com/en/products/socs/esp32-c3", 0, True))
    out.append(prop("Description",
                    "ESP32-C3 Super Mini Modul (Aftermarket), 2x8 Header 2,54 mm, "
                    "Reihenabstand 15,24 mm. Pinbelegung siehe docs/symbolpruefung-master.md",
                    0, True))
    out.append(prop("ki_keywords", "ESP32-C3 RISC-V WiFi BLE module", 0, True))

    out.append('\t\t(symbol "ESP32-C3-SuperMini_1_1"\n')
    out.append(f'\t\t\t(rectangle\n\t\t\t\t(start {-half_w:g} {body_top:g})\n'
               f'\t\t\t\t(end {half_w:g} {body_bot:g})\n'
               '\t\t\t\t(stroke\n\t\t\t\t\t(width 0.254)\n\t\t\t\t\t(type default)\n\t\t\t\t)\n'
               '\t\t\t\t(fill\n\t\t\t\t\t(type background)\n\t\t\t\t)\n\t\t\t)\n')

    idx = {"R": 0, "L": 0}
    for num, name, typ, side in _C3_PINS:
        i = idx[side]
        idx[side] += 1
        y = top_y - i * dy
        if side == "R":
            x, rot = half_w + pin_len, 180
        else:
            x, rot = -half_w - pin_len, 0
        out.append(
            f'\t\t\t(pin {typ} line\n'
            f'\t\t\t\t(at {x:g} {y:g} {rot})\n'
            f'\t\t\t\t(length {pin_len:g})\n'
            f'\t\t\t\t(name "{name}"\n\t\t\t\t\t(effects\n\t\t\t\t\t\t(font\n\t\t\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t\t\t)\n\t\t\t\t\t)\n\t\t\t\t)\n'
            f'\t\t\t\t(number "{num}"\n\t\t\t\t\t(effects\n\t\t\t\t\t\t(font\n\t\t\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t\t\t)\n\t\t\t\t\t)\n\t\t\t\t)\n'
            f'\t\t\t)\n'
        )
    out.append('\t\t)\n\t\t(embedded_fonts no)\n\t)')
    return "".join(out)


def build(source_dir: Path) -> str:
    blocks: dict[str, str] = {}
    for fname, name in VERBATIM:
        src = (source_dir / fname).read_text(encoding="utf-8")
        bks.collect(src, name, blocks)
    tp = bks.make_tp8485e(source_dir, blocks)
    c3 = make_esp32c3_supermini()

    top_names = [n for _f, n in VERBATIM] + ["TP8485E-SR", "ESP32-C3-SuperMini"]
    extends_lib = bks.wrap_lib(list(blocks.values()) + [tp, c3])
    flat_blocks = bks.flatten_all(extends_lib, top_names)
    return bks.normalize(bks.wrap_lib(flat_blocks))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="nur pruefen, nicht schreiben")
    args = ap.parse_args()

    source_dir = bks.find_symbol_dir()
    content = build(source_dir)

    if args.check:
        if not OUT_PATH.is_file():
            print(f"FEHLT: {OUT_PATH}", file=sys.stderr)
            return 1
        if OUT_PATH.read_text(encoding="utf-8") != content:
            print(f"VERALTET: {OUT_PATH}. python tools/build_krone_master_symbols.py erneut ausfuehren.",
                  file=sys.stderr)
            return 1
        print(f"OK: {OUT_PATH.relative_to(REPO_ROOT)} ist aktuell.")
        return 0

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(content, encoding="utf-8")
    names = sorted(re.findall(r'^\t\(symbol "([^"]+)"', content, re.M))
    print(f"geschrieben: {OUT_PATH.relative_to(REPO_ROOT)}  ({len(names)} Symbole)")
    for n in names:
        print(f"  + {n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
