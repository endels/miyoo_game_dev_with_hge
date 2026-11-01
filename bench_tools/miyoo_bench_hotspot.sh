#!/bin/sh
# Host-side helper for the Miyoo "B-mode" auto hotspot.
#
# The device must already be reachable over the hotspot (run miyoo_connect.sh
# first if needed). This script only deploys/enables/disables the device scripts.
#
# Usage:
#   ./miyoo_bench_hotspot.sh install    # deploy device scripts + hook + enable
#   ./miyoo_bench_hotspot.sh on         # enable marker (reboot to apply)
#   ./miyoo_bench_hotspot.sh off        # disable marker (reboot to apply)
#   ./miyoo_bench_hotspot.sh uninstall  # restore runtime.sh + remove boot script
#   ./miyoo_bench_hotspot.sh status
#
# Env (or miyoo_net.conf):
#   MIYOO_IP MIYOO_USER MIYOO_PASS

set -eu

HERE=$(cd -- "$(dirname -- "$0")" && pwd)
[ -f "$HERE/miyoo_net.conf" ] && . "$HERE/miyoo_net.conf"

MIYOO_IP=${MIYOO_IP:-192.168.100.100}
MIYOO_USER=${MIYOO_USER:-onion}
MIYOO_PASS=${MIYOO_PASS:-onion}
SYSDIR=/mnt/SDCARD/.tmp_update

SSH_OPTS="-o MACs=hmac-sha1 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10"

ssh_cmd() {
    sshpass -p "$MIYOO_PASS" ssh $SSH_OPTS "$MIYOO_USER@$MIYOO_IP" "$@"
}

usage() {
    awk 'NR >= 3 && /^set -eu/ { exit } NR >= 3 { sub(/^# ?/, ""); print }' "$0"
    exit "${1:-0}"
}

cmd=${1:-status}

case "$cmd" in
install)
    ssh_cmd "mkdir -p $SYSDIR/script"
    for f in bench_hotspot_boot.sh bench_hotspot_install.sh bench_hotspot_on.sh bench_hotspot_off.sh bench_hotspot_uninstall.sh; do
        cat "$HERE/device/$f" | ssh_cmd "cat > $SYSDIR/script/$f && chmod +x $SYSDIR/script/$f"
    done
    ssh_cmd "$SYSDIR/script/bench_hotspot_install.sh"
    echo "[bench] installed. Reboot the Miyoo to apply."
    ;;
on)
    ssh_cmd "$SYSDIR/script/bench_hotspot_on.sh"
    ;;
off)
    ssh_cmd "$SYSDIR/script/bench_hotspot_off.sh"
    ;;
uninstall)
    ssh_cmd "$SYSDIR/script/bench_hotspot_uninstall.sh"
    ;;
status)
    ssh_cmd '
        sysdir=/mnt/SDCARD/.tmp_update
        echo "wifi setting : $(/customer/app/jsonval wifi 2>/dev/null || echo ?)"
        echo "bench marker : $([ -f $sysdir/config/.bench_hotspot ] && echo yes || echo no)"
        echo "hotspot flag : $([ -f $sysdir/config/.hotspotState ] && echo yes || echo no)"
        echo "runtime hook : $(grep -c bench_hotspot_boot.sh $sysdir/runtime.sh 2>/dev/null || echo 0)"
        echo "hostapd      : $(pgrep hostapd 2>/dev/null || echo no)"
        echo "dnsmasq      : $(pgrep dnsmasq 2>/dev/null || echo no)"
        echo "wlan1 ip     : $(ifconfig wlan1 2>/dev/null | grep "inet addr" | cut -d: -f2 | cut -d" " -f1)"
    '
    ;;
-h|--help|help)
    usage 0
    ;;
*)
    usage 1
    ;;
esac
