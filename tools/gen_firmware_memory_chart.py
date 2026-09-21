#!/usr/bin/env python3
"""Erzeugt firmware/speicherprofil.svg -- Flash-Speicherverbrauch von Master-
und Modul-Firmware nach Bereich, gemessen am gelinkten Firmware-Abbild.

    python tools/gen_firmware_memory_chart.py [--no-build]

Baut beide Firmwares (Master: env esp32c3; Modul: env attiny1616_boot in
einer temporaeren Analyse-Variante ohne -flto, damit Funktionsgrenzen als
eigene Symbole sichtbar bleiben -- das ausgelieferte Abbild bleibt bei
-flto), liest die Sektionsgroessen aus dem Linker-Map bzw. per nm aus der
ELF-Datei, ordnet sie Bereichen zu (WLAN-Stack, Krypto, eigener Code, ...)
und schreibt eine selbststaendige SVG-Grafik (keine externen Schriften/
Ressourcen, damit sie in GitHub-READMEs ohne Weiteres rendert).

Erzeugt:
  firmware/speicherprofil.svg

Bei jeder Firmware-Aenderung neu ausfuehren (siehe CLAUDE.md Arbeitsweise).
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from datetime import date
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MASTER = REPO / "firmware" / "master"
MODULE = REPO / "firmware" / "module"
OUT = REPO / "firmware" / "speicherprofil.svg"

# Palette -- an das Dark-Theme der eigenen Web-UI angelehnt (docs/logo.svg:
# Gehaeuse #0b0c0e, Amber #f2b03d).
BG = "#0b0c0e"
PANEL = "#131519"
LINE = "#252a31"
LINE2 = "#1c2025"
TX = "#eef0f2"
DIM = "#9aa1ab"
FAINT = "#666e79"
AMBER = "#f2b03d"
PALETTE = ["#5b8def", "#b07cf0", "#35b3a3", "#7d8590", "#565d67",
           "#e8735c", "#4fc0e8", "#8a92a6"]
GREY = "#3c4147"
SANS = "system-ui,-apple-system,'Segoe UI',sans-serif"
MONO = "ui-monospace,Menlo,Consolas,monospace"


def _pio() -> str:
    for c in (REPO / ".venv" / "bin" / "pio", Path("pio")):
        if c == Path("pio") or c.exists():
            return str(c)
    return "pio"


def _run_pio(args: list[str], cwd: Path) -> str:
    r = subprocess.run([_pio(), *args], cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit(f"pio {' '.join(args)} fehlgeschlagen (cwd={cwd}).")
    return r.stdout


def _flash_stat(pio_output: str) -> tuple[int, int]:
    """(belegt, gesamt) aus der 'Flash: [...] XX.X% (used A bytes from B bytes)'-Zeile."""
    m = re.search(r"Flash:.*\(used (\d+) bytes from (\d+) bytes\)", pio_output)
    if not m:
        sys.exit("Flash-Zeile nicht in der pio-Ausgabe gefunden.")
    return int(m.group(1)), int(m.group(2))


def _parse_map_sections(map_path: Path, section_names: list[str]) -> dict[str, dict[str, int]]:
    """{Sektion: {Objektdatei: Bytes}} aus einer GNU-ld-Map-Datei."""
    sections: dict[str, dict[str, int]] = {}
    cur = None
    pat_head = re.compile(r"^\.(" + "|".join(re.escape(s) for s in section_names) + r")\b")
    pat_obj = re.compile(r"^\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+\.(?:o|a\([^)]*\)))$")
    with map_path.open() as f:
        for line in f:
            hm = pat_head.match(line)
            if hm:
                cur = hm.group(1)
                continue
            om = pat_obj.match(line)
            if om and cur:
                size = int(om.group(1), 16)
                sections.setdefault(cur, {})
                sections[cur][om.group(2)] = sections[cur].get(om.group(2), 0) + size
    return sections


def _bucket_master(obj: str) -> str:
    o = obj.lower()
    if "src/main.cpp.o" in obj:
        return "main.cpp -- eigener Code"
    if any(k in obj for k in ("busmaster", "charmap", "eventlog", "hadiscovery",
                               "masterapp", "moduleupdate", "otaverify", "protocol.c")):
        return "eigene Bibliotheken (lib/*)"
    if "libwifimanager" in o:
        return "WiFiManager (Captive Portal)"
    if any(k in o for k in ("libmbedtls", "libmbedcrypto", "libmbedx509")):
        return "mbedTLS/Crypto (TLS + Signaturpruefung)"
    if any(k in o for k in ("libnet80211", "libpp.a", "libphy", "libcore.a",
                             "libwpa_supplicant", "libwps", "libbtdm", "libcoexist")):
        return "WLAN-Funkstack (net80211/pp/phy/wpa)"
    if "liblwip" in o:
        return "lwIP (TCP/IP-Stack)"
    if "libwifi.a" in o or "libwifigeneric" in o:
        return "Arduino-WiFi-Klasse"
    if "libmdns" in o:
        return "mDNS (ESPmDNS)"
    if any(k in o for k in ("libwebserver", "libdnsserver", "libhttpclient")):
        return "WebServer/DNSServer/HTTPClient (eigene Nutzung)"
    if "libpubsubclient" in o:
        return "MQTT (PubSubClient)"
    if "libarduinojson" in o:
        return "ArduinoJson"
    if "libupdate.a" in o:
        return "OTA-Schreibpfad (Update.h)"
    if "libpreferences" in o or "libnvs" in o:
        return "Einstellungsspeicher (Preferences/NVS)"
    if "libdriver" in o:
        return "ESP-IDF-Treiber (UART, GPIO, ...)"
    if "libfreertos" in o:
        return "FreeRTOS"
    if any(k in o for k in ("libesp_common", "libesp_system", "libesp_hw_support",
                             "libesp_rom", "libheap", "libspi_flash",
                             "libbootloader_support", "libesp_partition", "libsoc.a",
                             "libhal", "libesp_timer", "libesp_event", "libesp_netif",
                             "libnewlib", "libvfs", "libapp_update", "libesp_ringbuf",
                             "libesp_pm", "libmicro-ecc")):
        return "ESP-IDF-Kern/HAL"
    return "sonstiges Framework"


def master_breakdown() -> tuple[list[tuple[str, int]], int, int]:
    out = _run_pio(["run", "-e", "esp32c3"], cwd=MASTER)
    used, total = _flash_stat(out)
    map_path = MASTER / ".pio" / "build" / "esp32c3" / "firmware.map"
    sections = _parse_map_sections(map_path, ["flash.text", "flash.rodata"])
    buckets: dict[str, int] = {}
    measured = 0
    for sec in ("flash.text", "flash.rodata"):
        for obj, sz in sections.get(sec, {}).items():
            b = _bucket_master(obj)
            buckets[b] = buckets.get(b, 0) + sz
            measured += sz
    # Sektionen, die im Map-Parser nicht erfasst sind (IRAM, Header, Alignment,
    # initialisierte Daten) -- Rest zur Bilanz, damit die Grafik nicht leise
    # Bytes "verliert".
    rest = used - measured
    if rest > 0:
        buckets["sonstige ESP32-Sektionen (IRAM, Header, Alignment)"] = (
            buckets.get("sonstige ESP32-Sektionen (IRAM, Header, Alignment)", 0) + rest)
    return sorted(buckets.items(), key=lambda kv: -kv[1]), used, total


MODULE_ANALYSIS_INI = """\
[env:attiny1616_boot_analysis]
extends = env:attiny1616
build_flags = -Wall -Wextra -std=c11 -Os -fno-lto -DHAS_BOOTLOADER
    -Wl,--section-start=.text=0x0C00 -Wl,-Map,.pio/build/attiny1616_boot_analysis/firmware.map
