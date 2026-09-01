#!/usr/bin/env python3
"""Erzeugt hardware/master/master.kicad_sch aus der Netzliste.

Backlog T11. Quelle ist Kapitel 6 von docs/schaltplan-master.md (verbindlich).
Aufbau und Vorgehen identisch zu tools/gen_daughtercard_sch.py:

  * Bauteile blockweise platzieren (grobes Raster, nicht handverlegt).
  * An jeden Pin ein lokales Label mit dem Netznamen.
  * PWR_FLAG auf die extern gespeisten Versorgungsnetze.
  * No-Connect-Flag auf die bewusst offenen Pins.

Danach:
  --erc      ERC laufen lassen         -> docs/erc-master.rpt
  --pdf      PDF exportieren           -> docs/master.pdf
  --png      PNG-Vorschau exportieren  -> docs/master.png
  --netlist  PCB-Netzliste exportieren -> hardware/master/master.net (.gitignore)
  --check-only   nur Netzliste pruefen

Offene Punkte (docs/schaltplan-master.md / GitHub-Issues):
  M-1  ESP32-C3-SuperMini Symbol/Footprint <-> Board-Silk (docs/symbolpruefung-master.md)
  M-2  Step-up-Modul (J4) bleibt DNP bis O-2 gemessen
  M-3  CHAIN-Level-Shifter U3 noetig oder 3,3 V direkt (R7 als 0-Ohm-Bruecke)
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PROJ_DIR = REPO_ROOT / "hardware" / "master"
SCH_PATH = PROJ_DIR / "master.kicad_sch"
PRO_PATH = PROJ_DIR / "master.kicad_pro"
NET_PATH = PROJ_DIR / "master.net"
PDF_PATH = REPO_ROOT / "docs" / "master.pdf"
PNG_PATH = REPO_ROOT / "docs" / "master.png"
ERC_PATH = REPO_ROOT / "docs" / "erc-master.rpt"

# ---------------------------------------------------------------------------
# Bauteile: ref -> (lib_id, value, dnp)
# ---------------------------------------------------------------------------
COMPONENTS: dict[str, tuple[str, str, bool]] = {
    "U1": ("krone_master:ESP32-C3-SuperMini", "ESP32-C3 SuperMini", False),
    "U2": ("krone_master:TP8485E-SR", "TP8485E-SR", False),
    "U3": ("krone_master:74LVC1G17", "74LVC1G17", False),   # CHAIN 3,3 V -> 5 V (M-3)
    "FB1": ("krone_master:FerriteBead_Small", "60R@100MHz", False),  # nur der Logikzweig
    "C1": ("krone_master:C", "100n", False),   # Abblockung U2
    "C2": ("krone_master:C", "100n", False),   # Abblockung U3
    "C3": ("krone_master:C", "47u", False),    # Bulk am +5V-Eingang
    "C4": ("krone_master:C", "100n", False),   # Abblockung nahe U1 (5V)
    "C5": ("krone_master:C", "10u", False),    # Stuetzkondensator +3V3
    "R1": ("krone_master:R", "120R", False),   # RS-485-Abschluss (fest, Spez. 7.1)
    "R2": ("krone_master:R", "680R", False),   # Fail-Safe-Bias A -> +3V3
    "R3": ("krone_master:R", "680R", False),   # Fail-Safe-Bias B -> GND
    "R4": ("krone_master:R", "10k", False),    # DE-Pulldown (kein Bus-Treiben im Reset)
    "R5": ("krone_master:R", "100R", False),   # CHAIN-Serienwiderstand
    "R6": ("krone_master:R", "1k", False),     # Status-LED-Vorwiderstand
    "R7": ("krone_master:R", "0R", True),      # CHAIN-Bypass (DNP, statt U3 wenn 3,3 V genuegt)
    "D1": ("krone_master:LED", "gruen", False),
    "J1": ("krone_master:Screw_Terminal_01x02", "5V IN", False),
    "J2": ("krone_master:Conn_02x05_Odd_Even", "BUS OUT", False),
    "J3": ("krone_master:Conn_01x04", "IO RSV", False),
    "J4": ("krone_master:Conn_01x04", "STEP-UP 5V->15V", True),   # Boost-Modul, DNP (M-2 / O-2)
    "JP1": ("krone_master:SolderJumper_3_Open", "ADER9", False),  # offen / +5V / +15V
    "TP1": ("krone_master:TestPoint", "RS485_A", False),
    "TP2": ("krone_master:TestPoint", "RS485_B", False),
    "TP3": ("krone_master:TestPoint", "CHAIN_BUS", False),
    "TP4": ("krone_master:TestPoint", "+3V3", False),
    "TP5": ("krone_master:TestPoint", "+15V", False),
    "TP6": ("krone_master:TestPoint", "GND", False),
    "TP7": ("krone_master:TestPoint", "ADER9", False),
}

# ---------------------------------------------------------------------------
# Footprints (Backlog T11).
# ---------------------------------------------------------------------------
_FP_R0805 = "Resistor_SMD:R_0805_2012Metric"
_FP_C0805 = "Capacitor_SMD:C_0805_2012Metric"
_FP_TP = "TestPoint:TestPoint_Pad_D1.5mm"

FOOTPRINTS: dict[str, str] = {
    "U1": "modules:ESP32-C3-SuperMini",
    "U2": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "U3": "Package_TO_SOT_SMD:SOT-23-5",
    "FB1": "Inductor_SMD:L_0805_2012Metric",
    "C1": _FP_C0805, "C2": _FP_C0805, "C4": _FP_C0805, "C5": _FP_C0805,
    "C3": "Capacitor_SMD:C_1206_3216Metric",
    "R1": _FP_R0805, "R2": _FP_R0805, "R3": _FP_R0805, "R4": _FP_R0805,
    "R5": _FP_R0805, "R6": _FP_R0805, "R7": _FP_R0805,
    "D1": "LED_SMD:LED_0805_2012Metric",
    "J1": "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
    "J2": "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical",
    "J3": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "J4": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "JP1": "Jumper:SolderJumper-3_P1.3mm_Open_Pad1.0x1.5mm",
    "TP1": _FP_TP, "TP2": _FP_TP, "TP3": _FP_TP, "TP4": _FP_TP,
    "TP5": _FP_TP, "TP6": _FP_TP, "TP7": _FP_TP,
}

# ---------------------------------------------------------------------------
# LCSC-Nummern fuer die (optionale) JLCPCB-Bestueckung. Der Master ist ein
# Einzelstueck; Handbestueckung ist zulaessig (SMD in 0805). Nur belegte,
# gegen den JLC-Parts-Manager gepruefte Nummern werden eingetragen
# (Projektregel 1). Leer = im Cart nachtragen bzw. von Hand bestuecken.
# Stand und Kandidaten: docs/schaltplan-master.md Stueckliste.
# ---------------------------------------------------------------------------
# Alle Nummern am 01.09.2026 im JLCPCB-Parts-Manager gegen Wert, Bauform und
# Bibliothekstyp geprueft (Projektregel 1).
LCSC: dict[str, str] = {
    "R1": "C17437",   # 120R 0805 Basic (wie Daughter Card R16)
    "R2": "C17798",   # 680R 0805 Basic
    "R3": "C17798",   # 680R 0805 Basic
    "R4": "C17414",   # 10k  0805 Basic
    "R5": "C17408",   # 100R 0805 Basic
    "R6": "C17513",   # 1k   0805 Basic
    "R7": "",         # 0R 0805 (DNP) - im Cart waehlen
    "C1": "C49678", "C2": "C49678", "C4": "C49678",   # 100n 0805 Basic
    "C5": "C15850",   # 10u 0805 Basic
    "C3": "C76659",   # 47u 25V 1206 (Extended)
    "D1": "C2297",    # LED gruen 0805 Basic (wie Daughter Card D4)
    "U2": "C94206",   # TP8485E-SR
    "U3": "C19829593",  # 74LVC1G17GV SOT-23-5 (Extended, Lager knapp - vor Bestellung pruefen)
    "FB1": "C18305",  # BLM21PG600SN1D Ferrit 60R@100MHz 3,5A 0805 (Extended)
}

# ---------------------------------------------------------------------------
# Netzliste: netzname -> [(ref, pin), ...]   (docs/schaltplan-master.md Kap. 6)
# ---------------------------------------------------------------------------
NETS: dict[str, list[tuple[str, str]]] = {
    # --- Versorgung ---
    # +5V_IN = ungefilterte 5-V-Schiene vom Netzteil, geht direkt an den Bus,
    # den Ader-9-Jumper und den Boost-Eingang. FB1 filtert nur den lokalen
    # Logikzweig (ESP32-C3 + U3), damit der Ferrit nicht den Busstrom fuehrt.
    "+5V_IN": [
        ("J1", "1"), ("C3", "1"), ("FB1", "1"),
        ("J2", "1"), ("JP1", "1"), ("J4", "1"),
    ],
    "+5V": [("FB1", "2"), ("U1", "1"), ("C4", "1"), ("C2", "1"), ("U3", "5")],
    "+3V3": [
        ("U1", "3"), ("U2", "8"), ("C1", "1"), ("C5", "1"),
        ("R2", "1"), ("TP4", "1"),
    ],
    "GND": [
        ("J1", "2"), ("U1", "2"), ("U2", "2"), ("U2", "5"), ("U3", "3"),
        ("C1", "2"), ("C2", "2"), ("C3", "2"), ("C4", "2"), ("C5", "2"),
        ("R3", "2"), ("R4", "2"), ("D1", "1"),
        ("J2", "2"), ("J2", "4"), ("J2", "6"), ("J2", "8"), ("J2", "10"),
        ("J3", "4"), ("J4", "2"), ("J4", "4"), ("TP6", "1"),
    ],
    "+15V": [("J4", "3"), ("JP1", "3"), ("TP5", "1")],
    # --- RS-485 ---
    "RS485_A": [("U2", "6"), ("R1", "1"), ("R2", "2"), ("J2", "3"), ("TP1", "1")],
    "RS485_B": [("U2", "7"), ("R1", "2"), ("R3", "1"), ("J2", "5"), ("TP2", "1")],
    "RO": [("U2", "1"), ("U1", "4")],          # -> GPIO4
    "DI": [("U2", "4"), ("U1", "5")],          # -> GPIO3
    "DE": [("U2", "3"), ("U1", "14"), ("R4", "1")],   # -> GPIO10
    # --- CHAIN (3,3 V GPIO -> 5 V Bus, U3 oder R7-Bruecke) ---
    "CHAIN_GPIO": [("U1", "9"), ("U3", "2"), ("R7", "1")],   # GPIO5
    "CHAIN_OUT": [("U3", "4"), ("R5", "1"), ("R7", "2")],
    "CHAIN_BUS": [("R5", "2"), ("J2", "7"), ("TP3", "1")],
    # --- Status-LED ---
    "LED_DRV": [("U1", "10"), ("R6", "1")],    # GPIO6
    "LED_A": [("R6", "2"), ("D1", "2")],
    # --- Ader 9 / Triac-Treiberspannung (JP1: Mitte = gemeinsam) ---
    "ADER9": [("JP1", "2"), ("J2", "9"), ("TP7", "1")],
    # --- Reserve-GPIO auf J3 ---
    "IO0_RSV": [("U1", "8"), ("J3", "1")],
    "IO1_RSV": [("U1", "7"), ("J3", "2")],
    "IO7_RSV": [("U1", "11"), ("J3", "3")],
}

# Netze mit externer Einspeisung -> PWR_FLAG, damit ERC sie als getrieben sieht.
# +3V3 wird vom 3V3-Pin des Moduls (power_out) getrieben -> kein Flag.
# +15V wird vom Step-up-Modul (J4.3) getrieben -> Flag (J4-Pin ist passive).
POWER_FLAG_NETS = ["+5V_IN", "+5V", "GND", "+15V"]

# Blockweise Platzierung fuer den optischen Schnellcheck.
# (Titel, x, y, Spalten, Zellbreite, Zellhoehe, [refs])
BLOCKS: list[tuple[str, float, float, int, float, float, list[str]]] = [
    ("ESP32-C3 Super Mini", 25, 45, 1, 40, 60, ["U1"]),
    ("Versorgung 5V", 95, 45, 3, 34, 30, ["J1", "FB1", "C3", "C4", "C5"]),
    ("RS-485-Transceiver", 210, 45, 3, 34, 40, ["U2", "C1", "R1", "R2", "R3", "R4"]),
    ("CHAIN Pegelwandler", 210, 150, 3, 34, 40, ["U3", "C2", "R5", "R7"]),
    ("Status-LED", 95, 150, 2, 30, 40, ["R6", "D1"]),
    ("Ader 9 / Step-up", 360, 45, 2, 34, 40, ["JP1", "J4"]),
    ("Bus / Reserve", 360, 150, 2, 40, 45, ["J2", "J3"]),
    ("Testpunkte", 25, 210, 7, 26, 40, ["TP1", "TP2", "TP3", "TP4", "TP5", "TP6", "TP7"]),
]

# Bewusst offene Pins: ESP32-C3 GPIO2 (Strapping), GPIO8/9 (Strapping/BOOT),
# GPIO20/21 (UART0-Konsole ueber USB-C) und der NC-Pin von U3.
NO_CONNECT_PINS = [
    ("U1", "6"), ("U1", "12"), ("U1", "13"), ("U1", "15"), ("U1", "16"),
    ("U3", "1"),
]


# --- ab hier identische Mechanik wie gen_daughtercard_sch.py -----------------

def _symbol_pins(lib_text: str) -> dict[str, list[str]]:
    import re

    out: dict[str, list[str]] = {}
    for m in re.finditer(r'^\t\(symbol "([^"]+)"', lib_text, re.M):
        name = m.group(1)
        start = m.end()
        depth = 1
        i = start
        while i < len(lib_text) and depth:
            c = lib_text[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            i += 1
        block = lib_text[start:i]
        out[name] = re.findall(r'\(pin\b.*?\(number "([^"]+)"', block, re.S)
    return out


def check_netlist_consistency() -> list[str]:
    lib_path = PROJ_DIR / "symbols" / "krone_master.kicad_sym"
    pins_by_symbol = _symbol_pins(lib_path.read_text(encoding="utf-8"))

    assigned: dict[tuple[str, str], str] = {}
    problems: list[str] = []

    for net, pins in NETS.items():
        for ref, pin in pins:
            key = (ref, pin)
            if key in assigned:
                problems.append(f"{ref}.{pin} doppelt vergeben: {assigned[key]} und {net}")
            assigned[key] = net
    for key in NO_CONNECT_PINS:
        if key in assigned:
            problems.append(f"{key[0]}.{key[1]} ist NC und zugleich in Netz {assigned[key]}")
        assigned[key] = "<NC>"

    fp_root = Path("/usr/share/kicad/footprints")
    for ref, (lib_id, _value, _dnp) in COMPONENTS.items():
        sym_name = lib_id.split(":", 1)[1]
        pins = pins_by_symbol.get(sym_name)
        if pins is None:
            problems.append(f"Symbol {sym_name} fuer {ref} nicht in krone_master.kicad_sym")
            continue
        for pin in pins:
            if (ref, pin) not in assigned:
                problems.append(f"{ref}.{pin} hat kein Netz")

        fp = FOOTPRINTS.get(ref)
        if not fp:
            problems.append(f"{ref} hat keinen Footprint (Backlog T11)")
        elif ":" in fp and fp.split(":", 1)[0] not in ("modules", "logos") and fp_root.is_dir():
            lib, _, mod = fp.partition(":")
            if not (fp_root / f"{lib}.pretty" / f"{mod}.kicad_mod").is_file():
                print(f"  [i] {ref}: Footprint {fp} nicht in {fp_root} gefunden", file=sys.stderr)
    return problems


def build_schematic():
    import kicad_sch_api as ksa
    from kicad_sch_api.library import get_symbol_cache

    get_symbol_cache().add_library_path(str(PROJ_DIR / "symbols" / "krone_master.kicad_sym"))

    sch = ksa.create_schematic("master")
    sch.set_paper_size("A2")
    sch.set_title_block(
        title="KRONE REW Zentralsteuerung (Master) - ESP32-C3 Super Mini",
        rev="0.1",
        date="2026-09-01",
        company="TenOfNine",
        comments={
            1: "Generiert aus docs/schaltplan-master.md Kap. 6 via tools/gen_master_sch.py",
            2: "Verbindung ueber gleichnamige Pin-Labels. Nicht handverlegt (Backlog T11).",
            3: "Offene Punkte: M-1 (ESP32-Pinbelegung), M-2 (Step-up DNP), M-3 (CHAIN-Pegel)",
        },
    )

    placed: dict = {}

    def place(ref: str, x: float, y: float) -> None:
        lib_id, value, dnp = COMPONENTS[ref]
        comp = sch.components.add(lib_id, reference=ref, value=value, position=(x, y),
                                  footprint=FOOTPRINTS.get(ref, ""))
        if dnp:
            comp.set_property("dnp", "true")
        placed[ref] = comp

    for title, bx, by, cols, cw, ch, refs in BLOCKS:
        sch.add_text(title, position=(bx, by - 18.0), size=2.5, bold=True)
        for i, ref in enumerate(refs):
            place(ref, bx + (i % cols) * cw, by + (i // cols) * ch)

    for n, net in enumerate(POWER_FLAG_NETS):
        sch.components.add("krone_master:PWR_FLAG", reference=f"#FLG{n + 1}",
                           position=(15.0, 30.0 + n * 16.0))

    for net, pins in NETS.items():
        for ref, pin in pins:
            sch.add_label(net, pin=(ref, pin))
    for n, net in enumerate(POWER_FLAG_NETS):
        sch.add_label(net, pin=(f"#FLG{n + 1}", "1"))

    for ref, pin in NO_CONNECT_PINS:
        pos = sch.get_component_pin_position(ref, pin)
        if pos is not None:
            sch.no_connects.add((pos.x, pos.y))

    return sch


def write_project_file() -> None:
    if PRO_PATH.exists():
        return
    pro = {
        "board": {}, "boards": [],
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": PRO_PATH.name, "version": 1},
        "net_settings": {},
        "pcbnew": {"last_paths": {}, "page_layout_descr_file": ""},
        "schematic": {}, "sheets": [], "text_variables": {},
    }
    PRO_PATH.write_text(json.dumps(pro, indent=2) + "\n", encoding="utf-8")


def run(cmd: list[str]) -> int:
    print("  $", " ".join(cmd))
    return subprocess.run(cmd).returncode


_CROP_SNIPPET = """
import sys
from PIL import Image, ImageChops, ImageOps
src, dst = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGB")
bg = Image.new("RGB", im.size, (255, 255, 255))
diff = ImageChops.difference(im, bg)
box = diff.getbbox()
if box:
    m = 24
    box = (max(0, box[0] - m), max(0, box[1] - m),
           min(im.size[0], box[2] + m), min(im.size[1], box[3] + m))
    im = im.crop(box)
