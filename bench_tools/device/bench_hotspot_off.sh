#!/bin/sh
# Disable B-mode auto hotspot. The current hotspot may remain until reboot or
# until the next network state check.

sysdir=/mnt/SDCARD/.tmp_update
rm -f "$sysdir/config/.bench_hotspot"
sync
echo "[bench] hotspot auto-start DISABLED"
echo "[bench] a reboot is needed to fully apply"
