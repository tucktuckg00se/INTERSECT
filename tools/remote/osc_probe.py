#!/usr/bin/env python3
"""Minimal INTERSECT remote-control client (docs/remote-control-protocol.md).

Standard library only, so it doubles as a reference for writing your own controller.

    python3 tools/remote/osc_probe.py                 # list instances, subscribe, print state
    python3 tools/remote/osc_probe.py select 2        # select slice 2
    python3 tools/remote/osc_probe.py bounds 0 1000 50000
    python3 tools/remote/osc_probe.py peaks 64        # ASCII waveform, 64 columns
    python3 tools/remote/osc_probe.py raw /intersect/undo
"""
from __future__ import annotations

import json
import os
import socket
import struct
import sys
import time
from pathlib import Path

PREFIX = "/intersect/"
FIXED_PORTS = range(9871, 9879)


# --- OSC 1.0 encoding ---------------------------------------------------------

def _pad(data: bytes) -> bytes:
    return data + b"\0" * (4 - len(data) % 4)


def _blob_pad(data: bytes) -> bytes:
    return data + b"\0" * ((4 - len(data) % 4) % 4)


def encode(address: str, *args) -> bytes:
    tags, payload = ",", b""
    for a in args:
        if isinstance(a, bool) or isinstance(a, int):
            tags += "i"
            payload += struct.pack(">i", int(a))
        elif isinstance(a, float):
            tags += "f"
            payload += struct.pack(">f", a)
        elif isinstance(a, (bytes, bytearray)):
            tags += "b"
            payload += struct.pack(">i", len(a)) + _blob_pad(bytes(a))
        else:
            tags += "s"
            payload += _pad(str(a).encode())
    return _pad(address.encode()) + _pad(tags.encode()) + payload


def _read_str(data: bytes, pos: int) -> tuple[str, int]:
    end = data.index(b"\0", pos)
    return data[pos:end].decode(errors="replace"), (end // 4 + 1) * 4


def decode(data: bytes) -> list[tuple[str, list]]:
    if data.startswith(b"#bundle"):
        out, pos = [], 16
        while pos < len(data):
            (size,) = struct.unpack_from(">i", data, pos)
            out += decode(data[pos + 4:pos + 4 + size])
            pos += 4 + size
        return out
    address, pos = _read_str(data, 0)
    tags, pos = _read_str(data, pos)
    args: list = []
    for t in tags[1:]:
        if t == "i":
            args.append(struct.unpack_from(">i", data, pos)[0]); pos += 4
        elif t == "f":
            args.append(struct.unpack_from(">f", data, pos)[0]); pos += 4
        elif t == "s":
            s, pos = _read_str(data, pos); args.append(s)
        elif t == "b":
            (n,) = struct.unpack_from(">i", data, pos)
            args.append(data[pos + 4:pos + 4 + n]); pos += 4 + n + (4 - n % 4) % 4
    return [(address, args)]


# --- Discovery ------------------------------------------------------------------

def instances_dir() -> Path:
    state = os.environ.get("XDG_STATE_HOME") or str(Path.home() / ".local/state")
    return Path(state) / "intersect" / "instances"


def discover() -> list[dict]:
    found = []
    for f in sorted(instances_dir().glob("*.json")):
        try:
            info = json.loads(f.read_text())
            os.kill(int(info["pid"]), 0)
            found.append(info)
        except (OSError, ValueError, KeyError):
            continue
    return found


class Client:
    def __init__(self, port: int):
        self.server = ("127.0.0.1", port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.3)
        self.reply_port = self.sock.getsockname()[1]

    def send(self, address: str, *args):
        if not address.startswith("/"):
            address = PREFIX + address
        self.sock.sendto(encode(address, *args), self.server)

    def receive(self, seconds: float):
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                data, _ = self.sock.recvfrom(65536)
            except socket.timeout:
                continue
            yield from decode(data)

    def hello(self) -> list | None:
        self.send("hello", "osc_probe", self.reply_port)
        for address, args in self.receive(1.0):
            if address == PREFIX + "hello/ack":
                return args
        return None


def connect() -> Client:
    instances = discover()
    candidates = [i["port"] for i in instances] or list(FIXED_PORTS)
    for info in instances:
        print(f"instance {info['instanceId'][:8]} port {info['port']} track '{info.get('trackName', '')}'")
    for port in candidates:
        client = Client(port)
        ack = client.hello()
        if ack:
            print(f"connected to port {port}: protocol {ack[0]}, INTERSECT {ack[2]}, track '{ack[3]}'")
            return client
    sys.exit("no INTERSECT instance answered")


def show(address: str, args: list):
    short = address[len(PREFIX):]
    shown = [f"<{len(a)} bytes>" if isinstance(a, bytes) else a for a in args]
    print(short, *shown)


def ascii_peaks(blob: bytes, rows: int = 9):
    pairs = [struct.unpack_from("bb", blob, i) for i in range(0, len(blob), 2)]
    half = rows // 2
    for r in range(half, -half - 1, -1):
        level = r / half * 127
        print("".join("#" if lo <= level <= hi else " " for lo, hi in pairs))


def main(argv: list[str]):
    client = connect()
    command = argv[0] if argv else "watch"

    if command == "select":
        client.send("slice/select", int(argv[1]))
    elif command == "bounds":
        client.send("slice/bounds", int(argv[1]), int(argv[2]), int(argv[3]))
    elif command == "describe":
        client.send("params/describe", client.reply_port)
    elif command == "peaks":
        width = int(argv[1]) if len(argv) > 1 else 64
        total = 0
        for address, args in client.receive(0.5):
            if address == PREFIX + "state/global":
                total = args[6]
        client.send("view/peaks", client.reply_port, 1, 0, total, width)
        for address, args in client.receive(1.0):
            if address == PREFIX + "peaks":
                ascii_peaks(args[3])
                break
        return
    elif command == "raw":
        def arg(v: str):
            for kind in (int, float):
                try:
                    return kind(v)
                except ValueError:
                    pass
            return v
        client.send(argv[1], *[arg(v) for v in argv[2:]])

    last_ping = time.time()
    try:
        for address, args in client.receive(3600 if command == "watch" else 1.5):
            show(address, args)
            if time.time() - last_ping > 1.5:
                client.send("ping", client.reply_port)
                last_ping = time.time()
    except KeyboardInterrupt:
        pass
    finally:
        client.send("bye", client.reply_port)


if __name__ == "__main__":
    main(sys.argv[1:])
