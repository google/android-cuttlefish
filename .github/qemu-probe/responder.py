#!/usr/bin/env python3
"""Stands in for the cuttlefish host daemons on the guest's hvc ports.

Serves the given unix sockets, which boot.sh wires to the virtio
consoles, and answers the two requests the guest will not boot without:
u-boot's oemlock queries (the device is unlocked, everything else is
allowed) and the sensors HAL's list-sensors (no sensors). Anything else
gets the same one-byte answer, and everything is logged with the port it
came in on.
"""
import os
import selectors
import socket
import struct
import sys
import time

OEMLOCK_LOCKED = 3
SENSORS_UPDATE_HAL = 2
RESPONSE_BIT = 0x80000000

sel = selectors.DefaultSelector()
start = time.time()


def log(msg):
    print(f"[{time.time() - start:6.1f}s] {msg}", flush=True)


def readn(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise EOFError
        buf += chunk
    return buf


def answer(path, conn):
    # cuttlefish/common/libs/transport: a header of command (31 bits),
    # response flag and payload size, then the payload.
    word, size = struct.unpack("<II", readn(conn, 8))
    payload = readn(conn, size) if size else b""
    log(f"{path}: command {word & ~RESPONSE_BIT} payload {payload!r}")
    if payload.startswith(b"list-sensors"):
        conn.sendall(struct.pack("<II", SENSORS_UPDATE_HAL, 2) + b"0\n")
    else:
        value = 0 if word & ~RESPONSE_BIT == OEMLOCK_LOCKED else 1
        conn.sendall(struct.pack("<II", word | RESPONSE_BIT, 1)
                     + bytes([value]))


for path in sys.argv[1:]:
    if os.path.exists(path):
        os.unlink(path)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind(path)
    s.listen(1)
    sel.register(s, selectors.EVENT_READ, ("listen", path))
while True:
    for key, _ in sel.select():
        kind, path = key.data
        if kind == "listen":
            conn, _ = key.fileobj.accept()
            sel.register(conn, selectors.EVENT_READ, ("conn", path))
            continue
        try:
            answer(path, key.fileobj)
        except (EOFError, ConnectionError, struct.error):
            log(f"{path}: closed")
            sel.unregister(key.fileobj)
            key.fileobj.close()
