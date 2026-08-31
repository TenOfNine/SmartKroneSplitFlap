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

Jedes Bauteil traegt einen Footprint (FOOTPRINTS, Backlog T5).

Danach:
  --erc      ERC laufen lassen         -> docs/erc-daughtercard.rpt
  --pdf      PDF exportieren           -> docs/daughtercard.pdf
  --png      PNG-Vorschau exportieren  -> docs/daughtercard.png (Schnellcheck auf
             GitHub; braucht poppler-utils und ein Python mit Pillow)
  --netlist  PCB-Netzliste exportieren -> hardware/daughtercard/daughtercard.net
             (.gitignore; Bauartefakt fuer den PCB-Editor)

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
NET_PATH = PROJ_DIR / "daughtercard.net"  # PCB-Netzliste, .gitignore (Bauartefakt)
PDF_PATH = REPO_ROOT / "docs" / "daughtercard.pdf"
PNG_PATH = REPO_ROOT / "docs" / "daughtercard.png"
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
    "F1": ("krone:R", "0R", False),  # 0-Ohm-Bruecke (war PTC); D-3, fuer Reihenschaltung >10 Module
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
# Footprints (Backlog T5). Grundlage: docs/schaltplan-daughtercard.md
# Kap. 1 (Passive 0805), Kap. 3 (Stueckliste, Bauformabweichungen), Kap. 5.
# ---------------------------------------------------------------------------

_FP_R0805 = "Resistor_SMD:R_0805_2012Metric"
_FP_R1206 = "Resistor_SMD:R_1206_3216Metric"
_FP_C0805 = "Capacitor_SMD:C_0805_2012Metric"
_FP_SOT23 = "Package_TO_SOT_SMD:SOT-23"
_FP_IDC10 = "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical"
# J1 sitzt board-to-board direkt auf dem Pfostenstecker der Anzeigenplatine
# (KRONE 6281 3 160-00). Daher Buchsenleiste, nicht Wannenstecker.
# Referenz: BKL 10120960 (Buchsenleiste 2x5, 2,54 mm, gerade).
_FP_SOCKET10 = "Connector_PinSocket_2.54mm:PinSocket_2x05_P2.54mm_Vertical"

FOOTPRINTS: dict[str, str] = {
    "U1": "Package_SO:SOIC-20W_7.5x12.8mm_P1.27mm",
    "U2": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "Q1": _FP_SOT23, "Q2": _FP_SOT23, "Q3": _FP_SOT23,
    "D1": _FP_SOT23, "D2": _FP_SOT23, "D3": _FP_SOT23,
    "D4": "LED_SMD:LED_0805_2012Metric",
    "R1": _FP_R0805, "R2": _FP_R0805, "R3": _FP_R0805, "R4": _FP_R0805,
    "R5": _FP_R0805, "R6": _FP_R0805, "R7": _FP_R0805, "R8": _FP_R0805,
    "R9": _FP_R0805, "R10": _FP_R0805, "R11": _FP_R0805, "R12": _FP_R0805,
    "R13": _FP_R0805, "R15": _FP_R0805,
    "R14": _FP_R1206,  # Kap. 3.2: 0-Ohm-Bruecke in 1206 (leichter von Hand auszuloeten)
    "R16": _FP_R0805,  # 120 Ohm; tatsaechliche Verlustleistung ~50 mW << 125 mW (0805)
    "C1": _FP_C0805, "C2": _FP_C0805, "C4": _FP_C0805, "C5": _FP_C0805, "C6": _FP_C0805,
    "C3": "Capacitor_SMD:C_1206_3216Metric",  # 10 uF, Reserve; Kap. 3.2 "Keramik oder Tantal"
    "F1": _FP_R1206,  # 0-Ohm-Bruecke in 1206
    "J1": _FP_SOCKET10,  # Buchsenleiste, board-to-board auf die Anzeigenplatine
    "J2": _FP_IDC10, "J3": _FP_IDC10,
    "J4": "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
    "J5": "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
    "J6": "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
    "JP1": "Jumper:SolderJumper-2_P1.3mm_Open_Pad1.0x1.5mm",
    "JP2": "Jumper:SolderJumper-2_P1.3mm_Open_Pad1.0x1.5mm",
    "JP3": "Jumper:SolderJumper-2_P1.3mm_Open_Pad1.0x1.5mm",
    "TP1": "TestPoint:TestPoint_Pad_D1.5mm", "TP2": "TestPoint:TestPoint_Pad_D1.5mm",
    "TP3": "TestPoint:TestPoint_Pad_D1.5mm", "TP4": "TestPoint:TestPoint_Pad_D1.5mm",
    "TP5": "TestPoint:TestPoint_Pad_D1.5mm", "TP6": "TestPoint:TestPoint_Pad_D1.5mm",
    "TP7": "TestPoint:TestPoint_Pad_D1.5mm",
}

