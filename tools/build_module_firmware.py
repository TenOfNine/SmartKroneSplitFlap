#!/usr/bin/env python3
"""Baut die Modul-Firmware (ATtiny1616) und legt das Intel-HEX nach
firmware/module/prebuilt/ ab -- fuer den Browser-UPDI-Flasher (Tab
"Daughter Card" auf der GitHub Page).

    python tools/build_module_firmware.py [--no-build]

Erzeugt:
  firmware/module/prebuilt/
    krone-daughtercard-attiny1616.hex   Firmware-Image (SerialUPDI schreibt es)
    README.md                           Kurzanleitung + SHA-256

Der Flasher (firmware/master/prebuilt/updi.js) prueft vor dem Schreiben die
Geraete-ID (ATtiny1616 = 1E 94 21). Bei jeder Firmware-Aenderung neu ausfuehren.
"""
from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from datetime import date
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FW = REPO / "firmware" / "module"
BUILD = FW / ".pio" / "build" / "attiny1616"
OUT = FW / "prebuilt"
HEX_NAME = "krone-daughtercard-attiny1616.hex"


def _pio() -> str:
    c = REPO / ".venv" / "bin" / "pio"
    return str(c) if c.exists() else "pio"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--no-build", action="store_true", help="pio run ueberspringen")
    args = ap.parse_args()

    if not args.no_build:
        r = subprocess.run([_pio(), "run", "-e", "attiny1616"], cwd=FW)
        if r.returncode != 0:
            return r.returncode

    src = BUILD / "firmware.hex"
    if not src.is_file():
        sys.exit(f"{src} fehlt -- erst 'pio run -e attiny1616 -d firmware/module'.")

    OUT.mkdir(parents=True, exist_ok=True)
    hex_out = OUT / HEX_NAME
    # Zeilenenden auf LF normalisieren (Repo-Vorgabe, .gitattributes); Intel-HEX
    # mit reinem LF ist gueltig und wird von avrdude/pymcuprog/updi.js akzeptiert.
    text = src.read_text().replace("\r\n", "\n").replace("\r", "\n")
    hex_out.write_text(text, encoding="ascii", newline="\n")

    digest = hashlib.sha256(hex_out.read_bytes()).hexdigest()
    head = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--short", "HEAD"],
                          capture_output=True, text=True).stdout.strip()

    (OUT / "README.md").write_text(
        "# Vorgebaute Modul-Firmware (ATtiny1616)\n\n"
        f"Erzeugt von `tools/build_module_firmware.py` aus `firmware/module/` "
        f"(zuletzt gebaut nahe Commit `{head}`, {date.today().isoformat()}). "
        "Bei jeder Firmware-Aenderung neu ausfuehren.\n\n"
        "| Datei | Zweck |\n|---|---|\n"
        f"| `{HEX_NAME}` | Intel-HEX der Modul-Firmware. Wird vom Browser-UPDI-Flasher "
        "(Tab **Daughter Card** auf der GitHub Page) geschrieben. |\n\n"
        f"SHA-256: `{digest}`\n\n"
        "## Flashen\n\n"
        "- **Browser (experimentell):** <https://tenofnine.github.io/SmartKroneSplitFlap/> "
        ", Tab *Daughter Card*. USB-Seriell-Adapter (5 V) mit 4,7-kOhm-Bruecke "
        "zwischen TX und RX an J6: TX/RX -> Pin 2 (UPDI), GND -> Pin 1, +5 V -> Pin 3 "
        "(nur wenn die Karte sonst keine 5 V hat). Chrome/Edge Desktop.\n"
        "- **Kommandozeile:**\n\n"
        "  ```\n"
        "  pio run -e attiny1616 -t upload -d firmware/module\n"
        "  ```\n\n"
        "  bzw. direkt `pymcuprog write -d attiny1616 -t uart -u <port> -f "
        f"{HEX_NAME} --erase --verify`.\n\n"
        "Der Flasher prueft vor dem Schreiben die Geraete-ID (ATtiny1616 = "
        "`1E 94 21`) und bricht bei Abweichung ab. Fuses (OSCCFG 20 MHz) werden "
        "nicht angefasst.\n",
        encoding="utf-8",
    )

    print(f"geschrieben: {OUT.relative_to(REPO)}/  ({HEX_NAME} "
          f"{hex_out.stat().st_size} B, sha256 {digest[:12]}…)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
