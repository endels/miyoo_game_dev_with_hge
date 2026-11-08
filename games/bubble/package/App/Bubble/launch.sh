#!/bin/sh
# Bubble as a RetroArch/libretro core. RetroArch owns video (miyoomini MI_GFX),
# audio, input and exit; the core reuses the HGE+SDL2 game and software renderer.
progdir=$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd -P)

export HGE_DIR="$progdir/"
export LD_LIBRARY_PATH="$progdir/lib:/mnt/SDCARD/.tmp_update/lib/parasyte:/customer/lib:/config/lib:/lib:/usr/lib:$LD_LIBRARY_PATH"

for c in /sys/devices/system/cpu/cpu[0-9]*; do
    echo performance > "$c/cpufreq/scaling_governor" 2>/dev/null
done

cat > "$progdir/ra_append.cfg" <<'CFG'
audio_latency = "192"
audio_sync = "false"
audio_out_rate = "44100"
config_save_on_exit = "false"
CFG

cd /mnt/SDCARD/RetroArch/
HOME=/mnt/SDCARD/RetroArch/ ./retroarch -v --appendconfig "$progdir/ra_append.cfg" -L "$progdir/bubble_libretro.so" > "$progdir/ra.log" 2>&1
sync

# Restore the debug hotspot after the game exits. Onion can leave hostapd/WiFi
# down in this path, so re-run the same boot helper that brings the AP up.
sysdir=/mnt/SDCARD/.tmp_update
if [ -f "$sysdir/config/.bench_hotspot" ]; then
    if [ -x "$sysdir/script/bench_hotspot_boot.sh" ]; then
        ( sleep 2; "$sysdir/script/bench_hotspot_boot.sh" ) >/dev/null 2>&1 &
    else
        touch "$sysdir/config/.hotspotState"
        touch /tmp/network_changed
    fi
fi
