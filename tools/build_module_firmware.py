#!/usr/bin/env python3
"""Baut die Modul-Firmware (ATtiny1616) in ihren drei Auspraegungen und legt die
Ergebnisse nach firmware/module/prebuilt/ ab.

    python tools/build_module_firmware.py [--no-build]

Erzeugt:
  firmware/module/prebuilt/
    krone-daughtercard-attiny1616.hex        App @ 0x0000, ohne Bootloader
                                             -> pio -t upload / pymcuprog CLI
    krone-daughtercard-bootloader.hex        residenter Bootloader @ 0x0000
    krone-daughtercard-attiny1616-boot.hex   App @ 0x0C00 (hinter dem Bootloader)
                                             -> Browser-Werksflash (updi.js)
    krone-daughtercard-attiny1616.mota        signierter Container der -boot-App
                                             -> Firmware-Verteilung ueber den Bus
    README.md

Der .mota-Container wird mit tools/ota_keys.py signiert (privater Schluessel aus
KRONE_OTA_KEY oder ~/.config/krone/ota-signing.pem). Fehlt der Schluessel, wird
er uebersprungen. EXPERIMENTELL -- Bootloader/Werksflash am Geraet noch nicht
verifiziert, siehe docs/module-bootloader.md.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
from datetime import date
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MODULE = REPO / "firmware" / "module"
BOOTLOADER = REPO / "firmware" / "bootloader"
OUT = MODULE / "prebuilt"

HEX_PLAIN = "krone-daughtercard-attiny1616.hex"
HEX_BOOT = "krone-daughtercard-attiny1616-boot.hex"
HEX_BL = "krone-daughtercard-bootloader.hex"
MOTA = "krone-daughtercard-attiny1616.mota"

APP_BASE = 0x0C00  # muss mit firmware/bootloader BOOT_APP_BASE / _boot-Linker uebereinstimmen


def _pio() -> str:
    c = REPO / ".venv" / "bin" / "pio"
    return str(c) if c.exists() else "pio"


def _objcopy() -> str:
    base = REPO.home() / ".platformio" / "packages" / "toolchain-atmelavr" / "bin"
    p = next(base.glob("avr-objcopy"), None)
    return str(p) if p else "avr-objcopy"


def _lf(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def _sign_mota(elf: Path, out: Path) -> bool:
    raw = out.with_suffix(".bin.tmp")
    subprocess.run([_objcopy(), "-O", "binary", str(elf), str(raw)], check=True)
    env = os.environ.get("KRONE_OTA_KEY", "")
    key = Path(env) if (env and "BEGIN" not in env) else (
        Path.home() / ".config" / "krone" / "ota-signing.pem")
    if not (env and "BEGIN" in env) and not key.is_file():
        raw.unlink(missing_ok=True)
        print(f"WARNUNG: kein OTA-Signaturschluessel ({key}); {out.name} wird nicht "
              f"erzeugt.", file=sys.stderr)
        return False
    r = subprocess.run([sys.executable, str(REPO / "tools" / "ota_keys.py"),
                        "sign", "--raw", str(raw), str(out)])
    raw.unlink(missing_ok=True)
    if r.returncode != 0:
        sys.exit("Signieren der .mota fehlgeschlagen.")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()

    envs = [
        (MODULE, "attiny1616", HEX_PLAIN, None),
        (MODULE, "attiny1616_boot", HEX_BOOT, MOTA),
        (BOOTLOADER, "bootloader", HEX_BL, None),
    ]
    OUT.mkdir(parents=True, exist_ok=True)

    for proj, env, hexname, mota in envs:
        build = proj / ".pio" / "build" / env
        if not args.no_build:
            r = subprocess.run([_pio(), "run", "-e", env], cwd=proj)
            if r.returncode != 0:
                return r.returncode
        src_hex = build / "firmware.hex"
        if not src_hex.is_file():
            sys.exit(f"{src_hex} fehlt -- 'pio run -e {env} -d {proj.relative_to(REPO)}'.")
        (OUT / hexname).write_text(_lf(src_hex.read_text()), encoding="ascii", newline="\n")
        if mota:
            _sign_mota(build / "firmware.elf", OUT / mota)

    plain_sha = hashlib.sha256((OUT / HEX_PLAIN).read_bytes()).hexdigest()
    mota_note = (
        f"`{MOTA}` " + ("vorhanden" if (OUT / MOTA).is_file() else "(nicht signiert)")
    )
    head = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--short", "HEAD"],
                          capture_output=True, text=True).stdout.strip()

    (OUT / "README.md").write_text(
        "# Vorgebaute Modul-Firmware (ATtiny1616)\n\n"
        f"Erzeugt von `tools/build_module_firmware.py` (zuletzt nahe Commit `{head}`, "
        f"{date.today().isoformat()}). Bei jeder Firmware-Aenderung neu ausfuehren.\n\n"
        "| Datei | Zweck |\n|---|---|\n"
        f"| `{HEX_PLAIN}` | App @ 0x0000, **ohne** Bootloader. `pio run -e attiny1616 "
        "-t upload` bzw. `pymcuprog`. Der abgesicherte Weg. |\n"
        f"| `{HEX_BL}` | residenter Bootloader @ 0x0000 (Werksflash). |\n"
        f"| `{HEX_BOOT}` | App @ 0x0C00, laeuft hinter dem Bootloader (Werksflash). |\n"
        f"| `{MOTA}` | signierter Container der `-boot`-App fuer die Firmware-"
        "Verteilung ueber den Bus (der Master bettet sie ein). |\n\n"
        f"SHA-256 (`{HEX_PLAIN}`): `{plain_sha}`\n\n"
        "## Flashen\n\n"
        "- **Toolchain (sicher):** `pio run -e attiny1616 -t upload -d firmware/module`.\n"
        "- **Browser-Werksflash (experimentell):** "
        "<https://tenofnine.github.io/SmartKroneSplitFlap/>, Tab *Daughter Card*. "
        "Schreibt Bootloader + `-boot`-App und setzt die Fuse `BOOTEND = 0x0C`. "
        "USB-Seriell-Adapter (5 V) mit 4,7-kOhm-Bruecke TX--RX an J6 "
        "(TX/RX -> Pin 2 UPDI, GND -> Pin 1, +5 V -> Pin 3).\n"
        "- **Ueber den Bus:** ist ein Bootloader geflasht, aktualisiert der Master "
        "die Karten aus seiner Web-UI (*Einstellungen > Modul-Firmware*). "
        "Siehe `docs/module-bootloader.md`.\n\n"
        f"Der Flasher prueft die Geraete-ID (ATtiny1616 = `1E 94 21`). {mota_note}.\n",
        encoding="utf-8",
    )

    print(f"geschrieben: {OUT.relative_to(REPO)}/  ({HEX_PLAIN}, {HEX_BOOT}, "
          f"{HEX_BL}, {mota_note})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
