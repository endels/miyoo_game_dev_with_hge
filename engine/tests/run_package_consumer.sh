#!/bin/sh
# Install the engine into a throwaway prefix, then build and run an out-of-tree
# project that only does find_package(hge).
set -e
BUILD="$1"
PREFIX="$2"
SRC="$3"

rm -rf "$PREFIX"
cmake --install "$BUILD" --prefix "$PREFIX" >/dev/null

WORK="$BUILD/package-consumer"
LOG="$BUILD/package-consumer.log"
rm -rf "$WORK"
mkdir -p "$WORK"

if ! cmake -S "$SRC" -B "$WORK" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$PREFIX" >"$LOG" 2>&1; then
    echo "--- configure failed ---"; cat "$LOG"; exit 1
fi
if ! cmake --build "$WORK" >>"$LOG" 2>&1; then
    echo "--- build failed ---"; cat "$LOG"; exit 1
fi
if ! "$WORK/consumer"; then
    echo "--- run failed ---"; exit 1
fi
echo "[package-consumer] OK"
