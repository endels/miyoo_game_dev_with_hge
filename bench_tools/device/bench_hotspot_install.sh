#!/bin/sh
# Install B-mode: back up runtime.sh, add one hook line after check_networking
# inside start_networking(), then enable the marker.
#
# Idempotent: skips the edit if the hook is already present. The boot script
# must already be deployed next to this file.

sysdir=/mnt/SDCARD/.tmp_update
rt="$sysdir/runtime.sh"
boot="$sysdir/script/bench_hotspot_boot.sh"

if [ ! -f "$rt" ]; then
    echo "[bench] missing $rt"
    exit 1
fi
if [ ! -f "$boot" ]; then
    echo "[bench] missing $boot"
    exit 1
fi

if grep -q "bench_hotspot_boot.sh" "$rt"; then
    echo "[bench] runtime hook already present; skip"
else
    cp -a "$rt" "$rt.bench.bak"
    awk '
        /^start_networking\(\)[[:space:]]*\{/ { inb = 1 }
        inb && /^[[:space:]]*check_networking[[:space:]]*$/ {
            print
            print "    [ -f $sysdir/config/.bench_hotspot ] && $sysdir/script/bench_hotspot_boot.sh &"
            inb = 0
            next
        }
        { print }
    ' "$rt" > "$rt.new" && mv "$rt.new" "$rt"
    sync
    echo "[bench] runtime hook added; backup: $rt.bench.bak"
fi

chmod +x "$boot"
chmod +x "$sysdir/script/bench_hotspot_on.sh" 2>/dev/null || true
chmod +x "$sysdir/script/bench_hotspot_off.sh" 2>/dev/null || true

wifi=$(/customer/app/jsonval wifi 2>/dev/null || echo "?")
[ "$wifi" = "1" ] || echo "[bench] WARN: WiFi setting is '$wifi'; enable WiFi in Onion or the hotspot will be disabled"

"$sysdir/script/bench_hotspot_on.sh"
