#!/usr/bin/env python3
"""Erzeugt hardware/daughtercard/daughtercard.kicad_sch aus der Netzliste.

Backlog T4. Quelle ist Kapitel 6 von docs/schaltplan-daughtercard.md (verbindlich).
Die Netzliste ist hier als Datenstruktur hinterlegt; jede Zeile entspricht einem
Eintrag in 6.1 / 6.2 / 6.3.

Vorgehen:
  * Bauteile platzieren (grobes Raster, nicht handverlegt - das Layout ist T5).
  * An jeden Pin ein lokales Label mit dem Netznamen haengen. Gleiche Namen sind
    dasselbe Netz. Das ist die uebliche Methode fuer generierte Schaltplaene und
    genuegt fuer ERC und Netzlistenexport.
  * PWR_FLAG auf die extern gespeisten Versorgungsnetze.
  * No-Connect-Flag auf die bewusst offenen Reserve-Pins von U1.

Danach:
  xvfb-run -a kicad-cli sch erc  --exit-code-violations -o docs/erc-daughtercard.rpt <sch>
  xvfb-run -a kicad-cli sch export pdf -o docs/daughtercard.pdf <sch>

Diese beiden Schritte fuehrt --erc bzw. --pdf gleich mit aus.

Zwei Netzlisten-Entscheidungen aus T4 stehen noch zur zweiten Pruefung, siehe
docs/pruefpunkte-t4.md (P-1 Testpads, P-2 BAT54S-Pinbelegung).
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PROJ_DIR = REPO_ROOT / "hardware" / "daughtercard"
SCH_PATH = PROJ_DIR / "daughtercard.kicad_sch"
PRO_PATH = PROJ_DIR / "daughtercard.kicad_pro"
PDF_PATH = REPO_ROOT / "docs" / "daughtercard.pdf"
ERC_PATH = REPO_ROOT / "docs" / "erc-daughtercard.rpt"

# ---------------------------------------------------------------------------
# Bauteile: ref -> (lib_id, value, dnp)
# ---------------------------------------------------------------------------

# Alle Symbole aus der Projektbibliothek krone (tools/build_krone_symbols.py),
# damit der Schaltplan reproduzierbar ist und ERC keine lib_symbol_mismatch-
# Warnung wirft.
COMPONENTS: dict[str, tuple[str, str, bool]] = {
    "U1": ("krone:ATtiny1616-S", "ATtiny1616-SNR", False),
    "U2": ("krone:TP8485E-SR", "TP8485E-SR", False),
    "Q1": ("krone:BSS84", "BSS84", False),
    "Q2": ("krone:MMBT3904", "MMBT3904", False),
    "Q3": ("krone:MMBT3904", "MMBT3904", True),
    "D1": ("krone:BAT54S", "BAT54S", False),
    "D2": ("krone:BAT54S", "BAT54S", False),
    "D3": ("krone:BAT54S", "BAT54S", False),
    "D4": ("krone:LED", "gruen", False),
    "R1": ("krone:R", "10k", False),
    "R2": ("krone:R", "1k", False),
    "R3": ("krone:R", "10k", False),
    "R4": ("krone:R", "1k", False),
    "R5": ("krone:R", "10k", False),
    "R6": ("krone:R", "1k", False),
    "R7": ("krone:R", "10k", False),
    "R8": ("krone:R", "100k", False),
    "R9": ("krone:R", "4k7", False),
    "R10": ("krone:R", "10k", True),
    "R11": ("krone:R", "1k", False),
    "R12": ("krone:R", "100k", False),
    "R13": ("krone:R", "1k", False),
    "R14": ("krone:R", "0R", False),
    "R15": ("krone:R", "1k", False),
    "R16": ("krone:R", "120R", False),
    "C1": ("krone:C", "100n", False),
    "C2": ("krone:C", "100n", False),
    "C3": ("krone:C", "10u", False),
    "C4": ("krone:C", "10n", False),
    "C5": ("krone:C", "10n", False),
    "C6": ("krone:C", "10n", False),
    "F1": ("krone:Polyfuse", "500mA", False),
    "J1": ("krone:Conn_02x05_Odd_Even", "Anzeige", False),
    "J2": ("krone:Conn_02x05_Odd_Even", "Bus in", False),
    "J3": ("krone:Conn_02x05_Odd_Even", "Bus out", False),
    "J4": ("krone:Screw_Terminal_01x02", "42V~ in", False),
    "J5": ("krone:Screw_Terminal_01x02", "42V~ out", False),
    "J6": ("krone:Conn_01x03", "UPDI", False),
    "JP1": ("krone:SolderJumper_2_Open", "5V", False),
    "JP2": ("krone:SolderJumper_2_Open", "15V", False),
    "JP3": ("krone:SolderJumper_2_Open", "TERM", False),
    "TP1": ("krone:TestPoint", "PB5", False),
    "TP2": ("krone:TestPoint", "PB4", False),
    "TP3": ("krone:TestPoint", "PULSE_NULL_RAW", False),
    "TP4": ("krone:TestPoint", "TRIAC_CTRL", False),
    "TP5": ("krone:TestPoint", "CHAIN_IN", False),
    "TP6": ("krone:TestPoint", "VSENS", False),
    "TP7": ("krone:TestPoint", "GND", False),
}

# ---------------------------------------------------------------------------
# Netzliste: netzname -> [(ref, pin), ...]   (Kapitel 6.1 / 6.2 / 6.3)
# ---------------------------------------------------------------------------

NETS: dict[str, list[tuple[str, str]]] = {
    # 6.1 Versorgungsnetze
    "+5V": [
        ("F1", "2"), ("U1", "1"), ("U2", "8"), ("C1", "1"), ("C2", "1"),
        ("C3", "1"), ("R1", "1"), ("R3", "1"), ("R5", "1"), ("R14", "1"),
        ("JP1", "1"), ("J3", "1"), ("D1", "2"), ("D2", "2"), ("D3", "2"),
    ],
    "GND": [
        ("U1", "20"), ("U2", "2"), ("U2", "5"), ("C1", "2"), ("C2", "2"),
        ("C3", "2"), ("C4", "2"), ("C5", "2"), ("C6", "2"), ("Q2", "2"),
        ("Q3", "2"), ("R12", "2"), ("D4", "1"), ("J1", "1"), ("J1", "3"),
        ("J2", "2"), ("J2", "4"), ("J2", "6"), ("J2", "8"), ("J2", "10"),
        ("J3", "2"), ("J3", "4"), ("J3", "6"), ("J3", "8"), ("J3", "10"),
        ("J6", "1"), ("D1", "1"), ("D2", "1"), ("D3", "1"), ("TP7", "1"),
    ],
    "+15V": [("J2", "9"), ("J3", "9"), ("JP2", "1")],
    "VDRV": [("JP1", "2"), ("JP2", "2"), ("R8", "1"), ("Q1", "2")],
    "VSENS": [("R14", "2"), ("J1", "5"), ("J1", "6"), ("TP6", "1")],
    "+5V_IN": [("J2", "1"), ("F1", "1"), ("J6", "3")],
    # 6.2 Signalnetze
    "RS485_A": [("U2", "6"), ("J2", "3"), ("J3", "3"), ("JP3", "1")],
    "RS485_B": [("U2", "7"), ("J2", "5"), ("J3", "5"), ("R16", "2")],
    "RS485_TERM": [("JP3", "2"), ("R16", "1")],
    "RO": [("U2", "1"), ("U1", "10")],
    "DI": [("U2", "4"), ("U1", "9")],
    "DE": [("U2", "3"), ("U1", "8")],
    "PULSE_BLATT_RAW": [("J1", "10"), ("R1", "2"), ("R2", "1")],
    "PULSE_BLATT": [("R2", "2"), ("C4", "1"), ("D1", "3"), ("U1", "2")],
    "PULSE_LEER_RAW": [("J1", "8"), ("R3", "2"), ("R4", "1")],
    "PULSE_LEER": [("R4", "2"), ("C5", "1"), ("D2", "3"), ("U1", "3")],
    "PULSE_NULL_RAW": [("J1", "7"), ("R5", "2"), ("R6", "1"), ("TP3", "1")],
    "PULSE_NULL": [("R6", "2"), ("C6", "1"), ("D3", "3"), ("U1", "4")],
    "TRIAC_DRV": [("U1", "5"), ("R7", "1"), ("R10", "1")],
    "Q2_BASE": [("R7", "2"), ("Q2", "1")],
    "PFET_GATE": [("Q2", "3"), ("R8", "2"), ("Q1", "1")],
    "Q1_DRAIN": [("Q1", "3"), ("R9", "1")],
    "TRIAC_CTRL": [("R9", "2"), ("Q3", "3"), ("J1", "9"), ("TP4", "1")],
    "Q3_BASE": [("R10", "2"), ("Q3", "1")],
    "CHAIN_IN_RAW": [("J2", "7"), ("R11", "1")],
    "CHAIN_IN": [("R11", "2"), ("R12", "1"), ("U1", "17"), ("TP5", "1")],
    "CHAIN_OUT": [("U1", "18"), ("R13", "1")],
    "CHAIN_OUT_EXT": [("R13", "2"), ("J3", "7")],
    "UPDI": [("U1", "16"), ("J6", "2")],
    "LED_A": [("U1", "19"), ("R15", "1")],
    "LED_K": [("R15", "2"), ("D4", "2")],
    "AC1": [("J4", "1"), ("J5", "1"), ("J1", "2")],
    "AC2": [("J4", "2"), ("J5", "2"), ("J1", "4")],
    "TP_PB5": [("U1", "6"), ("TP1", "1")],
    "TP_PB4": [("U1", "7"), ("TP2", "1")],
}

# Netze mit externer Einspeisung: brauchen ein PWR_FLAG, damit ERC sie als
# getrieben ansieht (+5V/GND wegen der power_in-Pins von U1/U2 zwingend).
POWER_FLAG_NETS = ["+5V", "GND", "+15V", "+5V_IN"]

# Bewusst offene Reserve-Pins von U1 (docs/pruefpunkte-t4.md P-1).
NO_CONNECT_PINS = [("U1", "11"), ("U1", "12"), ("U1", "13"), ("U1", "14"), ("U1", "15")]


def check_netlist_consistency() -> list[str]:
    """Prueft, dass jeder Bauteilpin genau einem Netz (oder NC) zugeordnet ist."""
    import kicad_sch_api as ksa
    from kicad_sch_api.library import get_symbol_cache

    cache = get_symbol_cache()
    cache.add_library_path(str(PROJ_DIR / "symbols" / "krone.kicad_sym"))

    assigned: dict[tuple[str, str], str] = {}
    problems: list[str] = []

    for net, pins in NETS.items():
        for ref, pin in pins:
            key = (ref, pin)
            if key in assigned:
                problems.append(
                    f"{ref}.{pin} doppelt vergeben: {assigned[key]} und {net}"
                )
            assigned[key] = net
    for key in NO_CONNECT_PINS:
        if key in assigned:
            problems.append(f"{key[0]}.{key[1]} ist NC und zugleich in Netz {assigned[key]}")
        assigned[key] = "<NC>"

    for ref, (lib_id, _value, _dnp) in COMPONENTS.items():
        sym = cache.get_symbol(lib_id)
        if sym is None:
            problems.append(f"Symbol {lib_id} fuer {ref} nicht gefunden")
            continue
        for p in sym.pins:
            if (ref, p.number) not in assigned:
                problems.append(f"{ref}.{p.number} ({p.name}) hat kein Netz")
    return problems


def build_schematic():
    import kicad_sch_api as ksa

    sch = ksa.create_schematic("daughtercard")
    sch.set_paper_size("A2")
    sch.set_title_block(
        title="KRONE REW Daughter Card - Modulsteuerung",
        rev="0.2",
        date="2026-08-28",
        company="TenOfNine",
        comments={
            1: "Generiert aus docs/schaltplan-daughtercard.md Kap. 6 via tools/gen_daughtercard_sch.py",
            2: "Layout ist nicht handverlegt - siehe Backlog T5. Offene Pruefpunkte: docs/pruefpunkte-t4.md",
        },
    )

    # Bauteile im groben Raster platzieren.
    order = list(COMPONENTS)
    col_w, row_h, per_row = 45.0, 55.0, 9
    x0, y0 = 40.0, 40.0
    placed = {}
    for i, ref in enumerate(order):
        lib_id, value, dnp = COMPONENTS[ref]
        x = x0 + (i % per_row) * col_w
        y = y0 + (i // per_row) * row_h
        comp = sch.components.add(lib_id, reference=ref, value=value, position=(x, y))
        if dnp:
            comp.set_property("dnp", "true")
        placed[ref] = comp

    # Labels an jeden Pin.
    for net, pins in NETS.items():
        for ref, pin in pins:
            sch.add_label(net, pin=(ref, pin))

    # PWR_FLAG-Symbole.
    for n, net in enumerate(POWER_FLAG_NETS, start=1):
        fx = x0 + (per_row - 1) * col_w + 20.0
        fy = y0 + n * 12.0
        flag = sch.components.add("krone:PWR_FLAG", reference=f"#FLG{n}", position=(fx, fy))
        sch.add_label(net, pin=(f"#FLG{n}", "1"))

    # No-Connect-Flags. sch.get_component_pin_position liefert die Position im
    # Blatt-Koordinatensystem (mit Y-Flip gegenueber der Symbolbibliothek);
    # component.get_pin_position tut das nicht.
    for ref, pin in NO_CONNECT_PINS:
        pos = sch.get_component_pin_position(ref, pin)
        if pos is not None:
            sch.no_connects.add((pos.x, pos.y))

    return sch


def write_project_file() -> None:
    """Minimale .kicad_pro, damit kicad-cli die projektlokale sym-lib-table
    (${KIPRJMOD}/symbols/krone.kicad_sym) findet und ${KIPRJMOD} aufloest."""
    pro = {
        "board": {},
        "boards": [],
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": PRO_PATH.name, "version": 1},
        "net_settings": {},
        "pcbnew": {},
        "schematic": {},
        "sheets": [],
        "text_variables": {},
    }
    PRO_PATH.write_text(json.dumps(pro, indent=2) + "\n", encoding="utf-8")


def run(cmd: list[str]) -> int:
    print("  $", " ".join(cmd))
    return subprocess.run(cmd).returncode


def kicad_cli() -> list[str]:
    base = ["kicad-cli"] if shutil.which("kicad-cli") else None
    if base is None:
        sys.exit("kicad-cli nicht gefunden.")
    return (["xvfb-run", "-a", *base] if shutil.which("xvfb-run") else base)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--erc", action="store_true", help="nach dem Schreiben ERC laufen lassen")
    ap.add_argument("--pdf", action="store_true", help="nach dem Schreiben PDF exportieren")
    ap.add_argument("--check-only", action="store_true", help="nur Netzliste pruefen, nichts schreiben")
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
    if args.pdf:
        run([*kicad_cli(), "sch", "export", "pdf", "-o", str(PDF_PATH), str(SCH_PATH)])
        print(f"PDF: {PDF_PATH.relative_to(REPO_ROOT)}")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
