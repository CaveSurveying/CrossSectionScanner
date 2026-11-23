#!/usr/bin/env python3
"""Frames of the XScan link with SexyTopo (docs/LINK.md), the reference both ends follow.

  frame(message)        the bytes to send for a message
  FrameReader().feed(data)   messages and problems found in the bytes received so far
"""
import json
import re
import zlib

FRAME_VERSION = b"XS1"
MAX_JSON = 262144
MAX_HEADER = 32
HEADER = re.compile(rb"XS1 (\d{1,9}) ([0-9a-f]{8})")

SERVICE = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
RX = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # SexyTopo to XScan
TX = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # XScan to SexyTopo


def frame(message):
    """A message (a dict with a type) as the bytes of one frame."""
    body = json.dumps(message, separators=(",", ":"), ensure_ascii=False).encode()
    return b"%s %d %08x\n" % (FRAME_VERSION, len(body), zlib.crc32(body)) + body


class FrameReader:
    """Joins received bytes into messages. feed returns what the new bytes completed, in order,
    each ("message", dict) or ("dropped", reason) with reason bad_header, bad_checksum or
    unknown_format."""

    def __init__(self):
        self.buffer = b""

    def feed(self, data):
        self.buffer += data
        found = []
        while True:
            end = self.buffer.find(b"\n")
            if end < 0:
                if len(self.buffer) > MAX_HEADER:
                    found.append(("dropped", "bad_header"))
                    self.skip()
                    continue
                return found
            match = HEADER.fullmatch(self.buffer[:end])
            if not match or int(match[1]) > MAX_JSON:
                found.append(("dropped", "bad_header"))
                self.skip()
                continue
            length, crc = int(match[1]), int(match[2], 16)
            start = end + 1
            if len(self.buffer) < start + length:
                return found
            body, self.buffer = self.buffer[start:start + length], self.buffer[start + length:]
            if zlib.crc32(body) != crc:
                found.append(("dropped", "bad_checksum"))
                continue
            try:
                message = json.loads(body)
            except ValueError:
                message = None
            if not isinstance(message, dict) or not isinstance(message.get("type"), str):
                found.append(("dropped", "unknown_format"))
                continue
            found.append(("message", message))

    def skip(self):
        """Drop bytes up to the next frame start after the first byte, or all but a possible
        partial XS1 at the end."""
        start = FRAME_VERSION + b" "
        next_start = self.buffer.find(start, 1)
        if next_start >= 0:
            self.buffer = self.buffer[next_start:]
            return
        for keep in range(len(start) - 1, 0, -1):
            if self.buffer.endswith(start[:keep]):
                self.buffer = self.buffer[-keep:]
                return
        self.buffer = b""

    def waiting(self):
        """Bytes held for a frame not yet complete, so a caller can drop them after a timeout."""
        return len(self.buffer)

    def clear(self):
        self.buffer = b""
