#!/usr/bin/env python3
"""Convierte los volcados RGB565 (410x502) de tools/sim en PNG. Solo biblioteca estándar."""
import struct
import sys
import zlib
from pathlib import Path

W, H = 410, 502


def to_png(raw: bytes, path: Path) -> None:
    rows = bytearray()
    for y in range(H):
        rows.append(0)
        for x in range(W):
            (p,) = struct.unpack_from("<H", raw, (y * W + x) * 2)
            r, g, b = (p >> 11) & 0x1F, (p >> 5) & 0x3F, p & 0x1F
            rows += bytes(((r * 527 + 23) >> 6, (g * 259 + 33) >> 6, (b * 527 + 23) >> 6))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    for f in sorted(out.glob("*.rgb565")):
        to_png(f.read_bytes(), f.with_suffix(".png"))
        print("PNG:", f.with_suffix(".png"))


if __name__ == "__main__":
    main()
