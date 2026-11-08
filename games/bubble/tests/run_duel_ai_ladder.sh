#!/bin/sh
# Duel AI difficulty ladder: level L (player side) vs default level 3 (CPU).
# Usage: run_duel_ai_ladder.sh <bubble-level1> [seeds]
#
# Runs 5 x seeds matches and prints the win rate of each level against the
# default bot. This is the A1/A0 measurement tool; it is deliberately not in
# the default ctest set because 1000 matches is slow.

set -eu

BIN="${1:-./build-native/bubble_level1}"
SEEDS="${2:-200}"

if [ ! -x "$BIN" ]; then
    echo "[ladder] no executable at $BIN" >&2
    exit 1
fi

echo "[ladder] $SEEDS seeds per level, player level vs default=3"
for level in 1 2 3 4 5; do
    wins=0
    draws=0
    n=0
    seed=1
    while [ "$seed" -le "$SEEDS" ]; do
        line=$("$BIN" --duel-sim --seed "$seed" --duel-ai-levels "$level,3" 2>/dev/null) || {
            echo "[ladder] level=$level seed=$seed failed" >&2
            exit 1
        }
        winner=$(printf '%s\n' "$line" | sed -n 's/.*winner=\([0-9]\).*/\1/p')
        [ "$winner" = "1" ] && wins=$((wins + 1))
        [ "$winner" = "3" ] && draws=$((draws + 1))
        n=$((n + 1))
        seed=$((seed + 1))
    done
    pct=$(awk -v w="$wins" -v n="$n" 'BEGIN { printf "%.1f", 100.0 * w / n }')
    printf '[ladder] level=%d wins=%d/%d pct=%s%% draws=%d\n' \
        "$level" "$wins" "$n" "$pct" "$draws"
done
