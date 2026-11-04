#!/usr/bin/env python3
"""Famicom-style BGM for the Bubble stage.

Four channels, 16 bars, 150 BPM, 4/4, 16 rows (16th notes) per bar = 256 rows.
The score below is the arrangement spec; the synth is a small NES emulation:
2 pulse channels (12.5% / 25% duty), a stepped triangle bass and a noise
channel for hi-hats. No samples.

    python3 tools/make_bgm.py         # tools/bgm/arcade_bubble.wav
    python3 tools/replace_bgm.py tools/bgm/arcade_bubble.wav
    cmake --build build-native -j
"""

import math
import os
import re
import random
import struct
import sys
import wave

SR = 44100
BPM = 150.0
ROWS_PER_BAR = 16
BARS = 16
ROW_S = 60.0 / BPM / 4.0          # one 16th note
LOOP_ROWS = BARS * ROWS_PER_BAR

# --------------------------------------------------------------------- score
# One line per bar. "-" extends the previous note by one row (16th); "---" is
# a rest when it does not follow a note. Notes in a bar are laid on each
# channel's rhythmic grid (see GRID below).
MAIN = """
E5 - G5 - C6 - G5 - E5 - C5 - E5 - G5
A5 - C6 - E6 - C6 - A5 - G5 - E5 - C5
D5 - E5 - G5 - E5 - D5 - C5 - D5 - E5
G5 - E5 - C5 - G4 - C5 --- --- ---
E5 - G5 - C6 - G5 - E5 - C5 - E5 - G5
A5 - C6 - E6 - C6 - A5 - G5 - E5 - C5
D5 - E5 - G5 - E5 - D5 - C5 - D5 - E5
G5 - E5 - C5 - G4 - C5 --- --- ---
C5 - E5 - G5 - C6 - B5 - A5 - G5 - E5
A5 - C6 - E6 - C6 - G5 - E5 - C5 - G4
F5 - A5 - C6 - A5 - G5 - F5 - E5 - D5
E5 - G5 - C6 - G5 - E5 - C5 --- ---
E5 - G5 - C6 - G5 - E5 - C5 - E5 - G5
A5 - C6 - E6 - C6 - A5 - G5 - E5 - C5
D5 - E5 - G5 - E5 - D5 - C5 - D5 - E5
G5 - E5 - C5 - G4 - C5 --- --- ---
"""

SUB = """
--- --- --- --- --- --- --- ---
C5 --- E5 --- G5 --- E5 ---
--- --- --- --- --- --- --- ---
G4 --- E4 --- C4 --- --- ---
--- --- --- --- --- --- --- ---
C5 --- E5 --- G5 --- E5 ---
--- --- --- --- --- --- --- ---
G4 --- E4 --- C4 --- --- ---
E5 --- G5 --- A5 --- G5 ---
C5 --- E5 --- G5 --- E5 ---
A4 --- C5 --- D5 --- E5 ---
G4 --- E4 --- C4 --- --- ---
--- --- --- --- --- --- --- ---
C5 --- E5 --- G5 --- E5 ---
--- --- --- --- --- --- --- ---
G4 --- E4 --- C4 --- --- ---
"""

BASS = """
C3 --- G3 --- C3 --- G3 ---
A2 --- E3 --- A2 --- E3 ---
D3 --- A3 --- D3 --- A3 ---
G2 --- D3 --- C3 --- --- ---
C3 --- G3 --- C3 --- G3 ---
A2 --- E3 --- A2 --- E3 ---
D3 --- A3 --- D3 --- A3 ---
G2 --- D3 --- C3 --- --- ---
C3 - E3 - G3 - C4 - B3 - G3 - E3 - C3
A2 - C3 - E3 - C3 - G2 - B2 - C3 - G2
F2 - A2 - C3 - A2 - G2 - F2 - E2 - D2
C2 - E2 - G2 - E2 - C2 --- --- ---
C3 --- G3 --- C3 --- G3 ---
A2 --- E3 --- A2 --- E3 ---
D3 --- A3 --- D3 --- A3 ---
G2 --- D3 --- C3 --- --- ---
"""

NOISE = """
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH --- --- ---
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH --- --- ---
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH --- --- ---
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH - HH - HH - HH
HH - HH - HH - HH - HH --- --- ---
"""

NOTE_RE = re.compile(r"^[A-G][#b]?(-?\d)$")
PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi_of(tok):
    tok = tok.replace("(", " ").split()[0]
    m = NOTE_RE.match(tok)
    if not m:
        return None
    name = tok[0]
    acc = 0
    rest = tok[1:]
    if rest[0] == "#":
        acc = 1
        rest = rest[1:]
    elif rest[0] == "b":
        acc = -1
        rest = rest[1:]
    octave = int(rest)
    return 12 * (octave + 1) + PC[name] + acc


def bars_of(block):
    return [ln for ln in block.strip("\n").split("\n") if ln.strip()]


def note_tokens(line):
    out = []
    for tok in line.split():
        if tok.startswith("-"):
            continue
        m = midi_of(tok)
        if m is not None:
            out.append(m)
    return out


def hh_slots(line):
    return sum(1 for tok in line.split() if tok.upper() == "HH")


