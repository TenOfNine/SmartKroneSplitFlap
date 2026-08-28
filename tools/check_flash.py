#!/usr/bin/env python3
"""Prueft den Flash-Verbrauch einer ELF-Datei gegen eine Obergrenze (Backlog T7/T10).

    python tools/check_flash.py <firmware.elf> <max_bytes>

Sucht avr-size / size in der PlatformIO-Toolchain oder im PATH.
"""
import glob
import os
import subprocess
import sys


def find_size_tool() -> list[str]:
    home = os.path.expanduser("~/.platformio/packages")
    for pat in ("toolchain-atmelavr/bin/avr-size",
                "toolchain-xtensa-esp32*/bin/xtensa-esp32-elf-size"):
        hits = glob.glob(os.path.join(home, pat))
        if hits:
            return [hits[0]]
    for tool in ("avr-size", "size"):
        if subprocess.run(["which", tool], capture_output=True).returncode == 0:
            return [tool]
    sys.exit("kein size-Werkzeug gefunden")


def main() -> int:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    elf, limit = sys.argv[1], int(sys.argv[2])

    out = subprocess.check_output([*find_size_tool(), "-A", elf], text=True)
    sections = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1].isdigit():
            sections[parts[0]] = int(parts[1])

    # Flash-belegend: .text + .data + weitere geladene Abschnitte
    flash = sum(v for k, v in sections.items()
                if k in (".text", ".data", ".rodata") or k.startswith(".flash"))

    print(f"{elf}: {flash} Byte Flash (Grenze {limit})")
    if flash > limit:
        print("UEBER DER GRENZE", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
