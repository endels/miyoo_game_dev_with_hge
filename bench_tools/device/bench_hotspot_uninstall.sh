#!/bin/sh
# Uninstall B-mode: restore runtime.sh from the backup, remove the boot script
# and the marker. If Onion has been updated since the backup was made, restoring
# the old runtime.sh would revert that update - check the backup date first.

sysdir=/mnt/SDCARD/.tmp_update
rt="$sysdir/runtime.sh"

if [ -f "$rt.bench.bak" ]; then
    cp -a "$rt.bench.bak" "$rt"
    echo "[bench] runtime.sh restored from $rt.bench.bak"
else
    echo "[bench] no runtime.sh.bench.bak; runtime.sh not restored"
fi

rm -f "$sysdir/config/.bench_hotspot"
rm -f "$sysdir/script/bench_hotspot_boot.sh"
sync
echo "[bench] uninstalled; reboot to apply"