"""


def _bucket_module(symbol: str) -> str:
    if symbol.startswith("motion_") or symbol == "remaining":
        return "lib/motion (Motor-/Blattsteuerung)"
    if symbol.startswith("enum_fsm_"):
        return "lib/enumeration (Adressvergabe)"
    if symbol.startswith("proto_") or symbol == "k_cmd_table":
        return "lib/protocol (Rahmen/CRC)"
    if symbol.startswith("config_"):
        return "lib/config"
    if symbol.startswith("__vector_"):
        return "Interrupt-Vektoren (ISR)"
    if symbol in ("send_frame", "ack", "main"):
        return "main.c -- eigener Code"
    if (symbol.startswith("eeprom_") or symbol in ("memcpy", "__do_clear_bss")
            or symbol.startswith("__udivmod") or symbol.startswith("__divmod")
            or symbol.startswith("__umul") or symbol.startswith("CSWTCH")):
        return "avr-libc (Laufzeitbibliothek)"
    return "sonstiges"


def module_breakdown() -> tuple[list[tuple[str, int]], int, int]:
    with tempfile.TemporaryDirectory() as tmp:
        ini = Path(tmp) / "platformio.ini"
        base = (MODULE / "platformio.ini").read_text()
        ini.write_text(base + "\n" + MODULE_ANALYSIS_INI)
        out = _run_pio(["run", "-c", str(ini), "-e", "attiny1616_boot_analysis"], cwd=MODULE)
        used, total = _flash_stat(out)
        elf = MODULE / ".pio" / "build" / "attiny1616_boot_analysis" / "firmware.elf"
        import shutil
        found = shutil.which("avr-nm")
        if not found:
            for c in Path.home().glob(".platformio/packages/toolchain-atmelavr/bin/avr-nm"):
                found = str(c)
                break
        if not found:
            sys.exit("avr-nm nicht gefunden (PlatformIO-Paket toolchain-atmelavr).")
        r = subprocess.run([found, "--print-size", "--size-sort", "-C", str(elf)],
                            capture_output=True, text=True)
        buckets: dict[str, int] = {}
        measured = 0
        for line in r.stdout.splitlines():
            parts = line.split()
            if len(parts) != 4:
                continue
            _addr, size_hex, typ, name = parts
            if typ.lower() == "b":
                continue  # BSS -- RAM, kein Flash-Verbrauch
            size = int(size_hex, 16)
            b = _bucket_module(name)
            buckets[b] = buckets.get(b, 0) + size
            measured += size
        rest = used - measured
        if rest > 0:
            buckets["Programmstart (Vektortabelle, avr-libc-Start)"] = (
                buckets.get("Programmstart (Vektortabelle, avr-libc-Start)", 0) + rest)
        return sorted(buckets.items(), key=lambda kv: -kv[1]), used, total


def _group_tail(items: list[tuple[str, int]], keep: int, tail_label: str,
                 always_keep: tuple[str, ...] = ()) -> list[tuple[str, int]]:
    """Top `keep` Eintraege einzeln, Rest in einen Sammel-Eintrag (ausser always_keep)."""
    head = [kv for kv in items if kv[0] in always_keep]
    rest_pool = [kv for kv in items if kv[0] not in always_keep]
    n_more = max(0, keep - len(head))
    head += rest_pool[:n_more]
    tail = rest_pool[n_more:]
    result = sorted(head, key=lambda kv: -kv[1])
    if tail:
        result.append((tail_label, sum(sz for _, sz in tail)))
    return result


def _fmt(n: int) -> str:
    return f"{n:,}".replace(",", ".")


def _svg_escape(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def _chip_svg(y0: int, title: str, subtitle: str, items: list[tuple[str, int]],
              used: int, total: int) -> tuple[str, int]:
    W = 860
    BAR_X, BAR_W, BAR_H = 20, 820, 26
    ROW_H = 21
    free = total - used
    display = items + [("frei", free)]
    colors = {}
    ci = 0
    for name, _ in items:
        if "eigener Code" in name:
            colors[name] = AMBER
        else:
            colors[name] = PALETTE[ci % len(PALETTE)]
            ci += 1
    colors["frei"] = None

    parts = []
    y = y0
    parts.append(f'<text x="20" y="{y+16}" font-family="{SANS}" font-size="16" '
                 f'font-weight="700" fill="{TX}">{_svg_escape(title)}</text>')
    parts.append(f'<text x="840" y="{y+16}" text-anchor="end" font-family="{MONO}" '
                 f'font-size="12" fill="{FAINT}">{subtitle}</text>')
    y += 30
    pct_used = used * 100 / total
    parts.append(f'<text x="20" y="{y+11}" font-family="{MONO}" font-size="12" '
                 f'fill="{DIM}">{pct_used:.1f} % belegt · {_fmt(free)} B frei von {_fmt(total)} B</text>')
    y += 18

    # Bar
    parts.append(f'<rect x="{BAR_X}" y="{y}" width="{BAR_W}" height="{BAR_H}" rx="6" '
                 f'fill="{PANEL}" stroke="{LINE2}"/>')
    x = BAR_X
    for name, sz in display:
        w = BAR_W * sz / total
        if name == "frei":
            parts.append(f'<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{BAR_H}" '
                         f'fill="url(#hatch)"/>')
        else:
            parts.append(f'<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{BAR_H}" '
                         f'fill="{colors[name]}"/>')
        x += w
    parts.append(f'<rect x="{BAR_X}" y="{y}" width="{BAR_W}" height="{BAR_H}" rx="6" '
                 f'fill="none" stroke="{LINE2}"/>')
    y += BAR_H + 16

    # Legend
    for name, sz in display:
        pct = sz * 100 / total
        sw_fill = colors[name] if name != "frei" else "none"
        sw_extra = "" if name != "frei" else f' stroke="{FAINT}" stroke-dasharray="2,2"'
        parts.append(f'<rect x="20" y="{y-10}" width="11" height="11" rx="2" '
                     f'fill="{sw_fill}"{sw_extra}/>')
        label_color = TX if name != "frei" else FAINT
        parts.append(f'<text x="38" y="{y}" font-family="{SANS}" font-size="12.5" '
                     f'fill="{label_color}">{_svg_escape(name)}</text>')
        parts.append(f'<text x="740" y="{y}" text-anchor="end" font-family="{MONO}" '
                     f'font-size="12" fill="{DIM if name != "frei" else FAINT}">{_fmt(sz)} B</text>')
        parts.append(f'<text x="840" y="{y}" text-anchor="end" font-family="{MONO}" '
                     f'font-size="12" font-weight="600" fill="{TX if name != "frei" else FAINT}">{pct:.1f} %</text>')
        y += ROW_H
    y += 6
    return "\n".join(parts), y


def render_svg(master: tuple[list[tuple[str, int]], int, int],
               module: tuple[list[tuple[str, int]], int, int],
               head: str) -> str:
    m_items, m_used, m_total = master
    mod_items, mod_used, mod_total = module
    m_items = _group_tail(m_items, keep=8, tail_label="weitere Systemkomponenten",
                           always_keep=("main.cpp -- eigener Code",))

    W = 860
    body_master, y1 = _chip_svg(
        76, "Master · ESP32-C3 Super Mini",
        f"OTA-Partition {_fmt(m_total)} B", m_items, m_used, m_total)
    body_module, y2 = _chip_svg(
        y1 + 22, "Modul · ATtiny1616",
        f"App-Sektion ab 0x0C00, {_fmt(mod_total)} B", mod_items, mod_used, mod_total)

    H = y2 + 14
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="{SANS}">
<defs>
<pattern id="hatch" width="8" height="8" patternTransform="rotate(45)" patternUnits="userSpaceOnUse">
<rect width="8" height="8" fill="{PANEL}"/><line x1="0" y1="0" x2="0" y2="8" stroke="#1a1e24" stroke-width="4"/>
</pattern>
</defs>
<rect width="{W}" height="{H}" fill="{BG}"/>
<text x="20" y="30" font-size="19" font-weight="700" fill="{TX}">Firmware-Speicherprofil</text>
<text x="20" y="49" font-size="12.5" fill="{DIM}">Flash-Speicherverbrauch nach Bereich, gemessen am gelinkten Firmware-Abbild · {head}</text>
<line x1="20" y1="60" x2="840" y2="60" stroke="{LINE}"/>
{body_master}
<line x1="20" y1="{y1+8}" x2="840" y2="{y1+8}" stroke="{LINE}"/>
{body_module}
</svg>
'''
    return svg


README_MD = """# firmware/

