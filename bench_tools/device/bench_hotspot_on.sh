#!/bin/sh
# Enable B-mode auto hotspot (marker consumed by the runtime.sh hook).

sysdir=/mnt/SDCARD/.tmp_update
touch "$sysdir/config/.bench_hotspot"
sync
echo "[bench] hotspot auto-start ENABLED"
echo "[bench] marker: $sysdir/config/.bench_hotspot"
echo "[bench] a reboot is needed for the boot hook to run"
