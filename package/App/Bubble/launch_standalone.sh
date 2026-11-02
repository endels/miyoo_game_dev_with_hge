#!/bin/sh
# Bubble Versus — direct /dev/fb0 output.
# SDL is used only for input/audio/timing; the game renders with SDL's software
# renderer into an ARGB8888 surface which we blit straight to /dev/fb0 (also
# ARGB8888) with double buffering. The mmiyoo SDL2 renderers are unusable for
# full-screen output (stock = stubs, PICO-8 = narrow bezel view).
progdir=$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd -P)

export LD_LIBRARY_PATH="$progdir/lib:/mnt/SDCARD/.tmp_update/lib/parasyte:/customer/lib:/config/lib:/lib:/usr/lib:$LD_LIBRARY_PATH"
export SDL_VIDEODRIVER=mmiyoo
export SDL_AUDIODRIVER=mmiyoo

echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null

# Take the display away from the compositor so our /dev/fb0 writes are visible.
killall -2 l 2>/dev/null
killall -9 disp_init 2>/dev/null

. /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh

cd "$progdir"
./bin/bubble_level1 "$@" > "$progdir/bubble.log" 2>&1

sync

# Ask Onion's runtime to restore the debug hotspot after the game exits.
sysdir=/mnt/SDCARD/.tmp_update
if [ -f "$sysdir/config/.bench_hotspot" ]; then
    touch "$sysdir/config/.hotspotState"
    touch /tmp/network_changed
fi

# Re-init mi_disp so runtime's relaunched MainUI can draw again.
/mnt/SDCARD/.tmp_update/bin/disp_init >/dev/null 2>&1 &
