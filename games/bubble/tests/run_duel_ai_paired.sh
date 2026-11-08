#!/bin/sh
# Paired AI comparison: level A vs default and level B vs default on the same
# seeds, then McNemar on the discordant outcomes.
# Usage: run_duel_ai_paired.sh <bubble-level1> <levelA> <levelB> [seeds]
#
# Output: wins/percent for each level, difference pp, McNemar chi2 and p.
# The script is deliberately not in the default ctest set (2 x n matches).

set -eu

BIN="${1:-./build-native/bubble_level1}"
A="${2:-4}"
B="${3:-5}"
SEEDS="${4:-1000}"

if [ ! -x "$BIN" ]; then
    echo "[paired] no executable at $BIN" >&2
    exit 1
fi

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

seed=1
while [ "$seed" -le "$SEEDS" ]; do
    la=$("$BIN" --duel-sim --seed "$seed" --duel-ai-levels "$A,3" 2>/dev/null) || {
        echo "[paired] level=$A seed=$seed failed" >&2
        exit 1
    }
    lb=$("$BIN" --duel-sim --seed "$seed" --duel-ai-levels "$B,3" 2>/dev/null) || {
        echo "[paired] level=$B seed=$seed failed" >&2
        exit 1
    }
    wa=$(printf '%s\n' "$la" | sed -n 's/.*winner=\([0-9]\).*/\1/p')
    wb=$(printf '%s\n' "$lb" | sed -n 's/.*winner=\([0-9]\).*/\1/p')
    [ "$wa" = "1" ] && wa=1 || wa=0
    [ "$wb" = "1" ] && wb=1 || wb=0
    printf '%s %s\n' "$wa" "$wb" >> "$tmp"
    seed=$((seed + 1))
done

python3 - "$tmp" "$A" "$B" "$SEEDS" <<'PY'
import math
import sys

path, A, B, n = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
wins_a = wins_b = 0
a_only = b_only = both = neither = 0
for line in open(path):
    wa, wb = (int(x) for x in line.split())
    wins_a += wa
    wins_b += wb
    if wa and wb:
        both += 1
    elif wa:
        a_only += 1
    elif wb:
        b_only += 1
    else:
        neither += 1

diff = 100.0 * (wins_b - wins_a) / n
disc = a_only + b_only
if disc:
    chi2 = (abs(a_only - b_only) - 1.0) ** 2 / disc
    p = math.erfc(math.sqrt(max(0.0, chi2) / 2.0))
else:
    chi2 = 0.0
    p = 1.0

print(f"[paired] n={n} level{A}={wins_a} ({100.0*wins_a/n:.1f}%) "
      f"level{B}={wins_b} ({100.0*wins_b/n:.1f}%) diff={diff:+.1f}pp")
print(f"[paired] discordant A_only={a_only} B_only={b_only} "
      f"both={both} neither={neither} chi2={chi2:.3f} p={p:.4f}")
PY
