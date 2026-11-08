#!/usr/bin/env python3
"""Replace one clip (default: bgm) in assets/audio.pak with a WAV file.

Pure Python, no ffmpeg needed: the mixer resamples per clip, so the WAV's own
rate/channel count is fine. Accepts 8/16/24/32-bit PCM and 32-bit float WAVs.

    python3 tools/replace_bgm.py tools/bgm/arcade_bubble.wav
    python3 tools/replace_bgm.py some_track.wav --name bgm --pak assets/audio.pak

The pak is embedded into the executable at link time, so rebuild afterwards:
    cmake --build build-native -j
"""

import argparse
import os
import struct
import sys


def read_wav(path):
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit("%s: not a RIFF/WAVE file" % path)

    fmt = None
    pcm = None
    pos = 12
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt = body
        elif tag == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    if fmt is None or pcm is None:
        raise SystemExit("%s: missing fmt/data chunk" % path)

    tag, channels, rate = struct.unpack("<HHI", fmt[:8])
    bits = struct.unpack("<H", fmt[14:16])[0]
    if tag not in (1, 3):
        raise SystemExit("unsupported WAV format tag %d" % tag)

    if tag == 3 and bits == 32:              # IEEE float -> s16
        n = len(pcm) // 4
        vals = struct.unpack("<%df" % n, pcm[:n * 4])
        out = bytearray()
        for v in vals:
            v = max(-1.0, min(1.0, v))
            out += struct.pack("<h", int(v * 32767.0))
        pcm = bytes(out)
    elif bits == 8:                          # unsigned 8-bit -> s16
        n = len(pcm)
        out = bytearray()
        for b in pcm[:n]:
            out += struct.pack("<h", (b - 128) << 8)
        pcm = bytes(out)
    elif bits == 24:
        n = len(pcm) // 3
        out = bytearray()
        for i in range(n):
            b = pcm[i * 3:i * 3 + 3]
            v = int.from_bytes(b, "little", signed=True) >> 8
            out += struct.pack("<h", v)
        pcm = bytes(out)
    elif bits == 32:
        n = len(pcm) // 4
        vals = struct.unpack("<%di" % n, pcm[:n * 4])
        out = bytearray()
        for v in vals:
            out += struct.pack("<h", v >> 16)
        pcm = bytes(out)
    elif bits != 16:
        raise SystemExit("unsupported bit depth %d" % bits)

    frames = len(pcm) // (2 * channels)
    return pcm, frames, channels, rate


def read_pak(path):
    data = open(path, "rb").read()
    if data[:4] != b"HGEA":
        raise SystemExit("%s: not an audio.pak" % path)
    version, count = struct.unpack("<II", data[4:12])
    off = 12
    entries = []
    for _ in range(count):
        name = data[off:off + 24].split(b"\0")[0].decode("ascii")
        frames, ch, rate = struct.unpack("<III", data[off + 24:off + 36])
        entries.append([name, frames, ch, rate, None])
        off += 36
    for e in entries:
        n = e[1] * e[2] * 2
        e[4] = data[off:off + n]
        off += n
    return version, entries


def write_pak(path, version, entries):
    out = bytearray()
    out += b"HGEA"
    out += struct.pack("<II", version, len(entries))
    for name, frames, ch, rate, _ in entries:
        out += name.encode("ascii")[:23].ljust(24, b"\0")
        out += struct.pack("<III", frames, ch, rate)
    for _, _, _, _, blob in entries:
        out += blob
    with open(path, "wb") as f:
        f.write(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--name", default="bgm")
    ap.add_argument("--pak", default=os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "audio.pak"))
    args = ap.parse_args()

    pcm, frames, ch, rate = read_wav(args.wav)
    version, entries = read_pak(args.pak)

    hit = False
    for e in entries:
        if e[0] == args.name:
            e[1], e[2], e[3], e[4] = frames, ch, rate, pcm
            hit = True
    if not hit:
        raise SystemExit("clip '%s' not found in %s" % (args.name, args.pak))

    write_pak(args.pak, version, entries)
    print("baked %-8s <- %s  %d frames  %dch  %d Hz  %.1fs  pak=%.2f MB" % (
        args.name, args.wav, frames, ch, rate, frames / float(rate),
        os.path.getsize(args.pak) / 1048576.0))


if __name__ == "__main__":
    main()
