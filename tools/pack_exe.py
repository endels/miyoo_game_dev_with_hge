"""Append game.pak + audio.pak to the built exe so it ships as one file.

Layout:  [exe][game.pak][audio.pak][MAGIC 8B][u32 gameSize][u32 audioSize]

Idempotent: if the exe already ends with a valid footer, the old payload is
stripped first, so repeated builds do not grow the file.
"""

import struct
import sys

MAGIC = b"HGEEMB01"


def payload_size(data):
    if len(data) < 16 or data[-16:-8] != MAGIC:
        return 0
    g, a = struct.unpack("<II", data[-8:])
    total = 16 + g + a
    return total if total <= len(data) else 0


def main():
    exe, game_pak, audio_pak = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(exe, "rb").read()
    old = payload_size(data)
    if old:
        data = data[:-old]

    g = open(game_pak, "rb").read()
    a = open(audio_pak, "rb").read()
    out = data + g + a + MAGIC + struct.pack("<II", len(g), len(a))
    open(exe, "wb").write(out)
    print("packed %s: exe %.2f MB + game %.2f MB + audio %.2f MB = %.2f MB"
          % (exe, len(data) / 1048576.0, len(g) / 1048576.0,
             len(a) / 1048576.0, len(out) / 1048576.0))


if __name__ == "__main__":
    main()
