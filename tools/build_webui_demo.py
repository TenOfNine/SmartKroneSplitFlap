#!/usr/bin/env python3
"""Baut eine statische Demo der Master-Web-UI fuer die GitHub Page.

Die Weboberflaeche der Zentralsteuerung liegt als einziger Roh-String
``INDEX_HTML`` in ``firmware/master/src/main.cpp``. Dieses Skript schneidet
den Block heraus, setzt direkt nach ``<body>`` den Demo-Shim
(``tools/webui_demo_shim.html``) ein -- der ``window.fetch`` fuer ``/api/*``
mit Beispieldaten ueberlagert -- und schreibt das Ergebnis nach
``firmware/master/prebuilt/demo/index.html``.

Die generierte Datei ist ein reines Bauartefakt (``.gitignore``); die
GitHub-Pages-Action ruft dieses Skript auf und legt ``demo/`` neben den
Web-Flasher.

    python tools/build_webui_demo.py            # schreibt die Datei
    python tools/build_webui_demo.py --check    # nur pruefen, nichts schreiben
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAIN_CPP = REPO / "firmware" / "master" / "src" / "main.cpp"
SHIM = REPO / "tools" / "webui_demo_shim.html"
OUT = REPO / "firmware" / "master" / "prebuilt" / "demo" / "index.html"

# R"HTML( ... )HTML" -- der Delimiter ist projektweit "HTML".
_BLOCK = re.compile(r'R"HTML\((?P<html>.*?)\)HTML"', re.DOTALL)


def extract_index_html(cpp_src: str) -> str:
    m = _BLOCK.search(cpp_src)
    if not m:
        raise SystemExit('INDEX_HTML: R"HTML(...)HTML"-Block in main.cpp nicht gefunden.')
    html = m.group("html")
    if "<body>" not in html:
        raise SystemExit("INDEX_HTML enthaelt kein <body> -- Format geaendert?")
    if "/api/status" not in html:
        raise SystemExit("INDEX_HTML sieht unerwartet aus (kein /api/status).")
    return html


def build(html: str, shim: str) -> str:
    # Shim direkt nach dem oeffnenden <body>-Tag -- laeuft damit vor dem
    # UI-Skript und ersetzt window.fetch, bevor der erste Aufruf faellt.
    marker = "<body>"
    idx = html.index(marker) + len(marker)
    banner_note = (
        "\n<!-- ================================================================\n"
        "     DEMO-BUILD -- erzeugt von tools/build_webui_demo.py aus\n"
        "     firmware/master/src/main.cpp + tools/webui_demo_shim.html.\n"
        "     Nicht von Hand bearbeiten. Kein Firmware-Bestandteil.\n"
        "     ================================================================ -->\n"
    )
    return html[:idx] + banner_note + shim.strip() + "\n" + html[idx:]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="nur extrahieren und zusammensetzen, nichts schreiben")
    ap.add_argument("-o", "--out", type=Path, default=OUT,
                    help=f"Zieldatei (Vorgabe: {OUT.relative_to(REPO)})")
    args = ap.parse_args()

    html = extract_index_html(MAIN_CPP.read_text(encoding="utf-8"))
    shim = SHIM.read_text(encoding="utf-8")
    page = build(html, shim)

    # simple Konsistenzpruefungen
    for needle in ("demo-banner", "window.fetch =", "/api/status"):
        if needle not in page:
            raise SystemExit(f"Zusammengesetzte Seite unvollstaendig: {needle!r} fehlt.")

    if args.check:
        print(f"OK -- {len(page)} Bytes (nicht geschrieben).")
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(page, encoding="utf-8")
    try:
        shown = args.out.resolve().relative_to(REPO)
    except ValueError:
        shown = args.out  # Ziel ausserhalb des Repos (z. B. _site/ in der Pages-Action)
    print(f"{shown} -- {len(page)} Bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
