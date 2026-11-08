"""Bake SFX + BGM (from the FSM sample project) into assets/audio.pak.

Format
    char magic[4]  "HGEA"
    u32  version   1
    u32  count
    count x { char name[24]; u32 frames; u32 channels; u32 rate }
    s16le PCM blobs, same order

Decoding happens here via ffmpeg, so the game needs no OGG decoder at runtime
(important for embedded builds).
"""

import os
import struct
import subprocess
import sys

DLG = os.path.join(os.environ.get("HGE_DLC_ROOT",
    r"F:\SteamLibrary\steamapps\common\RPG Maker MV\dlc"),
    "FSM_Woods_and_Cave_Tiles", "FSM_sample02_EN", "audio")
OUT = os.environ.get("HGE_AUDIO_PAK", os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "assets", "audio.pak"))
FFMPEG = os.environ.get("HGE_FFMPEG", "ffmpeg")

# name, source, rate, channels
ENTRIES = [
    ("shoot",   "se/Move1.ogg",    44100, 1),
    ("stick",   "se/Switch2.ogg",  44100, 1),
    ("pop",     "se/Chime1.ogg",   44100, 1),
    ("pop2",    "se/Chime2.ogg",   44100, 1),
    ("drop",    "se/Earth1.ogg",   44100, 1),
    ("descend", "se/Earth5.ogg",   44100, 1),
    ("lose",    "se/Buzzer1.ogg",  44100, 1),
    ("win",     "me/Fanfare2.ogg", 44100, 1),
    ("bgm",     "bgm/nodokanamura.ogg", 22050, 1),
]


def decode(src, rate, ch):
    path = src if os.path.isabs(src) else os.path.join(DLG, src)
    cmd = [FFMPEG, "-v", "error", "-y", "-i", path,
           "-ac", str(ch), "-ar", str(rate), "-f", "s16le", "-"]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        raise RuntimeError("ffmpeg failed for %s: %s" % (src, p.stderr.decode("utf8", "replace")[:300]))
    return p.stdout


def main():
    blobs = []
    for name, src, rate, ch in ENTRIES:
        data = decode(src, rate, ch)
        frames = len(data) // (2 * ch)
        blobs.append((name, frames, ch, rate, data))
        print("  %-8s %-18s %5.1fs  %6.1f KB" %
              (name, src, frames / float(rate), len(data) / 1024.0))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as fp:
        fp.write(b"HGEA")
        fp.write(struct.pack("<II", 1, len(blobs)))
        for name, frames, ch, rate, data in blobs:
            fp.write(name.encode("ascii")[:23].ljust(24, b"\0"))
            fp.write(struct.pack("<III", frames, ch, rate))
        total = 0
        for name, frames, ch, rate, data in blobs:
            fp.write(data)
            total += len(data)
    print("wrote %s  %.2f MB" % (OUT, os.path.getsize(OUT) / 1048576.0))


if __name__ == "__main__":
    main()
