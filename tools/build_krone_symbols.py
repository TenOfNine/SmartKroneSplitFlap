#!/usr/bin/env python3
"""Erzeugt die Projekt-Symbolbibliothek hardware/daughtercard/symbols/krone.kicad_sym.

Hintergrund: Backlog T4, Vorarbeit aus docs/symbolpruefung.md Abschnitt 4.

Damit T4, T5, CI und das Layout reproduzierbar dieselben Symbole verwenden -
unabhaengig von der installierten KiCad-Version - werden ALLE im Schaltplan der
Daughter Card verwendeten Symbole aus der offiziellen KiCad-9-Bibliothek in eine
Projektbibliothek kopiert. Der Schaltplan referenziert dann nur noch `krone:*`.
Das vermeidet auch die ERC-Warnung `lib_symbol_mismatch` (eingebettete Kopie
weicht von der Systembibliothek ab).

Sonderfaelle:
  * ATtiny1616-S  wird unveraendert kopiert (erbt von ATtiny406-S).
  * TP8485E-SR    ist eine unveraenderte Kopie von Interface_UART:MAX3485
                  (erbt von LTC2850xS8), gesetzt sind nur Value, Footprint
                  SOIC-8_3.9x4.9mm_P1.27mm, LCSC C94206, MPN. Pinbelegung
                  gegen das Datenblatt geprueft, siehe docs/symbolpruefung.md.

Basissymbole (`extends`) werden automatisch mitkopiert.

Aufruf:
    python tools/build_krone_symbols.py [--check]
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
OUT_PATH = REPO_ROOT / "hardware" / "daughtercard" / "symbols" / "krone.kicad_sym"

KICAD_SYMBOL_DIRS = [
    Path("/usr/share/kicad/symbols"),
    Path("/usr/local/share/kicad/symbols"),
    Path("/app/share/kicad/symbols"),
    Path.home() / ".local/share/kicad/9.0/symbols",
]

# Verbatim zu kopierende Symbole: (Quelldatei, Symbolname). Basissymbole per
# extends werden automatisch aufgeloest.
VERBATIM = [
    ("MCU_Microchip_ATtiny.kicad_sym", "ATtiny1616-S"),
    ("Device.kicad_sym", "R"),
    ("Device.kicad_sym", "C"),
    ("Device.kicad_sym", "LED"),
    ("Device.kicad_sym", "Polyfuse"),
    ("Transistor_FET.kicad_sym", "BSS84"),
    ("Transistor_BJT.kicad_sym", "MMBT3904"),
    ("Diode.kicad_sym", "BAT54S"),
    ("Connector_Generic.kicad_sym", "Conn_02x05_Odd_Even"),
    ("Connector_Generic.kicad_sym", "Conn_01x03"),
    ("Connector.kicad_sym", "Screw_Terminal_01x02"),
    ("Connector.kicad_sym", "TestPoint"),
    ("Jumper.kicad_sym", "SolderJumper_2_Open"),
    ("power.kicad_sym", "PWR_FLAG"),
]


def find_symbol_dir() -> Path:
    for d in KICAD_SYMBOL_DIRS:
        if (d / "Interface_UART.kicad_sym").is_file():
            return d
    sys.exit(
        "KiCad-Symbolbibliothek nicht gefunden. Erwartet in einem von:\n  "
        + "\n  ".join(str(d) for d in KICAD_SYMBOL_DIRS)
    )


def extract_block(text: str, symbol_name: str) -> str:
    """Liefert den vollstaendigen (symbol "name" ...) S-Ausdruck als Text."""
    needle = f'(symbol "{symbol_name}"'
    start = text.find(needle)
    if start == -1:
        raise KeyError(f'Symbol "{symbol_name}" nicht gefunden.')
    depth = 0
    in_str = False
    esc = False
    for i in range(start, len(text)):
        c = text[i]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start : i + 1]
    raise ValueError(f'Klammern unbalanciert bei "{symbol_name}".')


def base_of(block: str) -> str | None:
    m = re.search(r'\(extends "([^"]+)"\)', block)
    return m.group(1) if m else None


def collect(src_text: str, name: str, out: "dict[str, str]") -> None:
    """Fuegt name und alle Basissymbole (rekursiv) zu out hinzu (Basis zuerst)."""
    if name in out:
        return
    block = extract_block(src_text, name)
    base = base_of(block)
    if base:
        collect(src_text, base, out)
    out[name] = block


def reindent(block: str) -> str:
    """(symbol ...) sitzt eine Ebene tief; nur der fuehrende Tab fehlt."""
    return "\t" + block


def make_tp8485e(source_dir: Path, out: "dict[str, str]") -> str:
    src = (source_dir / "Interface_UART.kicad_sym").read_text(encoding="utf-8")
    collect(src, "MAX3485", out)  # zieht LTC2850xS8 als Basis mit
    block = out.pop("MAX3485")

    block = block.replace('(symbol "MAX3485"', '(symbol "TP8485E-SR"', 1)
    block = block.replace(
        '(property "Value" "MAX3485"', '(property "Value" "TP8485E-SR"', 1
    )
    block = block.replace(
        '(property "Footprint" ""',
        '(property "Footprint" "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm"',
        1,
    )
    block = block.replace(
        '(property "Datasheet" "https://datasheets.maximintegrated.com/en/ds/MAX3483-MAX3491.pdf"',
        '(property "Datasheet" "https://www.3peak.com/product/detail/TP8485E"',
        1,
    )
    block = block.replace(
        '(property "Description" "True RS-485/RS-422, 10Mbps, Slew-Rate Limited, with low-power shutdown, with receiver/driver enable, 32 receiver drive capacitity, DIP-8 and SOIC-8"',
        '(property "Description" "Full Fail-Safe RS-485-Transceiver, 3-5,5 V, 250 kbps, SOIC-8. Pinkompatibel zu MAX3485 - Pinbelegung gegen Datenblatt geprueft, siehe docs/symbolpruefung.md"',
        1,
    )

    extra = "".join(
        f'\t\t(property "{k}" "{v}"\n'
        "\t\t\t(at 0 0 0)\n"
        "\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n"
        "\t\t\t\t(hide yes)\n\t\t\t)\n\t\t)\n"
        for k, v in (("LCSC", "C94206"), ("MPN", "TP8485E-SR"))
    )
    idx = block.rfind("\n")
    return block[:idx] + "\n" + extra + block[idx + 1 :]


def wrap_lib(blocks: "list[str]") -> str:
    parts = [
        "(kicad_symbol_lib",
        "\t(version 20241209)",
        '\t(generator "kicad_symbol_editor")',
        '\t(generator_version "9.0")',
    ]
    parts += [reindent(b) for b in blocks]
    parts.append(")")
    return "\n".join(parts) + "\n"


def flatten_all(extends_lib_text: str, top_names: "list[str]") -> str:
    """Loest alle `extends` auf, indem kicad-sch-api die Symbole einmal in einen
    Schaltplan einbettet (dabei flacht es die Vererbung ab) und die eingebetteten
    Definitionen wieder herausgezogen werden.

    Grund: kicad-sch-api bettet Symbole IMMER abgeflacht ein. Fuer das von
    Q_NPN_BEC abgeleitete MMBT3904 weicht diese Abflachung von der KiCad-eigenen
    ab, was ERC als `lib_symbol_mismatch` meldet. Wenn die Projektbibliothek
    dieselbe (kicad-sch-api-)Abflachung enthaelt, ist eingebettete Kopie ==
    Bibliothek und die Warnung entfaellt.
    """
    import kicad_sch_api as ksa
    from kicad_sch_api.library import get_symbol_cache

    with tempfile.TemporaryDirectory() as td:
        libp = Path(td) / "krone.kicad_sym"
        libp.write_text(extends_lib_text, encoding="utf-8")

        cache = get_symbol_cache()
        cache.clear_cache()
        cache.add_library_path(str(libp))

        sch = ksa.create_schematic("flatten")
        for i, name in enumerate(top_names):
            sch.components.add(f"krone:{name}", reference=f"X{i}", value="x",
                               position=(25.4 * (i + 1), 25.4))
        schp = Path(td) / "flatten.kicad_sch"
        sch.save(str(schp))
        text = schp.read_text(encoding="utf-8")

    out: list[str] = []
    for name in top_names:
        blk = extract_block(text, f"krone:{name}")
        blk = blk.replace(f'(symbol "krone:{name}"', f'(symbol "{name}"', 1)
        # lib_symbols-Eintraege sind zwei Ebenen tief, die Bibliothek will eine.
        blk = "\n".join(ln[1:] if ln.startswith("\t") else ln for ln in blk.split("\n"))
        out.append(blk)
    return out


def build(source_dir: Path) -> str:
    blocks: dict[str, str] = {}
    for fname, name in VERBATIM:
        src = (source_dir / fname).read_text(encoding="utf-8")
        collect(src, name, blocks)
    tp = make_tp8485e(source_dir, blocks)

    # Zwischenbibliothek mit extends, dann ueber kicad-sch-api abflachen.
    top_names = [n for _f, n in VERBATIM] + ["TP8485E-SR"]
    extends_lib = wrap_lib(list(blocks.values()) + [tp])
    flat_blocks = flatten_all(extends_lib, top_names)
    return normalize(wrap_lib(flat_blocks))


def resolve_kicad_cli() -> list[str] | None:
    if shutil.which("kicad-cli"):
        base = ["kicad-cli"]
    elif shutil.which("flatpak"):
        base = ["flatpak", "run", "--command=kicad-cli", "org.kicad.KiCad"]
    else:
        return None
    return ["xvfb-run", "-a", *base] if shutil.which("xvfb-run") else base


def normalize(content: str) -> str:
    """Laesst KiCad die Datei einmal umschreiben (Symbolreihenfolge,
    embedded_fonts, Property-Reihenfolge), damit das erste Oeffnen in der GUI
    keinen Diff erzeugt."""
    cli = resolve_kicad_cli()
    if cli is None:
        print("[!] kicad-cli nicht gefunden, ueberspringe Formatnormalisierung.", file=sys.stderr)
        return content
    with tempfile.TemporaryDirectory() as td:
        raw = Path(td) / "raw.kicad_sym"
        out = Path(td) / "out.kicad_sym"
        raw.write_text(content, encoding="utf-8")
        res = subprocess.run(
            [*cli, "sym", "upgrade", "--force", "-o", str(out), str(raw)],
            capture_output=True, text=True,
        )
        if res.returncode != 0 or not out.is_file():
            print("[!] kicad-cli sym upgrade fehlgeschlagen, nutze Rohformat:\n"
                  + res.stderr.strip(), file=sys.stderr)
            return content
        return out.read_text(encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="nur pruefen, nicht schreiben")
    args = ap.parse_args()

    source_dir = find_symbol_dir()
    content = build(source_dir)

    if args.check:
        if not OUT_PATH.is_file():
            print(f"FEHLT: {OUT_PATH}", file=sys.stderr)
            return 1
        if OUT_PATH.read_text(encoding="utf-8") != content:
            print(f"VERALTET: {OUT_PATH}. python tools/build_krone_symbols.py erneut ausfuehren.",
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
