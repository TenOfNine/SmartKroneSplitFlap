#!/usr/bin/env python3
"""Tests fuer tools/busctl.py (stdlib unittest, kein pytest noetig).

    python tools/test_busctl.py
    python -m unittest tools.test_busctl
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import busctl  # noqa: E402
from busctl import CMD, Bus, Frame, Parser, SimBusTransport, crc16, encode  # noqa: E402


class TestCrc(unittest.TestCase):
    def test_check_value(self):
        self.assertEqual(crc16(b"123456789"), 0x4B37)

    def test_empty(self):
        self.assertEqual(crc16(b""), 0xFFFF)

    def test_single_zero(self):
        self.assertEqual(crc16(b"\x00"), 0x40BF)


class TestFrame(unittest.TestCase):
    def test_golden_set_frame(self):
        # identisch mit firmware/module/test/test_frame.c test_encode_layout
        raw = encode(CMD["SET"], 3, bytes([40]))
        self.assertEqual(raw[:6], bytes([0xAA, 0x55, 0x05, 0x01, 0x03, 0x28]))
        self.assertEqual(len(raw), 8)

    def test_roundtrip(self):
        p = Parser()
        for cmd, addr, pl in [
            (CMD["GO"], 0, b""),
            (CMD["SET_CONFIG"], 250, bytes([40, 0, 5, 3])),
            (CMD["SET_ALL"], 0, bytes(range(1, 33))),
        ]:
            frames = p.feed_bytes(encode(cmd, addr, pl))
            self.assertEqual(len(frames), 1)
            self.assertEqual(frames[0], Frame(cmd, addr, pl))

    def test_bad_crc_rejected(self):
        p = Parser()
        bad = bytearray(encode(CMD["PING"], 1))
        bad[-1] ^= 0xFF
        self.assertEqual(p.feed_bytes(bytes(bad)), [])

    def test_bad_len_rejected(self):
        p = Parser()
        # Praeambel korrekt, LEN = 2 (< Overhead 4)
        self.assertEqual(p.feed_bytes(bytes([0xAA, 0x55, 0x02, 0, 0, 0, 0])), [])

    def test_resync_after_junk(self):
        p = Parser()
        frames = p.feed_bytes(bytes([0x00, 0xFF, 0xAA, 0x13]) + encode(CMD["STOP"], 7))
        self.assertEqual(frames, [Frame(CMD["STOP"], 7, b"")])


class TestSim(unittest.TestCase):
    def test_enumeration(self):
        bus = Bus(SimBusTransport(3))
        self.assertEqual(bus.enumerate(), [1, 2, 3])

    def test_status_and_show(self):
        bus = Bus(SimBusTransport(4))
        bus.show([13, 14, 15, 16])
        st = bus.status(3)
        self.assertIsNotNone(st)
        self.assertEqual(st["ziel"], 15)
        self.assertEqual(st["blattzahl"], 40)

    def test_uid_unique(self):
        bus = Bus(SimBusTransport(5))
        uids = {bytes(bus.get_uid(a)) for a in range(1, 6)}
        self.assertEqual(len(uids), 5)

    def test_ping_and_set(self):
        bus = Bus(SimBusTransport(2))
        self.assertEqual(bus.ping(2), 1)
        self.assertTrue(bus.set_target(1, 20))


class TestSelftest(unittest.TestCase):
    def test_selftest_returns_zero(self):
        self.assertEqual(busctl.selftest(), 0)


if __name__ == "__main__":
    unittest.main()