Master- und Modul-Firmware der KRONE-REW-Ersatzsteuerung. Details je Ziel:
[`master/README.md`](master/README.md) (ESP32-C3), [`module/README.md`](module/README.md)
(ATtiny1616). Gemeinsame Versionsnummer + Historie: [`CHANGELOG.md`](CHANGELOG.md).

## Speicherprofil

![Firmware-Speicherprofil](speicherprofil.svg)

Aufschlüsselung des Flash-Speicherverbrauchs nach Bereich (Web-UI/eigener Code,
WLAN-Stack, Krypto/TLS, Motor-/Enumerations-Logik, ...), gemessen am jeweils
gelinkten Firmware-Abbild (`firmware.map` bzw. `avr-nm --size-sort` gegen die
ELF-Datei). Die Modul-Zahlen stammen aus einem einmaligen Analyse-Build ohne
`-flto` (damit Funktionsgrenzen als eigene Symbole sichtbar bleiben) -- das
ausgelieferte Abbild wird weiterhin mit `-flto` gebaut und ist ein paar Bytes
kleiner.

Neu erzeugen nach jeder Firmware-Änderung:

```bash
python tools/gen_firmware_memory_chart.py
```

(Siehe CLAUDE.md Arbeitsweise -- gehört zu den nach jeder Firmware-Änderung neu
zu erzeugenden Dateien, wie `prebuilt/`.)
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--no-build", action="store_true",
                     help="vorhandene .pio-Build-Ordner wiederverwenden statt neu zu bauen")
    args = ap.parse_args()
    if args.no_build:
        sys.exit("--no-build ist noch nicht unterstuetzt (Analyse-Build wird immer frisch gebaut).")

    print("baue Master (env esp32c3) ...")
    master = master_breakdown()
    print("baue Modul-Analyse (env attiny1616_boot_analysis, ohne -flto) ...")
    module = module_breakdown()

    head = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--short", "HEAD"],
                          capture_output=True, text=True).stdout.strip()
    stamp = f"Stand {date.today().isoformat()}, nahe Commit `{head}`" if head else date.today().isoformat()

    svg = render_svg(master, module, stamp)
    OUT.write_text(svg, encoding="utf-8")
    print(f"geschrieben: {OUT.relative_to(REPO)}")

    readme = REPO / "firmware" / "README.md"
    if not readme.exists():
        readme.write_text(README_MD, encoding="utf-8")
        print(f"geschrieben: {readme.relative_to(REPO)}")
    else:
        print(f"unveraendert: {readme.relative_to(REPO)} (existiert bereits)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
