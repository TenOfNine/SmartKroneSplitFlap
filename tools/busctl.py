#!/usr/bin/env python3
"""busctl.py -- Kommandozeilenwerkzeug fuer den KRONE-REW-Bus (Backlog T9).

Fuer die stufenweise Inbetriebnahme nach docs/spezifikation.md Kapitel 10:
Enumeration ausloesen, Modulstatus abfragen, Zielblatt setzen, Rohrahmen
mitschneiden. Nutzt einen USB-RS-485-Adapter (pyserial) oder -- fuer Tests und
ohne Hardware -- einen Loopback (TX auf RX).

Das Rahmenformat ist dieselbe Spezifikation wie in firmware/module/lib/protocol
(5.3): Praeambel 0xAA 0x55, LEN, CMD, ADDR, PAYLOAD, CRC16/MODBUS little endian.

Beispiele:
    tools/busctl.py --port /dev/ttyUSB0 enum
    tools/busctl.py --port /dev/ttyUSB0 status 3
    tools/busctl.py --port /dev/ttyUSB0 show 13 3 40 1 1
    tools/busctl.py --port /dev/ttyUSB0 sniff 10
    tools/busctl.py selftest          # ohne Hardware
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass, field

# --- Protokoll (Spiegel von firmware/module/lib/protocol) -----------------

PREAMBLE = (0xAA, 0x55)
LEN_OVERHEAD = 4
MAX_PAYLOAD = 32
ADDR_BROADCAST = 0
ADDR_MIN = 1
ADDR_MAX = 250
ADDR_SERVICE = 250

CMD = {
    "SET": 0x01, "SET_ALL": 0x02, "GO": 0x03, "STOP": 0x04,
    "GET_STATUS": 0x10, "HOME": 0x20, "SET_CONFIG": 0x30, "GET_CONFIG": 0x31,
    "IDENTIFY": 0x40, "ENUM_RESET": 0x50, "ENUM_ASSIGN": 0x51,
    "ENUM_DONE": 0x52, "GET_UID": 0x53, "PING": 0xF0,
}
CMD_NAME = {v: k for k, v in CMD.items()}

STATE_NAME = {0: "idle", 1: "homing", 2: "moving", 3: "fehler"}


def crc16(data: bytes) -> int:
    """CRC16/MODBUS: Polynom 0xA001, Startwert 0xFFFF."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return crc & 0xFFFF