im = ImageOps.expand(im, border=8, fill=(255, 255, 255))
im.save(dst, optimize=True)
print(f"{dst}  {im.size[0]}x{im.size[1]}")
"""


def render_png(dpi: int = 200) -> None:
    if not shutil.which("pdftoppm"):
        print("[!] pdftoppm nicht gefunden (poppler-utils), ueberspringe PNG.", file=sys.stderr)
        return
    import tempfile

    with tempfile.TemporaryDirectory() as td:
        bare_pdf = Path(td) / "bare.pdf"
        run([*kicad_cli(), "sch", "export", "pdf", "-e", "-o", str(bare_pdf), str(SCH_PATH)])
        prefix = Path(td) / "p"
        run(["pdftoppm", "-png", "-r", str(dpi), "-f", "1", "-l", "1", str(bare_pdf), str(prefix)])
        raw = next(Path(td).glob("p*.png"), None)
        if raw is None:
            print("[!] pdftoppm hat kein PNG erzeugt.", file=sys.stderr)
            return
        for crop_py in ["/usr/bin/python3", "/usr/local/bin/python3", sys.executable]:
            if not Path(crop_py).exists():
                continue
            res = subprocess.run([crop_py, "-c", _CROP_SNIPPET, str(raw), str(PNG_PATH)],
                                 capture_output=True, text=True)
            if res.returncode == 0:
                print("  " + res.stdout.strip())
                return
        shutil.copyfile(raw, PNG_PATH)


def kicad_cli() -> list[str]:
    if not shutil.which("kicad-cli"):
        sys.exit("kicad-cli nicht gefunden.")
    return (["xvfb-run", "-a", "kicad-cli"] if shutil.which("xvfb-run") else ["kicad-cli"])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--erc", action="store_true")
    ap.add_argument("--pdf", action="store_true")
    ap.add_argument("--png", action="store_true")
    ap.add_argument("--netlist", action="store_true")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args()

    problems = check_netlist_consistency()
    if problems:
        print("Netzliste inkonsistent:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    print(f"Netzliste konsistent: {len(COMPONENTS)} Bauteile, {len(NETS)} Netze, "
          f"{sum(len(v) for v in NETS.values())} Pinverbindungen.")
    if args.check_only:
        return 0

    sch = build_schematic()
    SCH_PATH.parent.mkdir(parents=True, exist_ok=True)
    sch.save(str(SCH_PATH))
    print(f"geschrieben: {SCH_PATH.relative_to(REPO_ROOT)}")
    write_project_file()
    print(f"geschrieben: {PRO_PATH.relative_to(REPO_ROOT)}")

    rc = 0
    if args.erc:
        ERC_PATH.parent.mkdir(parents=True, exist_ok=True)
        erc_rc = run([*kicad_cli(), "sch", "erc", "--exit-code-violations",
                      "--severity-error", "--severity-warning",
                      "-o", str(ERC_PATH), str(SCH_PATH)])
        print(f"ERC: {'sauber' if erc_rc == 0 else f'Verletzungen (rc={erc_rc}), siehe {ERC_PATH.relative_to(REPO_ROOT)}'}")
        rc = rc or erc_rc
    if args.pdf or args.png:
        run([*kicad_cli(), "sch", "export", "pdf", "-o", str(PDF_PATH), str(SCH_PATH)])
        print(f"PDF: {PDF_PATH.relative_to(REPO_ROOT)}")
    if args.png:
        render_png()
        print(f"PNG: {PNG_PATH.relative_to(REPO_ROOT)}")
    if args.netlist:
        nl_rc = run([*kicad_cli(), "sch", "export", "netlist", "--format", "kicadsexpr",
                     "-o", str(NET_PATH), str(SCH_PATH)])
        ok = nl_rc == 0 and NET_PATH.is_file()
        print(f"Netzliste: {NET_PATH.relative_to(REPO_ROOT)}" + ("" if ok else f"  FEHLER (rc={nl_rc})"))
        rc = rc or (0 if ok else 1)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