# ---------------------------------------------------------------------------
# LCSC-Bestellnummern fuer die JLCPCB-Bestueckung (--jlc).
#
# Ziel: so viele "Basic Part" wie moeglich, damit keine Ruestkosten (Extended
# Part Fee, einmalig ~3 USD je Position) anfallen. Alle 0805/1206-Widerstaende
# und -Kondensatoren von JLCPCB sind Basic Parts.
#
# Quelle der Basic-Nummern: JLCPCB "Basic Parts Library"-Liste (Snapshot).
# Details und Stand siehe docs/jlc-bestueckung.md. Vor der Bestellung jede
# Nummer im JLCPCB-Parts-Manager gegen Wert, Bauform und Anschlussbild pruefen
# (Projektregel 1: keine Nummer ungeprueft uebernehmen).
#
# Leer ("") = bewusst keine feste Nummer. Betrifft Bauteile, die es im
# Basic-Snapshot nicht gibt (BAT54S, BSS84, LED, PTC) oder die von Hand
# geloetet werden. Fuer die SMT-Bestueckung traegt sie der Nutzer im
# JLCPCB-Cart nach; --jlc listet sie mit leerem Feld und warnt.
# ---------------------------------------------------------------------------

LCSC: dict[str, str] = {
    # --- Basic Parts: Widerstaende 0805 ---
    "R1": "C17414", "R3": "C17414", "R5": "C17414", "R7": "C17414",   # 10k
    "R10": "C17414",                                                   # 10k (DNP)
    "R2": "C17513", "R4": "C17513", "R6": "C17513", "R11": "C17513",   # 1k
    "R13": "C17513", "R15": "C17513",                                  # 1k
    "R8": "C17407", "R12": "C17407",                                   # 100k
    "R9": "C17673",                                                    # 4k7
    "R16": "C17437",                                                   # 120R 0805
    # --- Basic Parts: Widerstaende 1206 ---
    "R14": "C17888",                                                   # 0R 1206 (Handloet-Bruecke)
    # --- Basic Parts: Kondensatoren ---
    "C1": "C49678", "C2": "C49678",                                    # 100n 50V 0805
    "C4": "C1710", "C5": "C1710", "C6": "C1710",                       # 10n 50V 0805
    "C3": "C13585",                                                    # 10u 50V 1206 (Reserve-Bulk)
    # --- Basic Part: Transistor ---
    "Q2": "C20526", "Q3": "C20526",                                    # MMBT3904 NPN SOT-23
    # --- Extended Parts (unvermeidbar, je ~3 USD Ruestkosten einmalig) ---
    "U1": "C614136",   # ATtiny1616-SNR, SOIC-20
    "U2": "C94206",    # TP8485E-SR, RS-485-Transceiver SOIC-8
    # --- ohne feste Nummer: im Basic-Snapshot nicht vorhanden ---
    "Q1": "",   # BSS84 P-MOSFET  (Kandidat Basic: C8492 - vor Bestellung pruefen)
    "D1": "", "D2": "", "D3": "",   # BAT54S Doppel-Schottky (Extended)
    "D4": "",   # LED gruen 0805
    "F1": "C17888",  # 0 Ohm 1206 Basic (0-Ohm-Bruecke statt PTC, D-3)
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
    # USART0 auf der Standard-MUX-Position (P-3): RXD=PB3, TXD=PB2, XDIR=PB0.
    "RO": [("U2", "1"), ("U1", "8")],
    "DI": [("U2", "4"), ("U1", "9")],
    "DE": [("U2", "3"), ("U1", "11")],
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

# Funktionsblock-Platzierung fuer den optischen Schnellcheck. Kein Ersatz fuer
# ein handverlegtes Blatt (T5), aber gruppiert die Bauteile sinnvoll.
# (Titel, x, y, Spalten, Zellbreite, Zellhoehe, [refs])
BLOCKS: list[tuple[str, float, float, int, float, float, list[str]]] = [
    ("MCU", 25, 40, 1, 40, 55, ["U1"]),
    ("RS-485-Bus", 95, 40, 3, 40, 55, ["U2", "R16", "JP3"]),
    ("Versorgung", 230, 40, 4, 34, 55, ["F1", "C1", "C2", "C3", "R14", "JP1", "JP2"]),
    ("Triac-Ansteuerung", 400, 40, 4, 38, 55, ["Q1", "Q2", "Q3", "R7", "R8", "R9", "R10"]),
    ("Impulseingang Blatt (PA4)", 25, 140, 4, 36, 50, ["R1", "R2", "C4", "D1"]),
    ("Impulseingang Leerbild (PA5)", 210, 140, 4, 36, 50, ["R3", "R4", "C5", "D2"]),
    ("Impulseingang Nullimpuls (PA6)", 395, 140, 4, 36, 50, ["R5", "R6", "C6", "D3"]),
    ("CHAIN / Enumeration", 25, 225, 3, 40, 50, ["R11", "R12", "R13"]),
    ("Status-LED / UPDI", 175, 225, 3, 40, 50, ["R15", "D4", "J6"]),
    ("Stecker", 25, 300, 5, 48, 55, ["J1", "J2", "J3", "J4", "J5"]),
    ("Testpunkte", 310, 300, 7, 32, 50, ["TP1", "TP2", "TP3", "TP4", "TP5", "TP6", "TP7"]),
]

# Bewusst offene Reserve-Pins von U1 (docs/pruefpunkte-t4.md P-1, pruefpunkte-t7.md P-3).
# PB1 (Pin 10) ist frei, seit DE auf PB0 liegt; PB0 (Pin 11) traegt jetzt DE.
NO_CONNECT_PINS = [("U1", "10"), ("U1", "12"), ("U1", "13"), ("U1", "14"), ("U1", "15")]


def _symbol_pins(lib_text: str) -> dict[str, list[str]]:
    """Pinnummern je Symbol direkt aus der .kicad_sym lesen (ohne kicad-sch-api,
    damit der Konsistenzcheck von der Toolchain-Version unabhaengig ist)."""
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
    """Prueft, dass jeder Bauteilpin genau einem Netz (oder NC) zugeordnet ist
    und jedes Bauteil einen Footprint hat."""
    lib_path = PROJ_DIR / "symbols" / "krone.kicad_sym"
    pins_by_symbol = _symbol_pins(lib_path.read_text(encoding="utf-8"))

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

    fp_root = Path("/usr/share/kicad/footprints")
    for ref, (lib_id, _value, _dnp) in COMPONENTS.items():
        sym_name = lib_id.split(":", 1)[1]
        pins = pins_by_symbol.get(sym_name)
        if pins is None:
            problems.append(f"Symbol {sym_name} fuer {ref} nicht in krone.kicad_sym")
            continue
        for pin in pins:
            if (ref, pin) not in assigned:
                problems.append(f"{ref}.{pin} hat kein Netz")

        fp = FOOTPRINTS.get(ref)
        if not fp:
            problems.append(f"{ref} hat keinen Footprint (Backlog T5)")
        elif fp_root.is_dir():
            lib, _, mod = fp.partition(":")
            if not (fp_root / f"{lib}.pretty" / f"{mod}.kicad_mod").is_file():
                # nur ein Hinweis: Footprintnamen koennen zwischen KiCad-Versionen
                # abweichen, ohne dass die Netzliste falsch ist
                print(f"  [i] {ref}: Footprint {fp} nicht in {fp_root} gefunden",
                      file=sys.stderr)
    return problems


def build_schematic():
    import kicad_sch_api as ksa
    from kicad_sch_api.library import get_symbol_cache

    get_symbol_cache().add_library_path(str(PROJ_DIR / "symbols" / "krone.kicad_sym"))

    sch = ksa.create_schematic("daughtercard")
    sch.set_paper_size("A2")
    sch.set_title_block(
        title="KRONE REW Daughter Card - Modulsteuerung",
        rev="0.2",
        date="2026-08-28",
        company="TenOfNine",
        comments={
            1: "Generiert aus docs/schaltplan-daughtercard.md Kap. 6 via tools/gen_daughtercard_sch.py",
            2: "Verbindung ueber gleichnamige Pin-Labels. Nicht handverlegt (Backlog T5).",
            3: "Offene Pruefpunkte: docs/pruefpunkte-t4.md",
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

    # Bauteile blockweise platzieren, mit Blocktitel.
    for title, bx, by, cols, cw, ch, refs in BLOCKS:
        sch.add_text(title, position=(bx, by - 18.0), size=2.5, bold=True)
        for i, ref in enumerate(refs):
            place(ref, bx + (i % cols) * cw, by + (i // cols) * ch)

    # PWR_FLAG-Symbole zwischen Versorgungs- und Triac-Block.
    for n, net in enumerate(POWER_FLAG_NETS):
        sch.components.add("krone:PWR_FLAG", reference=f"#FLG{n + 1}",
                           position=(370.0, 45.0 + n * 16.0))

    # Labels an jeden Pin.
    for net, pins in NETS.items():
        for ref, pin in pins:
            sch.add_label(net, pin=(ref, pin))
    for n, net in enumerate(POWER_FLAG_NETS):
        sch.add_label(net, pin=(f"#FLG{n + 1}", "1"))

    # No-Connect-Flags. sch.get_component_pin_position liefert die Position im
    # Blatt-Koordinatensystem (mit Y-Flip gegenueber der Symbolbibliothek);
    # component.get_pin_position tut das nicht.
    for ref, pin in NO_CONNECT_PINS:
        pos = sch.get_component_pin_position(ref, pin)
        if pos is not None:
            sch.no_connects.add((pos.x, pos.y))

    return sch


def write_project_file() -> None:
    """Legt eine minimale .kicad_pro an, falls noch keine existiert, damit
    kicad-cli die projektlokale sym-lib-table findet und ${KIPRJMOD} aufloest.

    Eine vorhandene Datei wird NICHT ueberschrieben -- der PCB-Editor und
    tools/gen_daughtercard_pcb.py reichern sie um Board-Einstellungen und
    Netzklassen an."""
    if PRO_PATH.exists():
        return
    pro = {
        "board": {},
        "boards": [],
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": PRO_PATH.name, "version": 1},
        "net_settings": {},
        "pcbnew": {"last_paths": {}, "page_layout_descr_file": ""},
        "schematic": {},
        "sheets": [],
        "text_variables": {},
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
    """PNG-Vorschau fuer den optischen Schnellcheck auf GitHub.

    Eigener PDF-Export ohne Zeichnungsrahmen (-e), damit der Weissrand-Zuschnitt
    danach greift. Der Rahmen bleibt im regulaeren docs/daughtercard.pdf.
    """
    if not shutil.which("pdftoppm"):
        print("[!] pdftoppm nicht gefunden (poppler-utils), ueberspringe PNG.", file=sys.stderr)
        return
    import tempfile

    with tempfile.TemporaryDirectory() as td:
        bare_pdf = Path(td) / "bare.pdf"
        run([*kicad_cli(), "sch", "export", "pdf", "-e",
             "-o", str(bare_pdf), str(SCH_PATH)])
        prefix = Path(td) / "p"
        run(["pdftoppm", "-png", "-r", str(dpi), "-f", "1", "-l", "1",
             str(bare_pdf), str(prefix)])
        raw = next(Path(td).glob("p*.png"), None)
        if raw is None:
            print("[!] pdftoppm hat kein PNG erzeugt.", file=sys.stderr)
            return
        # PIL steckt meist im System-Python, nicht in der venv.
        candidates = ["/usr/bin/python3", "/usr/local/bin/python3", sys.executable]
        done = False
        for crop_py in candidates:
            if not Path(crop_py).exists():
                continue
            res = subprocess.run([crop_py, "-c", _CROP_SNIPPET, str(raw), str(PNG_PATH)],
                                 capture_output=True, text=True)
            if res.returncode == 0:
                print("  " + res.stdout.strip())
                done = True
                break
        if not done:
            print("[!] Zuschnitt nicht moeglich (kein Python mit Pillow), kopiere ungeschnitten.",
                  file=sys.stderr)
            shutil.copyfile(raw, PNG_PATH)


def kicad_cli() -> list[str]:
    base = ["kicad-cli"] if shutil.which("kicad-cli") else None
    if base is None:
        sys.exit("kicad-cli nicht gefunden.")
    return (["xvfb-run", "-a", *base] if shutil.which("xvfb-run") else base)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--erc", action="store_true", help="nach dem Schreiben ERC laufen lassen")
    ap.add_argument("--pdf", action="store_true", help="PDF exportieren (docs/daughtercard.pdf)")
    ap.add_argument("--png", action="store_true", help="PNG-Vorschau exportieren (docs/daughtercard.png); zieht --pdf nach sich")
    ap.add_argument("--netlist", action="store_true", help="PCB-Netzliste exportieren (hardware/daughtercard/daughtercard.net)")
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
        print(f"Netzliste: {NET_PATH.relative_to(REPO_ROOT)}"
              + ("" if ok else f"  FEHLER (rc={nl_rc})"))
        rc = rc or (0 if ok else 1)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
