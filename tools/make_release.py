#!/usr/bin/env python3
"""Release-Paket der vorgebauten Firmware vorbereiten (Tag vMAJOR.MINOR).

Aufruf:
    python tools/make_release.py            # pruefen und Tag lokal anlegen
    python tools/make_release.py --check    # nur pruefen

Prueft, dass Master- und Modul-Version uebereinstimmen, firmware/CHANGELOG.md
eine Zeile fuer die Version hat, die eingebettete Modul-Firmware zur Version
passt und die Prebuilts committet sind. Danach legt es das annotierte Tag
vMAJOR.MINOR auf HEAD an. Erst der Push des Tags (git push origin vX.Y) loest
in .github/workflows/ci.yml den Release-Job aus: GitHub-Release mit allen
Prebuilts; der Pages-Workflow spiegelt es fuer die Versionsauswahl im
Webflasher.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

PREBUILTS = [
    "firmware/master/prebuilt/krone-master-esp32c3.factory.bin",
    "firmware/master/prebuilt/krone-master-esp32c3.kota",
    "firmware/master/prebuilt/manifest.json",
    "firmware/module/prebuilt/krone-daughtercard-attiny1616.hex",
    "firmware/module/prebuilt/krone-daughtercard-attiny1616-boot.hex",
    "firmware/module/prebuilt/krone-daughtercard-bootloader.hex",
    "firmware/module/prebuilt/krone-daughtercard-attiny1616.mota",
]


def _git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=REPO, check=True,
                          capture_output=True, text=True).stdout.strip()


def _grab(path: str, pattern: str) -> int:
    m = re.search(pattern, (REPO / path).read_text(), re.M)
    if not m:
        sys.exit(f"{path}: Muster {pattern!r} nicht gefunden")
    return int(m.group(1), 0)


def versions() -> tuple[int, int]:
    mj = _grab("firmware/master/src/main.cpp", r"FW_VERSION_MAJOR\s*=\s*(\d+)")
    mn = _grab("firmware/master/src/main.cpp", r"FW_VERSION_MINOR\s*=\s*(\d+)")
    aj = _grab("firmware/module/src/board.h", r"#define APP_VERSION_MAJOR\s+(\d+)u")
    an = _grab("firmware/module/src/board.h", r"#define APP_VERSION_MINOR\s+(\d+)u")
    emb = _grab("firmware/master/src/module_fw.h", r"#define MODULE_FW_VER\s+(0x[0-9a-fA-F]+)u")
    if (mj, mn) != (aj, an):
        sys.exit(f"Master {mj}.{mn} und Modul {aj}.{an} unterscheiden sich")
    if emb != (aj << 8 | an):
        sys.exit(f"module_fw.h traegt 0x{emb:04x} statt {aj}.{an} -- "
                 "tools/build_master_firmware.py ausfuehren")
    return mj, mn


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="nur pruefen, kein Tag")
    args = ap.parse_args()

    mj, mn = versions()
    tag = f"v{mj}.{mn}"
    if not re.search(rf"^\| {mj}\.{mn} \|", (REPO / "firmware/CHANGELOG.md").read_text(), re.M):
        sys.exit(f"firmware/CHANGELOG.md hat keine Zeile fuer {mj}.{mn}")
    for p in PREBUILTS:
        if not (REPO / p).is_file():
            sys.exit(f"{p} fehlt")
    dirty = _git("status", "--porcelain", "--", "firmware/master/prebuilt",
                 "firmware/module/prebuilt", "firmware/master/src/module_fw.h")
    if dirty:
        sys.exit("Prebuilts nicht committet:\n" + dirty)
    if _git("tag", "--list", tag):
        sys.exit(f"Tag {tag} existiert bereits -- Version erhoehen (CLAUDE.md)")
    print(f"ok: Version {mj}.{mn}, Prebuilts committet, Tag {tag} frei")
    if args.check:
        return 0
    _git("tag", "-a", tag, "-m", f"Firmware {mj}.{mn}")
    print(f"Tag {tag} auf {_git('rev-parse', '--short', 'HEAD')} angelegt.")
    print(f"Release ausloesen: git push origin {tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
