#!/usr/bin/env python3
"""Baut die Master-Firmware und legt ein flash-fertiges Merged-Image nach
firmware/master/prebuilt/ ab (fuer Webflasher / esptool-js / ESP Web Tools).

    python tools/build_master_firmware.py [--no-build]

Erzeugt:
  firmware/master/prebuilt/
    krone-master-esp32c3.factory.bin   Merged-Image, an Offset 0x0 flashen
    manifest.json                      Manifest fuer ESP Web Tools
    README.md                          Kurzanleitung + SHA-256

Die Einzeldateien (bootloader/partitions/boot_app0/app) stehen weiter unter
firmware/master/.pio/build/esp32c3/ (gitignored). Bei jeder Firmware-Aenderung
neu ausfuehren.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import date
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FW = REPO / "firmware" / "master"
BUILD = FW / ".pio" / "build" / "esp32c3"
OUT = FW / "prebuilt"

# Flash-Offsets fuer arduino-esp32 auf dem ESP32-C3 (Standard, boards.txt).
LAYOUT = [
    (0x0, "bootloader.bin"),
    (0x8000, "partitions.bin"),
    (0xE000, "boot_app0.bin"),
    (0x10000, "firmware.bin"),
]


def _pio() -> str:
    for c in (REPO / ".venv" / "bin" / "pio", Path("pio")):
        if c == Path("pio") or c.exists():
            return str(c)
    return "pio"


def _esptool() -> list[str]:
    base = REPO.home() / ".platformio" / "packages"
    et = next(base.glob("tool-esptoolpy/esptool.py"), None)
    if et is None:
        sys.exit("esptool.py nicht gefunden (PlatformIO-Paket tool-esptoolpy).")
    return [sys.executable, str(et)]


def _boot_app0() -> Path:
    base = REPO.home() / ".platformio" / "packages" / "framework-arduinoespressif32"
    p = next(base.rglob("boot_app0.bin"), None)
    if p is None:
        sys.exit("boot_app0.bin nicht gefunden (arduino-esp32 Framework).")
    return p


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--no-build", action="store_true", help="pio run ueberspringen")
    args = ap.parse_args()

    if not args.no_build:
        r = subprocess.run([_pio(), "run", "-e", "esp32c3"], cwd=FW)
        if r.returncode != 0:
            return r.returncode

    # boot_app0.bin in den Build-Ordner spiegeln, damit merge_bin nur dort liest.
    (BUILD / "boot_app0.bin").write_bytes(_boot_app0().read_bytes())
    for _off, name in LAYOUT:
        if not (BUILD / name).is_file():
            sys.exit(f"{name} fehlt in {BUILD} -- erst 'pio run -e esp32c3'.")

    OUT.mkdir(parents=True, exist_ok=True)
    factory = OUT / "krone-master-esp32c3.factory.bin"
    cmd = [*_esptool(), "--chip", "esp32c3", "merge_bin", "-o", str(factory),
           "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "4MB"]
    for off, name in LAYOUT:
        cmd += [hex(off), str(BUILD / name)]
    subprocess.run(cmd, check=True, capture_output=True)

    digest = hashlib.sha256(factory.read_bytes()).hexdigest()
    head = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--short", "HEAD"],
                          capture_output=True, text=True).stdout.strip()

    manifest = {
        "name": "KRONE REW Zentralsteuerung",
        "version": date.today().isoformat(),
        # NICHT erzwungen loeschen: so bleiben Hostname / WLAN / MQTT (NVS) auch
        # beim Flashen ueber den Browser erhalten. Fuer einen echten Neustart
        # bietet ESP Web Tools weiterhin "Erase device" an.
        "new_install_prompt_erase": False,
        "builds": [{
            "chipFamily": "ESP32-C3",
            "parts": [{"path": "krone-master-esp32c3.factory.bin", "offset": 0}],
        }],
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    (OUT / "README.md").write_text(
        "# Vorgebaute Master-Firmware (ESP32-C3 Super Mini)\n\n"
        f"Erzeugt von `tools/build_master_firmware.py` aus `firmware/master/` "
        f"(zuletzt gebaut nahe Commit `{head}`, {date.today().isoformat()}). "
        "Bei jeder Firmware-Aenderung neu ausfuehren.\n\n"
        "| Datei | Zweck |\n|---|---|\n"
        "| `index.html` | Web-Flasher (ESP Web Tools). Wird per GitHub Actions als Page veroeffentlicht. |\n"
        "| `krone-master-esp32c3.factory.bin` | Merged-Image, im Webflasher an **Offset 0x0** flashen (mit „Erase before flash\") |\n"
        "| `manifest.json` | Manifest fuer [ESP Web Tools](https://esphome.github.io/esp-web-tools/) |\n\n"
        f"SHA-256 (`factory.bin`): `{digest}`\n\n"
        "## Flashen\n\n"
        "- **Browser:** <https://tenofnine.github.io/SmartKroneSplitFlap/> "
        "(laedt immer diesen Verzeichnisstand). Chrome/Edge Desktop.\n"
        "- Alternativ [esptool-js](https://espressif.github.io/esptool-js/) — "
        "Datei an Offset `0x0`, „Erase\" aktivieren. Der C3 Super Mini geht ueber "
        "die USB-C-Buchse selbsttaetig in den Download-Modus (kein BOOT-Taster).\n"
        "- Kommandozeile:\n\n"
        "  ```\n"
        "  esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 "
        "krone-master-esp32c3.factory.bin\n"
        "  ```\n\n"
        "Nach dem Boot: Access-Point `krone_anzeige` fuer die WLAN-Einrichtung, "
        "serielle Konsole auf USB-C (115200 Bd). Status-LED (GPIO6): schnelles "
        "Blinken = kein WLAN.\n\n"
        "> `index.html` ist handgepflegt und wird von diesem Skript **nicht** "
        "ueberschrieben.\n",
        encoding="utf-8",
    )

    kib = factory.stat().st_size // 1024
    print(f"geschrieben: {OUT.relative_to(REPO)}/  (factory.bin {kib} KiB, sha256 {digest[:12]}…)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
