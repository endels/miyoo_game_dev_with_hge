#!/bin/sh
# Attach gdb to a running bubble_level1 (or any PID) and dump a one-shot
# diagnostic snapshot. Built for the "audio goes silent after a while" hunt.
#
#   tools/attach_dump.sh <pid>
#   tools/attach_dump.sh              # picks the newest bubble_level1 process
#
# Requires a binary built with debug info (build-dbg, RelWithDebInfo) and the
# target to have been started with HGE_ALLOW_GDB=1 so that yama
# ptrace_scope=1 does not block the attach.

set -u

PID="${1:-}"
if [ -z "$PID" ]; then
    PID=$(pgrep -n -x bubble_level1 || true)
fi
if [ -z "$PID" ]; then
    echo "usage: $0 <pid>   (no bubble_level1 process found)" >&2
    exit 2
fi

GDBBIN="${GDB:-gdb}"
if ! command -v "$GDBBIN" >/dev/null 2>&1; then
    echo "gdb not found; set GDB=/path/to/gdb" >&2
    exit 2
fi

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

echo "=== kernel thread view (pid $PID) ==="
for t in /proc/$PID/task/*; do
    printf '%6s %-16s %s\n' "$(basename "$t")" \
        "$(cat "$t/comm" 2>/dev/null)" "$(cat "$t/wchan" 2>/dev/null)"
done 2>/dev/null
echo
EXE=$(readlink -f "/proc/$PID/exe" 2>/dev/null || true)

# One-shot snapshot: thread states, backtraces, audio mixer diagnostics.
set -- -batch -p "$PID" \
    -ex 'set pagination off' \
    -ex 'set print thread-events off' \
    -ex 'echo \n===== threads =====\n' \
    -ex 'info threads' \
    -ex 'echo \n===== backtraces =====\n' \
    -ex 'thread apply all bt 12'

if [ -n "$EXE" ] && readelf -S "$EXE" 2>/dev/null | grep -q '\.debug_info'; then
    set -- "$@" -ex "source $HERE/gdb/audio_state.gdb"
else
    echo "note: $EXE has no debug info; skipping mixer symbol dump (use build-dbg)" >&2
fi

"$GDBBIN" -q "$@" 2>&1
