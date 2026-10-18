#!/usr/bin/env python3
"""Wurzelzertifikate fuer den GitHub-Abruf der Zentralsteuerung (#38).

Erzeugt firmware/master/src/ca_bundle.h: ausgewaehlte Wurzel-CAs aus dem
Mozilla-Bestand (Python-Paket certifi) im Format, das
WiFiClientSecure::setCACertBundle() des Arduino-ESP32-Kerns erwartet
(libraries/WiFiClientSecure/src/esp_crt_bundle.c, wie IDF gen_crt_bundle.py):

    uint16 Anzahl (big endian), dann je Zertifikat
    uint16 Namenslaenge, uint16 Schluessellaenge (big endian),
    Subject (DER), oeffentlicher Schluessel (DER, SubjectPublicKeyInfo),
    sortiert nach Subject (binaere Suche im Kern).

Nur grosse oeffentliche Wurzeln, unter denen GitHub (raw.githubusercontent.com)
realistisch ausstellen laesst; wechselt GitHub zu einer anderen CA, scheitert
der Abruf sichtbar (TLS-Fehler) statt unsicher, der Browser-Upload bleibt.

Aufruf:
    python tools/gen_ca_bundle.py            # Header schreiben
    python tools/gen_ca_bundle.py --list     # Auswahl anzeigen
"""
import argparse
import struct
import sys
from pathlib import Path

import certifi
from cryptography import x509
from cryptography.hazmat.primitives import serialization

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "firmware" / "master" / "src" / "ca_bundle.h"

# Teilstrings des Subjects (RFC 4514), Gross-/Kleinschreibung wie im Bestand.
WANT = [
    "CN=ISRG Root X1,", "CN=ISRG Root X2,",
    "CN=USERTrust ECC Certification Authority,", "CN=USERTrust RSA Certification Authority,",
    "CN=Sectigo Public Server Authentication Root E46,",
    "CN=Sectigo Public Server Authentication Root R46,",
    "CN=DigiCert Global Root G2,", "CN=DigiCert Global Root G3,",
    "CN=DigiCert TLS ECC P384 Root G5,", "CN=DigiCert TLS RSA4096 Root G5,",
    "CN=GlobalSign Root E46,", "CN=GlobalSign Root R46,",
    "OU=GlobalSign Root CA - R3", "OU=GlobalSign Root CA - R6",
    "OU=GlobalSign ECC Root CA - R4", "OU=GlobalSign ECC Root CA - R5",
    "CN=GTS Root R1,", "CN=GTS Root R3,", "CN=GTS Root R4,",
    "CN=Amazon Root CA 1,", "CN=Amazon Root CA 3,",
]


def select() -> list:
    certs = x509.load_pem_x509_certificates(Path(certifi.where()).read_bytes())
    picked = []
    for w in WANT:
        hits = [c for c in certs if w in c.subject.rfc4514_string() + ","]
        if len(hits) != 1:
            sys.exit(f"{w!r}: {len(hits)} Treffer im certifi-Bestand {certifi.__version__}")
        picked.append(hits[0])
    return picked


def bundle(certs: list) -> bytes:
    entries = []
    for c in certs:
        name = c.subject.public_bytes()
        key = c.public_key().public_bytes(serialization.Encoding.DER,
                                          serialization.PublicFormat.SubjectPublicKeyInfo)
        entries.append((name, key))
    entries.sort(key=lambda e: e[0])
    out = struct.pack(">H", len(entries))
    for name, key in entries:
        out += struct.pack(">HH", len(name), len(key)) + name + key
    return out


def header(data: bytes, certs: list) -> str:
    names = "\n".join(f" *   {c.subject.rfc4514_string()}" for c in certs)
    rows = []
    for i in range(0, len(data), 16):
        rows.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    return (
        "/* Generiert von tools/gen_ca_bundle.py -- nicht von Hand aendern.\n"
        f" * Quelle: certifi {certifi.__version__} (Mozilla), {len(certs)} Wurzeln:\n"
        f"{names}\n"
        " */\n"
        "#pragma once\n#include <stdint.h>\n\n"
        f"static const uint8_t CA_BUNDLE[{len(data)}] = {{\n" + "\n".join(rows) + "\n};\n"
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="nur Auswahl anzeigen")
    args = ap.parse_args()
    certs = select()
    if args.list:
        for c in certs:
            print(c.subject.rfc4514_string())
        return 0
    data = bundle(certs)
    OUT.write_text(header(data, certs))
    print(f"geschrieben: {OUT.relative_to(REPO)} ({len(certs)} Wurzeln, {len(data)} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
