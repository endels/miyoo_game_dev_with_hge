#!/bin/sh
# B-mode boot hook: ensure WiFi is powered, then start the hotspot.
#
# Onion's update_networking.sh start_hotspot path does NOT call check_wifi, so
# at boot the WiFi chip may not be ready yet and hostapd can fail while the UI
# still shows the hotspot as enabled (the UI only reads .hotspotState).
# hotspot_create.sh does check_wifi (axp_test wifion, wlan0 up, wpa_supplicant)
# and then start_hotspot (wlan1 up, wlan0 down, hostapd, dnsmasq).
#
# Manual off/on in Tweaks works because by then the WiFi stack is ready; this
# hook waits for the interface and retries so it behaves the same at boot.

sysdir=/mnt/SDCARD/.tmp_update
log="$sysdir/bench_hotspot_boot.log"
exec >> "$log" 2>&1
echo "=== $(date) bench_hotspot_boot ==="

sleep 3
touch "$sysdir/config/.hotspotState"

# Wait up to 30s for the WiFi interface to appear (8188fu enumeration).
i=0
while [ "$i" -lt 30 ]; do
    if ifconfig wlan0 >/dev/null 2>&1 || ifconfig wlan1 >/dev/null 2>&1; then
        break
    fi
    sleep 1
    i=$((i + 1))
done
echo "wifi wait: ${i}s"

"$sysdir/script/network/hotspot_create.sh"

# Retry a few times if hostapd did not come up (WiFi may still be settling).
i=0
while [ "$i" -lt 3 ] && ! pgrep hostapd >/dev/null 2>&1; do
    sleep 3
    "$sysdir/script/network/hotspot_create.sh"
    i=$((i + 1))
done
echo "hostapd retries: $i"

echo "--- ifconfig wlan1 ---"
ifconfig wlan1 2>&1
echo "--- hostapd ---"
pgrep hostapd 2>&1
echo "--- dnsmasq ---"
pgrep dnsmasq 2>&1
echo "=== end ==="
