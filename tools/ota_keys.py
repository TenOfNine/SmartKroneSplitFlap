#!/usr/bin/env python3
"""Schluesselverwaltung und Signatur fuer das Browser-OTA der Zentralsteuerung.

Container `.kota` (KRONE-OTA, Little-Endian):

    Offset  Laenge  Inhalt
    0       4       Magic "KRN1"
    4       1       Formatversion (=1)
    5       3       reserviert (0)
    8       4       img_len  -- Laenge des App-Images
    12      32      SHA-256 des App-Images
    44      64      ECDSA-P-256-Signatur (r||s, je 32 Byte) ueber Byte 0..43
    108     img_len App-Image (firmware.bin)

Der private Schluessel liegt NIE im Repo. Standardpfad:
``~/.config/krone/ota-signing.pem`` oder Umgebungsvariable ``KRONE_OTA_KEY``
(Pfad oder PEM-Inhalt). Der oeffentliche Schluessel wird als C-Header nach
``firmware/master/lib/otaverify/ota_pubkey.h`` geschrieben und committet.

    python tools/ota_keys.py init [--force]      Schluesselpaar anlegen + Header
    python tools/ota_keys.py pubkey              Header aus vorhandenem Key neu schreiben
    python tools/ota_keys.py sign IN.bin OUT.kota
    python tools/ota_keys.py verify IN.kota      Container gegen ota_pubkey.h pruefen (kein privater Key noetig)
"""
from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.asymmetric.ec import ECDSA
from cryptography.hazmat.primitives.hashes import SHA256

REPO = Path(__file__).resolve().parent.parent
PUBKEY_H = REPO / "firmware" / "master" / "lib" / "otaverify" / "ota_pubkey.h"
DEFAULT_KEY = Path.home() / ".config" / "krone" / "ota-signing.pem"

MAGIC = b"KRN1"
FORMAT_VERSION = 1
HEADER_LEN = 108
SIGNED_LEN = 44


def _load_private() -> ec.EllipticCurvePrivateKey:
    env = os.environ.get("KRONE_OTA_KEY", "")
    if env and "BEGIN" in env:
        data = env.encode()
    else:
        p = Path(env) if env else DEFAULT_KEY
        if not p.is_file():
            sys.exit(
                f"Privater OTA-Schluessel nicht gefunden: {p}\n"
                f"Mit 'python tools/ota_keys.py init' anlegen."
            )
        data = p.read_bytes()
    key = serialization.load_pem_private_key(data, password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        sys.exit("Schluessel ist kein ECDSA P-256 (secp256r1).")
    return key


def _pub_point(pub: ec.EllipticCurvePublicKey) -> bytes:
    """65 Byte, unkomprimiert: 0x04 || X(32) || Y(32)."""
    return pub.public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )


def _write_header(pub: ec.EllipticCurvePublicKey) -> None:
    raw = _pub_point(pub)
    assert len(raw) == 65
    body = ",\n    ".join(
        ", ".join(f"0x{b:02x}" for b in raw[i : i + 12]) for i in range(0, 65, 12)
    )
    PUBKEY_H.parent.mkdir(parents=True, exist_ok=True)
    PUBKEY_H.write_text(
        "/* Erzeugt von tools/ota_keys.py -- nicht von Hand bearbeiten.\n"
        " * Oeffentlicher OTA-Signaturschluessel (ECDSA P-256, unkomprimierter Punkt).\n"
        " * Der zugehoerige private Schluessel liegt ausserhalb des Repos. */\n"
        "#ifndef KRONE_OTA_PUBKEY_H\n#define KRONE_OTA_PUBKEY_H\n\n"
        "#include <stdint.h>\n\n"
        "static const uint8_t OTA_PUBKEY[65] = {\n    "
        + body
        + "\n};\n\n#endif\n",
        encoding="utf-8",
    )
    print(f"geschrieben: {PUBKEY_H.relative_to(REPO)}")