# ---------------------------------------------------------------------- synth
def add_pulse(buf, start, dur, midi, duty, vol, vib=0.0, decay=0.62):
    n0 = int(round(start * SR))
    n = int(round(dur * SR))
    if n <= 0:
        return
    f = 440.0 * (2.0 ** ((midi - 69) / 12.0))
    phase = 0.0
    atk = max(1, int(0.002 * SR))
    dcy = max(1, int(0.10 * SR))
    rel = max(1, int(0.015 * SR))
    for i in range(n):
        t = i / SR
        ff = f * (1.0 + vib * math.sin(2.0 * math.pi * 5.5 * t)) if vib else f
        phase += ff / SR
        p = phase - math.floor(phase)
        v = 1.0 if p < duty else -1.0
        if i < atk:
            e = i / atk
        elif i < atk + dcy:
            e = 1.0 - (1.0 - decay) * (i - atk) / dcy
        else:
            e = decay
        if i > n - rel:
            e *= (n - i) / rel
        j = n0 + i
        if 0 <= j < len(buf):
            buf[j] += v * e * vol


def add_tri(buf, start, dur, midi, vol):
    n0 = int(round(start * SR))
    n = int(round(dur * SR))
    if n <= 0:
        return
    f = 440.0 * (2.0 ** ((midi - 69) / 12.0))
    phase = 0.0
    fade = max(1, int(0.004 * SR))
    for i in range(n):
        phase += f / SR
        p = phase - math.floor(phase)
        v = 4.0 * abs(p - 0.5) - 1.0
        v = round(v * 8.0) / 8.0          # NES triangle has 16 steps
        e = 1.0
        if i < fade:                      # avoid key clicks only
            e = i / fade
        elif i > n - fade:
            e = (n - i) / fade
        j = n0 + i
        if 0 <= j < len(buf):
            buf[j] += v * e * vol


def add_hat(buf, start, vol, rng):
    n = int(0.05 * SR)
    n0 = int(round(start * SR))
    prev = 0.0
    for i in range(n):
        t = i / SR
        w = rng.uniform(-1.0, 1.0)
        hp = w - prev
        prev = w
        j = n0 + i
        if 0 <= j < len(buf):
            buf[j] += hp * math.exp(-t * 95.0) * vol


def build():
    total = int(round(LOOP_ROWS * ROW_S * SR))
    buf = [0.0] * total
    rng = random.Random(2025)
    m, s, b, n = (bars_of(MAIN), bars_of(SUB), bars_of(BASS), bars_of(NOISE))
    assert len(m) == len(s) == len(b) == len(n) == BARS

    for bar in range(BARS):
        row0 = bar * ROWS_PER_BAR
        t0 = row0 * ROW_S

        # Pulse 1: melody, eighth-note grid (2 rows per note).
        mel = note_tokens(m[bar])
        for i, midi in enumerate(mel[:8]):
            dur = 2 * ROW_S
            if bar == BARS - 1 and i == len(mel) - 1:
                dur = 1 * ROW_S            # short final note, then silence
            add_pulse(buf, t0 + i * 2 * ROW_S, dur * 0.9, midi, 0.25, 0.22,
                      vib=0.003 if dur >= 4 * ROW_S else 0.0)

        # Pulse 2: counter line, quarter-note grid (4 rows per note).
        sub = note_tokens(s[bar])
        for i, midi in enumerate(sub[:4]):
            add_pulse(buf, t0 + i * 4 * ROW_S, 3.6 * ROW_S, midi, 0.125, 0.11,
                      decay=0.55)

        # Triangle: quarter grid for bars 1-8/13-16, eighth grid for 9-12 bars
        # (the walking bars), matching the written note count.
        bass = note_tokens(b[bar])
        step = 4 * ROW_S if len(bass) <= 4 else 2 * ROW_S
        for i, midi in enumerate(bass):
            dur = step * 0.92
            if bar == BARS - 1 and i == len(bass) - 1:
                dur = t0 + ROWS_PER_BAR * ROW_S - (t0 + i * step) - 0.01
            add_tri(buf, t0 + i * step, dur, midi, 0.30)

        # Noise: hi-hats on an eighth-note grid.
        hh = hh_slots(n[bar])
        for i in range(min(hh, 8)):
            add_hat(buf, t0 + i * 2 * ROW_S, 0.13 if i % 2 == 0 else 0.09, rng)

    # Master: normalize, tiny soft clip, no reverb tail (keeps the loop tight).
    peak = max(1e-9, max(abs(v) for v in buf))
    g = 0.92 / peak
    out = bytearray()
    for v in buf:
        x = v * g
        if x > 0.9:
            x = 0.9 + (x - 0.9) * 0.5
        elif x < -0.9:
            x = -0.9 + (x + 0.9) * 0.5
        out += struct.pack("<h", int(max(-1.0, min(1.0, x)) * 32767.0))
    return bytes(out), total


def main():
    dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "bgm", "arcade_bubble.wav")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    pcm, total = build()
    with wave.open(dst, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm)
    print("wrote %s  %.2fs  %.2f MB  rows=%d  row=%.3fs" % (
        dst, total / float(SR), len(pcm) / 1048576.0, LOOP_ROWS, ROW_S))


if __name__ == "__main__":
    main()
