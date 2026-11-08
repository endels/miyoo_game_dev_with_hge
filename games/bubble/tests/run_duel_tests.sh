#!/bin/sh
# Golden test for the deterministic headless duel.
#
# `--duel-sim --seed N` drives both sides with the same greedy bot on a fixed
# 1/60 step and prints one summary line per match. Comparing those lines with a
# recorded golden freezes default play: any change to the default AI level
# (shot interval, aim jitter, column weights, side-wall chance) or to the battle
# rules that alters an outcome fails ctest instead of shipping silently.
#
# Usage: run_duel_tests.sh <bubble-binary> <golden-file> [first-seed] [last-seed]
#        HGE_UPDATE_GOLDEN=1 run_duel_tests.sh ...     # rewrite the golden
#
# The golden is checked in. Regenerate it only for a deliberate behaviour
# change, and commit the golden together with the code that changed it.

set -eu

BIN="$1"
GOLDEN="$2"
FIRST="${3:-1}"
LAST="${4:-32}"

if [ ! -x "$BIN" ]; then
    echo "[test] no executable at $BIN" >&2
    exit 1
fi

OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT

seed="$FIRST"
while [ "$seed" -le "$LAST" ]; do
    rc=0
    line=$("$BIN" --duel-sim --seed "$seed" 2>/dev/null) || rc=$?
    # 0 = match finished, 2 = still running when the frame cap hit; both are
    # deterministic results. Anything else is a crash.
    if [ "$rc" -ne 0 ] && [ "$rc" -ne 2 ]; then
        echo "[test] duel seed=$seed exited $rc" >&2
        exit 1
    fi
    if [ -z "$line" ]; then
        echo "[test] duel seed=$seed printed nothing on stdout" >&2
        exit 1
    fi
    printf '%s\n' "$line" >> "$OUT"
    seed=$((seed + 1))
done

if [ "${HGE_UPDATE_GOLDEN:-0}" = "1" ]; then
    cp "$OUT" "$GOLDEN"
    echo "[test] updated $GOLDEN ($(wc -l < "$GOLDEN") matches, seeds $FIRST..$LAST)"
    exit 0
fi

if ! diff -u "$GOLDEN" "$OUT"; then
    echo "[test] duel golden mismatch (-golden +now)" >&2
    echo "  golden: $GOLDEN" >&2
    echo "  seeds:  $FIRST..$LAST at the default AI level" >&2
    echo "  if the change was intentional, regenerate and commit the golden:" >&2
    echo "    HGE_UPDATE_GOLDEN=1 sh $0 $BIN $GOLDEN $FIRST $LAST" >&2
    exit 1
fi

echo "[test] duel golden ok ($(wc -l < "$GOLDEN") matches, seeds $FIRST..$LAST)"