def cmd_init(args: argparse.Namespace) -> int:
    if DEFAULT_KEY.exists() and not args.force:
        sys.exit(f"{DEFAULT_KEY} existiert schon. --force ueberschreibt (Signaturen brechen!).")
    key = ec.generate_private_key(ec.SECP256R1())
    DEFAULT_KEY.parent.mkdir(parents=True, exist_ok=True)
    DEFAULT_KEY.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    DEFAULT_KEY.chmod(0o600)
    print(f"privater Schluessel: {DEFAULT_KEY} (chmod 600) -- sichern, nicht committen!")
    _write_header(key.public_key())
    return 0


def cmd_pubkey(_args: argparse.Namespace) -> int:
    _write_header(_load_private().public_key())
    return 0


def cmd_sign(args: argparse.Namespace) -> int:
    key = _load_private()
    image = Path(args.infile).read_bytes()
    if not args.raw and image[:1] != b"\xe9":
        sys.exit("Eingabe ist kein ESP32-App-Image (Magic 0xE9 fehlt). "
                 "Fuer ein Rohbinaer (z. B. ATtiny) --raw angeben.")
    digest = hashlib.sha256(image).digest()
    header = bytearray(HEADER_LEN)
    header[0:4] = MAGIC
    header[4] = FORMAT_VERSION
    struct.pack_into("<I", header, 8, len(image))
    header[12:44] = digest

    der = key.sign(bytes(header[:SIGNED_LEN]), ECDSA(SHA256()))
    r, s = utils.decode_dss_signature(der)
    header[44:76] = r.to_bytes(32, "big")
    header[76:108] = s.to_bytes(32, "big")

    out = Path(args.outfile)
    out.write_bytes(bytes(header) + image)
    print(
        f"geschrieben: {out}  ({out.stat().st_size} B, Image {len(image)} B, "
        f"sha256 {digest.hex()[:12]}…)"
    )
    return 0


def _pubkey_from_header() -> ec.EllipticCurvePublicKey:
    txt = PUBKEY_H.read_text()
    hexes = [h for h in txt.replace("\n", " ").split() if h.startswith("0x")]
    raw = bytes(int(h.rstrip(","), 16) for h in hexes)
    if len(raw) != 65 or raw[0] != 0x04:
        sys.exit(f"{PUBKEY_H} enthaelt keinen 65-Byte-Punkt.")
    return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), raw)


def cmd_verify(args: argparse.Namespace) -> int:
    blob = Path(args.infile).read_bytes()
    if len(blob) < HEADER_LEN or blob[:4] != MAGIC or blob[4] != FORMAT_VERSION:
        sys.exit("Kein gueltiger .kota-Header (Magic/Version).")
    img_len = struct.unpack_from("<I", blob, 8)[0]
    image = blob[HEADER_LEN:]
    if len(image) != img_len:
        sys.exit(f"img_len {img_len} != tatsaechliche Imagelaenge {len(image)}.")
    if hashlib.sha256(image).digest() != blob[12:44]:
        sys.exit("SHA-256 des Images passt nicht zum Header.")
    r = int.from_bytes(blob[44:76], "big")
    s = int.from_bytes(blob[76:108], "big")
    try:
        _pubkey_from_header().verify(
            utils.encode_dss_signature(r, s), bytes(blob[:SIGNED_LEN]), ECDSA(SHA256()))
    except Exception:
        sys.exit("Signatur ungueltig (passt nicht zu ota_pubkey.h).")
    print(f"OK: {args.infile} — Signatur gueltig, Image {img_len} B, "
          f"sha256 {blob[12:44].hex()[:12]}…")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("init", help="Schluesselpaar anlegen + Header schreiben")
    p.add_argument("--force", action="store_true")
    p.set_defaults(fn=cmd_init)
    sub.add_parser("pubkey", help="Header aus vorhandenem Key neu schreiben").set_defaults(fn=cmd_pubkey)
    p = sub.add_parser("sign", help="App-Image signieren -> .kota / .mota")
    p.add_argument("infile")
    p.add_argument("outfile")
    p.add_argument("--raw", action="store_true",
                   help="Rohbinaer ohne ESP-Magic (z. B. ATtiny-App fuer .mota)")
    p.set_defaults(fn=cmd_sign)
    p = sub.add_parser("verify", help="Container gegen ota_pubkey.h pruefen")
    p.add_argument("infile")
    p.set_defaults(fn=cmd_verify)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())
