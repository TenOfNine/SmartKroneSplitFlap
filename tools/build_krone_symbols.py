#!/usr/bin/env python3
"""Erzeugt die Projekt-Symbolbibliothek hardware/daughtercard/symbols/krone.kicad_sym.

Hintergrund: Backlog T4, Vorarbeit aus docs/symbolpruefung.md Abschnitt 4.

Damit T4, CI und das Layout reproduzierbar dieselben Symbole verwenden, werden die
benoetigten Symbole aus der installierten offiziellen KiCad-Bibliothek kopiert:

  * ATtiny1616-S            (unveraendert, erbt von ATtiny406-S)
  * ATtiny406-S             (Basissymbol, wird von ATtiny1616-S benoetigt)
  * TP8485E-SR              (unveraenderte Kopie von Interface_UART:MAX3485,
                             nur Value/Footprint/LCSC/MPN gesetzt)
  * LTC2850xS8              (Basissymbol, wird von TP8485E-SR benoetigt)

KiCad loest 'extends' nur innerhalb derselben .kicad_sym-Datei auf, daher muessen
die Basissymbole mit in die Datei.

Die Pinbelegung beider Bauteile ist in docs/symbolpruefung.md gegen die
Datenblaetter geprueft und vom Betreiber freigegeben (27.08.2026). Dieses Skript
aendert an den Pins nichts.

Aufruf:
    python tools/build_krone_symbols.py [--check]

--check schreibt nicht, sondern meldet nur, ob die vorhandene Datei zum
erzeugten Inhalt passt (fuer CI).
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
OUT_PATH = REPO_ROOT / "hardware" / "daughtercard" / "symbols" / "krone.kicad_sym"

# Kandidatenpfade fuer die installierte KiCad-Symbolbibliothek.
KICAD_SYMBOL_DIRS = [
    Path("/usr/share/kicad/symbols"),
    Path("/usr/local/share/kicad/symbols"),
    Path("/app/share/kicad/symbols"),  # Flatpak
    Path.home() / ".local/share/kicad/9.0/symbols",
]

SOURCES = {
    "MCU_Microchip_ATtiny.kicad_sym": ["ATtiny406-S", "ATtiny1616-S"],
    "Interface_UART.kicad_sym": ["LTC2850xS8"],
}


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
        raise KeyError(f'Symbol "{symbol_name}" nicht in der Quelle gefunden.')
    depth = 0
    i = start
    in_str = False
    esc = False
    while i < len(text):
        c = text[i]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    return text[start : i + 1]
        i += 1
    raise ValueError(f'Klammern unbalanciert bei "{symbol_name}".')


def reindent(block: str) -> str:
    """Setzt den Block auf die richtige Tiefe innerhalb von (kicad_symbol_lib ...).

    extract_block liefert den Text ab der oeffnenden Klammer von (symbol ...).
    In der Quelldatei steht (symbol ...) eine Ebene tief, die Kindzeilen tragen
    also bereits zwei Tabs. Es fehlt nur der eine Tab vor der (symbol-Zeile
    selbst; die schliessende Klammer traegt ihn schon.
    """
    return "\t" + block


def make_tp8485e(max3485_like_base: str, source_dir: Path) -> str:
    """Baut TP8485E-SR aus dem MAX3485-Block: gleiche Pins, andere Metadaten."""
    src = (source_dir / "Interface_UART.kicad_sym").read_text(encoding="utf-8")
    block = extract_block(src, "MAX3485")

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

    # LCSC- und MPN-Feld direkt vor der schliessenden Klammer einfuegen.
    extra = (
        '\t\t(property "LCSC" "C94206"\n'
        "\t\t\t(at 0 0 0)\n"
        "\t\t\t(effects\n"
        "\t\t\t\t(font\n"
        "\t\t\t\t\t(size 1.27 1.27)\n"
        "\t\t\t\t)\n"
        "\t\t\t\t(hide yes)\n"
        "\t\t\t)\n"
        "\t\t)\n"
        '\t\t(property "MPN" "TP8485E-SR"\n'
        "\t\t\t(at 0 0 0)\n"
        "\t\t\t(effects\n"
        "\t\t\t\t(font\n"
        "\t\t\t\t\t(size 1.27 1.27)\n"
        "\t\t\t\t)\n"
        "\t\t\t\t(hide yes)\n"
        "\t\t\t)\n"
        "\t\t)\n"
    )
    assert block.endswith("\n\t)") or block.endswith("\n)")
    idx = block.rfind("\n")
    block = block[:idx] + "\n" + extra + block[idx + 1 :]
    return block


def build(source_dir: Path) -> str:
    parts: list[str] = [
        "(kicad_symbol_lib",
        "\t(version 20241209)",
        '\t(generator "kicad_symbol_editor")',
        '\t(generator_version "9.0")',
    ]

    for fname, names in SOURCES.items():
        src = (source_dir / fname).read_text(encoding="utf-8")
        for name in names:
            parts.append(reindent(extract_block(src, name)))

    parts.append(reindent(make_tp8485e("", source_dir)))
    parts.append(")")
    return normalize("\n".join(parts) + "\n")


def resolve_kicad_cli() -> list[str] | None:
    if shutil.which("kicad-cli"):
        base = ["kicad-cli"]
    elif shutil.which("flatpak"):
        base = ["flatpak", "run", "--command=kicad-cli", "org.kicad.KiCad"]
    else:
        return None
    if shutil.which("xvfb-run"):
        return ["xvfb-run", "-a", *base]
    return base


def normalize(content: str) -> str:
    """Laesst KiCad die Datei einmal umschreiben, damit sie exakt dem nativen
    Format entspricht (Symbolreihenfolge, embedded_fonts, Property-Reihenfolge).

    So erzeugt das erste Oeffnen in der KiCad-GUI keinen Diff. Fehlt kicad-cli,
    bleibt der rohe Zusammenbau erhalten - inhaltlich identisch, nur nicht
    formattreu.
    """
    cli = resolve_kicad_cli()
    if cli is None:
        print(
            "[!] kicad-cli nicht gefunden, ueberspringe Formatnormalisierung.",
            file=sys.stderr,
        )
        return content

    with tempfile.TemporaryDirectory() as td:
        raw = Path(td) / "raw.kicad_sym"
        out = Path(td) / "out.kicad_sym"
        raw.write_text(content, encoding="utf-8")
        res = subprocess.run(
            [*cli, "sym", "upgrade", "--force", "-o", str(out), str(raw)],
            capture_output=True,
            text=True,
        )
        if res.returncode != 0 or not out.is_file():
            print(
                "[!] kicad-cli sym upgrade fehlgeschlagen, nutze Rohformat:\n"
                + res.stderr.strip(),
                file=sys.stderr,
            )
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
            print(
                f"VERALTET: {OUT_PATH} weicht vom erzeugten Inhalt ab. "
                "python tools/build_krone_symbols.py erneut ausfuehren.",
                file=sys.stderr,
            )
            return 1
        print(f"OK: {OUT_PATH} ist aktuell.")
        return 0

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(content, encoding="utf-8")
    print(f"geschrieben: {OUT_PATH.relative_to(REPO_ROOT)}")
    for fname, names in SOURCES.items():
        for name in names:
            print(f"  + {name}  ({fname})")
    print("  + TP8485E-SR  (Kopie von Interface_UART:MAX3485)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
