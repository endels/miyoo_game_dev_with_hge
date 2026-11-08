#!/bin/sh
# Golden test for the deterministic headless battle simulation.
# Usage: run_host_tests.sh <bubble_level1> <golden-file>

set -eu

BIN="$1"
GOLDEN="$2"

OUT="$("$BIN" --sim-only 5)"
printf '%s\n' "$OUT"

EXPECT="$(cat "$GOLDEN")"
if [ "$OUT" != "$EXPECT" ]; then
    echo "[test] sim golden mismatch" >&2
    echo "  expected: $EXPECT" >&2
    echo "  got:      $OUT" >&2
    exit 1
fi