def encode(cmd: int, addr: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("Payload zu gross")
    length = LEN_OVERHEAD + len(payload)
    body = bytes([length, cmd, addr]) + payload
    crc = crc16(body)
    return bytes(PREAMBLE) + body + bytes([crc & 0xFF, crc >> 8])


@dataclass
class Frame:
    cmd: int
    addr: int
    payload: bytes = b""

    def __str__(self) -> str:
        name = CMD_NAME.get(self.cmd, f"0x{self.cmd:02X}")
        return f"{name} addr={self.addr} payload={self.payload.hex(' ') or '-'}"


class Parser:
    """Bytesynchroner Parser, gleiche Logik wie proto_parser_feed in C."""

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self._buf = bytearray()
        self._need = 0
        self._sync = 0

    def feed(self, byte: int) -> Frame | None:
        if not self._buf:
            if self._sync == 0:
                if byte == PREAMBLE[0]:
                    self._sync = 1
                return None
            if byte == PREAMBLE[1]:
                self._buf = bytearray(PREAMBLE)
                self._need = 0
                return None
            if byte != PREAMBLE[0]:
                self._sync = 0
            return None

        self._buf.append(byte)
        if len(self._buf) == 3:
            length = self._buf[2]
            if length < LEN_OVERHEAD or length > LEN_OVERHEAD + MAX_PAYLOAD:
                self.reset()
                raise ValueError(f"LEN {length} ausserhalb des Bereichs")
            self._need = 3 + length
            return None

        if self._need and len(self._buf) == self._need:
            body = bytes(self._buf[2:self._need - 2])
            recv = self._buf[self._need - 2] | (self._buf[self._need - 1] << 8)
            frame = Frame(self._buf[3], self._buf[4], bytes(self._buf[5:self._need - 2]))
            self.reset()
            if crc16(body) != recv:
                raise ValueError("CRC-Fehler")
            return frame
        return None

    def feed_bytes(self, data: bytes) -> list[Frame]:
        out: list[Frame] = []
        for b in data:
            try:
                f = self.feed(b)
            except ValueError:
                continue
            if f is not None:
                out.append(f)
        return out


# --- Transport -----------------------------------------------------------

class Transport:
    def write(self, data: bytes) -> None:
        raise NotImplementedError

    def read(self, timeout: float) -> bytes:
        raise NotImplementedError

    def close(self) -> None:
        pass


class SerialTransport(Transport):
    def __init__(self, port: str, baud: int) -> None:
        import serial  # nur bei echter Hardware benoetigt

        self._s = serial.Serial(port, baud, timeout=0)

    def write(self, data: bytes) -> None:
        self._s.write(data)
        self._s.flush()

    def read(self, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        out = bytearray()
        while time.monotonic() < deadline:
            chunk = self._s.read(256)
            if chunk:
                out.extend(chunk)
                deadline = time.monotonic() + 0.01
            else:
                time.sleep(0.001)
        return bytes(out)

    def close(self) -> None:
        self._s.close()


class LoopbackTransport(Transport):
    """Alles Gesendete erscheint sofort im Empfangspuffer (TX auf RX)."""

    def __init__(self) -> None:
        self._rx = bytearray()

    def write(self, data: bytes) -> None:
        self._rx.extend(data)

    def read(self, timeout: float) -> bytes:
        data = bytes(self._rx)
        self._rx.clear()
        return data


class _SimModule:
    """Minimalnachbildung einer Daughter Card fuer den Trockentest."""

    def __init__(self, index: int) -> None:
        self.index = index
        self.address: int | None = None
        self.enumerating = False
        self.chain_out = False
        self.ist = 1
        self.ziel = 1
        self.zustand = 0
        self.fehler = 0
        self.blattzahl = 40
        self.uid = bytes((0xA0 + index,) + tuple(range(9)))

    def handle(self, f: "Frame", chain_in: bool) -> "Frame | None":
        if f.cmd == CMD["ENUM_RESET"]:
            self.enumerating = True
            self.address = None
            self.chain_out = False
            return None
        if f.cmd == CMD["ENUM_ASSIGN"]:
            if self.enumerating and chain_in and not self.chain_out and f.payload:
                self.address = f.payload[0]
                self.chain_out = True
                self.enumerating = False
                return Frame(CMD["ENUM_ASSIGN"], self.address)
            return None
        if f.cmd == CMD["ENUM_DONE"]:
            self.enumerating = False
            return None

        addressed = f.addr == self.address and self.address is not None
        broadcast = f.addr == ADDR_BROADCAST

        if f.cmd == CMD["SET_ALL"] and broadcast and self.address:
            if 1 <= self.address <= len(f.payload):
                self.ziel = f.payload[self.address - 1]
            return None
        if f.cmd == CMD["GO"] and broadcast:
            self.ist = self.ziel
            return None
        if not addressed:
            return None
        if f.cmd == CMD["SET"]:
            self.ziel = f.payload[0]
            return Frame(CMD["SET"], self.address)
        if f.cmd == CMD["GET_STATUS"]:
            k = 0
            return Frame(CMD["GET_STATUS"], self.address, bytes(
                [self.ist, self.ziel, self.zustand, self.fehler, self.blattzahl,
                 k & 0xFF, k >> 8, 1]))
        if f.cmd == CMD["GET_UID"]:
            return Frame(CMD["GET_UID"], self.address, self.uid)
        if f.cmd == CMD["PING"]:
            return Frame(CMD["PING"], self.address, bytes([1]))
        if f.cmd in (CMD["HOME"], CMD["STOP"], CMD["SET_CONFIG"]):
            return Frame(f.cmd, self.address)
        return None


class SimBusTransport(Transport):
    """Bus mit simulierten Modulen (CHAIN-Kette). Fuer Tests ohne Hardware."""

    def __init__(self, n_modules: int) -> None:
        self._mods = [_SimModule(i) for i in range(n_modules)]
        # als bereits enumeriert starten, damit Einzelkommandos ohne
        # vorherige Enumeration funktionieren
        for i, m in enumerate(self._mods, start=1):
            m.address = i
        self._parser = Parser()
        self._rx = bytearray()

    def write(self, data: bytes) -> None:
        for f in self._parser.feed_bytes(data):
            chain = True  # Master-CHAIN aktiv
            for m in self._mods:
                resp = m.handle(f, chain)
                if resp is not None:
                    self._rx.extend(encode(resp.cmd, resp.addr, resp.payload))
                    if resp.cmd == CMD["ENUM_ASSIGN"]:
                        break  # pro ENUM_ASSIGN uebernimmt genau eine Karte
                chain = m.chain_out

    def read(self, timeout: float) -> bytes:
        data = bytes(self._rx)
        self._rx.clear()
        return data


# --- Bus ---------------------------------------------------------------

@dataclass
class Bus:
    transport: Transport
    response_timeout: float = 0.05
    parser: Parser = field(default_factory=Parser)
    verbose: bool = False

    def send(self, cmd: int, addr: int, payload: bytes = b"") -> bytes:
        raw = encode(cmd, addr, payload)
        if self.verbose:
            print(f"  TX {raw.hex(' ')}  ({CMD_NAME.get(cmd, cmd)} addr={addr})")
        self.transport.write(raw)
        return raw

    def recv(self, timeout: float | None = None) -> list[Frame]:
        data = self.transport.read(timeout if timeout is not None else self.response_timeout)
        if self.verbose and data:
            print(f"  RX {data.hex(' ')}")
        return self.parser.feed_bytes(data)

    # -- Kommandos --

    def ping(self, addr: int) -> int | None:
        self.send(CMD["PING"], addr)
        for f in self.recv():
            if f.cmd == CMD["PING"]:
                return f.payload[0] if f.payload else 0
        return None

    def status(self, addr: int) -> dict | None:
        self.send(CMD["GET_STATUS"], addr)
        for f in self.recv():
            if f.cmd == CMD["GET_STATUS"] and len(f.payload) >= 8:
                p = f.payload
                return {
                    "addr": addr, "ist": p[0], "ziel": p[1],
                    "zustand": STATE_NAME.get(p[2], p[2]), "fehler": p[3],
                    "blattzahl": p[4], "korrektur": p[5] | (p[6] << 8),
                    "fw": p[7],
                }
        return None

    def get_uid(self, addr: int) -> bytes | None:
        self.send(CMD["GET_UID"], addr)
        for f in self.recv():
            if f.cmd == CMD["GET_UID"]:
                return f.payload
        return None

    def set_target(self, addr: int, blatt: int) -> bool:
        self.send(CMD["SET"], addr, bytes([blatt]))
        return any(f.cmd == CMD["SET"] for f in self.recv())

    def show(self, blaetter: list[int]) -> None:
        self.send(CMD["SET_ALL"], ADDR_BROADCAST, bytes(blaetter))
        self.send(CMD["GO"], ADDR_BROADCAST)

    def home(self, addr: int = ADDR_BROADCAST) -> None:
        self.send(CMD["HOME"], addr)
        self.recv()

    def stop(self, addr: int = ADDR_BROADCAST) -> None:
        self.send(CMD["STOP"], addr)
        self.recv()

    def set_config(self, addr: int, blattzahl: int, offset: int,
                   vorhalt: int, flags: int) -> bool:
        self.send(CMD["SET_CONFIG"], addr, bytes([blattzahl, offset, vorhalt, flags]))
        return any(f.cmd == CMD["SET_CONFIG"] for f in self.recv())

    def enumerate(self, max_modules: int = ADDR_MAX) -> list[int]:
        self.send(CMD["ENUM_RESET"], ADDR_BROADCAST)
        time.sleep(0.005)
        self.recv()
        assigned: list[int] = []
        for addr in range(ADDR_MIN, max_modules + 1):
            self.send(CMD["ENUM_ASSIGN"], ADDR_BROADCAST, bytes([addr]))
            acked = any(f.cmd == CMD["ENUM_ASSIGN"] for f in self.recv())
            if not acked:
                break
            assigned.append(addr)
        self.send(CMD["ENUM_DONE"], ADDR_BROADCAST)
        self.recv()
        return assigned

    def sniff(self, seconds: float) -> list[tuple[float, Frame]]:
        start = time.monotonic()
        seen: list[tuple[float, Frame]] = []
        while time.monotonic() - start < seconds:
            for f in self.recv(timeout=0.2):
                seen.append((time.monotonic() - start, f))
        return seen


# --- Selbsttest (ohne Hardware) --------------------------------------

def selftest() -> int:
    ok = True

    def check(cond: bool, msg: str) -> None:
        nonlocal ok
        print(f"  [{'ok' if cond else 'FEHLER'}] {msg}")
        ok = ok and cond

    # 1. CRC-Pruefwert (identisch mit firmware/module/test/test_crc)
    check(crc16(b"123456789") == 0x4B37, "CRC16/MODBUS Pruefwert 0x4B37")

    # 2. Golden Frame (identisch mit test_frame.c test_encode_layout):
    #    SET, addr 3, payload [40]  ->  AA 55 05 01 03 28 <crc>
    raw = encode(CMD["SET"], 3, bytes([40]))
    check(raw[:6] == bytes([0xAA, 0x55, 0x05, 0x01, 0x03, 0x28]),
          "Rahmenkopf SET addr=3 payload=40")
    check(len(raw) == 8, "Rahmenlaenge 8")

    # 3. Encode -> Parse Roundtrip ueber einen Loopback (TX auf RX)
    bus = Bus(LoopbackTransport())
    cases = [
        (CMD["GO"], ADDR_BROADCAST, b""),
        (CMD["SET"], 5, bytes([12])),
        (CMD["SET_ALL"], 0, bytes(range(1, 11))),
        (CMD["SET_CONFIG"], 250, bytes([40, 0, 5, 3])),
        (CMD["GET_UID"], 1, b""),
    ]
    for cmd, addr, pl in cases:
        bus.parser.reset()
        bus.send(cmd, addr, pl)
        frames = bus.recv(timeout=0)
        good = (len(frames) == 1 and frames[0].cmd == cmd and
                frames[0].addr == addr and frames[0].payload == pl)
        check(good, f"Roundtrip {CMD_NAME[cmd]} addr={addr} len={len(pl)}")

    # 4. Fehlerhafter CRC wird verworfen
    bus.parser.reset()
    bad = bytearray(encode(CMD["PING"], 1))
    bad[-1] ^= 0xFF
    check(bus.parser.feed_bytes(bytes(bad)) == [], "verfaelschter CRC wird verworfen")

    # 5. Resynchronisation nach Muell
    bus.parser.reset()
    junk = bytes([0x00, 0xFF, 0xAA, 0x12])
    frames = bus.parser.feed_bytes(junk + encode(CMD["STOP"], 7))
    check(len(frames) == 1 and frames[0].cmd == CMD["STOP"] and frames[0].addr == 7,
          "Resync nach Stoerbytes")

    # 6. Gegen simulierte Module: Enumeration, Status, UID
    sim = Bus(SimBusTransport(3))
    check(sim.enumerate() == [1, 2, 3], "Enumeration von 3 Modulen -> [1, 2, 3]")
    st = sim.status(2)
    check(st is not None and st["blattzahl"] == 40, "Status von Modul 2")
    check(sim.ping(3) == 1, "PING Modul 3 -> FW 1")
    uids = {bytes(sim.get_uid(a)) for a in (1, 2, 3)}
    check(len(uids) == 3, "GET_UID: drei verschiedene Seriennummern")
    sim.show([13, 14, 15])
    st = sim.status(1)
    check(st is not None and st["ziel"] == 13, "SET_ALL + GO wirkt auf Modul 1")

    print("\n" + ("selftest bestanden" if ok else "selftest FEHLGESCHLAGEN"))
    return 0 if ok else 1


# --- CLI ------------------------------------------------------------

def build_transport(args: argparse.Namespace) -> Transport:
    if args.sim is not None:
        return SimBusTransport(args.sim)
    if args.loopback:
        return LoopbackTransport()
    if not args.port:
        sys.exit("Kein --port angegeben (oder --sim N / --loopback fuer Tests).")
    return SerialTransport(args.port, args.baud)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serieller Port des USB-RS485-Adapters")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--loopback", action="store_true", help="TX auf RX, ohne Hardware")
    ap.add_argument("--sim", type=int, metavar="N",
                    help="N simulierte Module (Trockentest ohne Hardware)")
    ap.add_argument("-v", "--verbose", action="store_true", help="Rohrahmen anzeigen")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("selftest", help="Rahmenlogik ohne Hardware pruefen")
    sub.add_parser("enum", help="Enumeration ausloesen")
    p = sub.add_parser("status", help="Modulstatus abfragen"); p.add_argument("addr", type=int)
    p = sub.add_parser("uid", help="Seriennummer lesen"); p.add_argument("addr", type=int)
    p = sub.add_parser("ping", help="Modul anpingen"); p.add_argument("addr", type=int)
    p = sub.add_parser("set", help="Zielblatt setzen (SET)")
    p.add_argument("addr", type=int); p.add_argument("blatt", type=int)
    p = sub.add_parser("show", help="alle Module setzen (SET_ALL + GO)")
    p.add_argument("blatt", type=int, nargs="+")
    p = sub.add_parser("home", help="Homing"); p.add_argument("addr", type=int, nargs="?", default=0)
    p = sub.add_parser("stop", help="Stop"); p.add_argument("addr", type=int, nargs="?", default=0)
    p = sub.add_parser("config", help="Konfiguration schreiben (SET_CONFIG)")
    p.add_argument("addr", type=int)
    p.add_argument("blattzahl", type=int)
    p.add_argument("offset", type=int)
    p.add_argument("vorhalt", type=int)
    p.add_argument("flags", type=lambda s: int(s, 0))
    p = sub.add_parser("sniff", help="Rohrahmen mitschneiden")
    p.add_argument("seconds", type=float, nargs="?", default=5.0)

    args = ap.parse_args(argv)

    if args.cmd == "selftest":
        return selftest()

    bus = Bus(build_transport(args), verbose=args.verbose)
    try:
        if args.cmd == "enum":
            addrs = bus.enumerate()
            print(f"{len(addrs)} Module: {addrs}")
        elif args.cmd == "status":
            st = bus.status(args.addr)
            print(st if st else "keine Antwort")
        elif args.cmd == "uid":
            uid = bus.get_uid(args.addr)
            print(uid.hex(" ") if uid else "keine Antwort")
        elif args.cmd == "ping":
            v = bus.ping(args.addr)
            print(f"FW-Version {v}" if v is not None else "keine Antwort")
        elif args.cmd == "set":
            print("ACK" if bus.set_target(args.addr, args.blatt) else "keine Antwort")
        elif args.cmd == "show":
            bus.show(args.blatt)
            print(f"SET_ALL {args.blatt} + GO gesendet")
        elif args.cmd == "home":
            bus.home(args.addr)
            print("HOME gesendet")
        elif args.cmd == "stop":
            bus.stop(args.addr)
            print("STOP gesendet")
        elif args.cmd == "config":
            ok = bus.set_config(args.addr, args.blattzahl, args.offset,
                                args.vorhalt, args.flags)
            print("ACK" if ok else "keine Antwort")
        elif args.cmd == "sniff":
            for t, f in bus.sniff(args.seconds):
                print(f"  {t:7.3f}s  {f}")
    finally:
        bus.transport.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
