"""Build the NewsX taskbar icon from the QML title-bar mark."""

from pathlib import Path
import struct
import zlib


ROOT = Path(__file__).resolve().parent
BACKGROUND = (57, 127, 189)
LETTER = (249, 251, 252)
SIZES = (16, 24, 32, 48, 64, 128, 256)


def inside_polygon(x, y, points):
    inside = False
    previous = points[-1]
    for current in points:
        x1, y1 = previous
        x2, y2 = current
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            inside = not inside
        previous = current
    return inside


def inside_mark(x, y):
    if 0.30 <= x <= 0.40 and 0.26 <= y <= 0.74:
        return True
    if 0.60 <= x <= 0.70 and 0.26 <= y <= 0.74:
        return True
    return inside_polygon(x, y, ((0.37, 0.26), (0.47, 0.26),
                                  (0.63, 0.74), (0.53, 0.74)))


def inside_tile(x, y):
    left, top, right, bottom, radius = 0.06, 0.06, 0.94, 0.94, 0.24
    if not (left <= x <= right and top <= y <= bottom):
        return False
    cx = min(max(x, left + radius), right - radius)
    cy = min(max(y, top + radius), bottom - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2


def png_chunk(kind, payload):
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))


def make_png(size):
    rows = bytearray()
    samples = 4
    for y in range(size):
        rows.append(0)
        for x in range(size):
            tile_count = 0
            mark_count = 0
            for sy in range(samples):
                ny = (y + (sy + 0.5) / samples) / size
                for sx in range(samples):
                    nx = (x + (sx + 0.5) / samples) / size
                    if inside_tile(nx, ny):
                        tile_count += 1
                        mark_count += inside_mark(nx, ny)
            if tile_count:
                rgb = tuple(round((LETTER[i] * mark_count + BACKGROUND[i]
                                   * (tile_count - mark_count)) / tile_count)
                            for i in range(3))
                rows.extend((*rgb, round(255 * tile_count / (samples * samples))))
            else:
                rows.extend((0, 0, 0, 0))
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
            + png_chunk(b"IDAT", zlib.compress(rows, 9))
            + png_chunk(b"IEND", b""))


images = [(size, make_png(size)) for size in SIZES]
(ROOT / "newsx.png").write_bytes(images[-1][1])
offset = 6 + len(images) * 16
entries = []
for size, data in images:
    dimension = size if size < 256 else 0
    entries.append(struct.pack("<BBBBHHII", dimension, dimension, 0, 0,
                               1, 32, len(data), offset))
    offset += len(data)
(ROOT / "newsx.ico").write_bytes(struct.pack("<HHH", 0, 1, len(images))
                                 + b"".join(entries)
                                 + b"".join(data for _, data in images))
