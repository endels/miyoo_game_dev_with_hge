#!/bin/sh
# PC-side helper: connect the host WiFi (Windows via WSL, or Linux) to the
# Miyoo AP, then verify that SSH is reachable. Run this before ssh-ing.
#
# The Miyoo must already be hosting the AP (B-mode auto hotspot).
#
# Usage:
#   ./miyoo_connect.sh            # connect (respects cooldown / max attempts)
#   ./miyoo_connect.sh --check    # only check MIYOO_IP:SSH_PORT
#   ./miyoo_connect.sh --force    # ignore cooldown (still respects attempts)
#
# Config: miyoo_net.conf (next to this script). See miyoo_net.conf.example.
#
# Exit codes: 0 ok, 1 failed, 2 cooldown, 3 busy, 4 no interface/tools.

set -eu

HERE=$(cd -- "$(dirname -- "$0")" && pwd)
if [ -f "$HERE/miyoo_net.conf" ]; then . "$HERE/miyoo_net.conf"; fi

WIFI_IFACE=${WIFI_IFACE:-}
AP_SSID=${AP_SSID:-MiyooMini+APOnionOS}
AP_PASS=${AP_PASS:-onionos+}
MIYOO_IP=${MIYOO_IP:-192.168.100.100}
SSH_PORT=${SSH_PORT:-22}
PROFILE_NAME=${PROFILE_NAME:-MiyooMiniAP}
MAX_ATTEMPTS=${MAX_ATTEMPTS:-3}
RETRY_DELAY=${RETRY_DELAY:-6}
COOLDOWN=${COOLDOWN:-60}
STATE_FILE=${STATE_FILE:-$HERE/.miyoo_connect.state}
LOCK_DIR=${LOCK_DIR:-$HERE/.miyoo_connect.lock}

FORCE=0
CHECK=0

usage() {
    awk 'NR >= 3 && /^set -eu/ { exit } NR >= 3 { sub(/^# ?/, ""); print }' "$0"
    exit "${1:-0}"
}

log() { echo "[miyoo] $*"; }

check_target() {
    timeout 3 bash -c "</dev/tcp/$MIYOO_IP/$SSH_PORT" >/dev/null 2>&1
}

use_windows() {
    command -v powershell.exe >/dev/null 2>&1 && command -v netsh.exe >/dev/null 2>&1
}

ps_win() {
    powershell.exe -NoProfile -NonInteractive -Command "$1" 2>/dev/null | tr -d '\r'
}

win_iface() {
    if [ -n "$WIFI_IFACE" ]; then
        echo "$WIFI_IFACE"
        return 0
    fi
    ps_win '(Get-NetAdapter -Physical | Where-Object { $_.PhysicalMediaType -eq "Native 802.11" -or $_.InterfaceDescription -match "Wi-Fi|Wireless|WLAN|802.11" } | Select-Object -First 1 -ExpandProperty Name)' | sed '/^$/d' | head -n1
}

win_current_ssid() {
    netsh.exe wlan show interfaces 2>/dev/null | tr -d '\r' | awk -F: '
        /^[[:space:]]*SSID[[:space:]]*:/ {
            s = $2
            sub(/^[[:space:]]+/, "", s)
            sub(/[[:space:]]+$/, "", s)
            print s
            exit
        }
    '
}

xml_escape() {
    sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g'
}

win_ensure_profile() {
    if netsh.exe wlan show profiles name="$PROFILE_NAME" >/dev/null 2>&1; then
        return 0
    fi
    if ! command -v wslpath >/dev/null 2>&1; then
        log "wslpath not found; cannot pass the profile path to Windows"
        return 1
    fi

    ssid_esc=$(printf '%s' "$AP_SSID" | xml_escape)
    pass_esc=$(printf '%s' "$AP_PASS" | xml_escape)
    tmp=$(mktemp /tmp/miyoo_wlan.XXXXXX.xml)

    cat > "$tmp" <<EOF
<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>$PROFILE_NAME</name>
  <SSIDConfig><SSID><name>$ssid_esc</name></SSID></SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>manual</connectionMode>
  <MSM><security><authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption><sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>$pass_esc</keyMaterial></sharedKey></security></MSM>
</WLANProfile>
EOF

    winpath=$(wslpath -w "$tmp")
    if ! netsh.exe wlan add profile filename="$winpath" user=current >/dev/null 2>&1; then
        log "failed to add WLAN profile '$PROFILE_NAME'"
        rm -f "$tmp"
        return 1
    fi
    rm -f "$tmp"
    log "WLAN profile added: $PROFILE_NAME"
}

win_connect() {
    win_ensure_profile || return 1
    netsh.exe wlan connect name="$PROFILE_NAME" ssid="$AP_SSID" interface="$WIFI_IFACE" >/dev/null 2>&1
}

linux_connect() {
    command -v nmcli >/dev/null 2>&1 || { log "nmcli not found"; return 1; }
    if [ -n "$WIFI_IFACE" ]; then
        nmcli device wifi connect "$AP_SSID" password "$AP_PASS" ifname "$WIFI_IFACE" >/dev/null 2>&1
    else
        nmcli device wifi connect "$AP_SSID" password "$AP_PASS" >/dev/null 2>&1
    fi
}

do_connect() {
    if use_windows; then
        if [ -z "$WIFI_IFACE" ]; then WIFI_IFACE=$(win_iface); fi
        if [ -z "$WIFI_IFACE" ]; then
            log "no Windows WLAN interface found (set WIFI_IFACE in miyoo_net.conf)"
            return 1
        fi
        log "Windows WLAN interface: $WIFI_IFACE"
        win_connect
    else
        log "non-Windows host: using nmcli"
        linux_connect
    fi
}

current_ssid() {
    if use_windows; then
        win_current_ssid
    else
        nmcli -t -f active,ssid dev wifi 2>/dev/null | awk -F: '$1=="yes" { print $2; exit }'
    fi
}

for a in "$@"; do
    case "$a" in
        --force) FORCE=1 ;;
        --check) CHECK=1 ;;
        -h|--help|help) usage 0 ;;
        *) echo "unknown argument: $a"; usage 1 ;;
    esac
done

if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    log "another connection attempt is already running ($LOCK_DIR)"
    exit 3
fi
trap 'rmdir "$LOCK_DIR" 2>/dev/null || true' EXIT INT TERM

if [ "$CHECK" -eq 1 ]; then
    if check_target; then
        log "reachable: $MIYOO_IP:$SSH_PORT"
        exit 0
    fi
    log "unreachable: $MIYOO_IP:$SSH_PORT"
    exit 1
fi

cur=$(current_ssid || true)
if [ "$cur" = "$AP_SSID" ] && check_target; then
    log "already on '$AP_SSID' and $MIYOO_IP:$SSH_PORT is reachable"
    exit 0
fi

now=$(date +%s)
last=0
if [ -f "$STATE_FILE" ]; then last=$(cat "$STATE_FILE" 2>/dev/null || echo 0); fi
if [ "$FORCE" -eq 0 ] && [ $((now - last)) -lt "$COOLDOWN" ]; then
    wait_s=$((COOLDOWN - (now - last)))
    log "cooldown: last attempt $((now - last))s ago; retry in ${wait_s}s (or use --force)"
    exit 2
fi
echo "$now" > "$STATE_FILE"

i=1
while [ "$i" -le "$MAX_ATTEMPTS" ]; do
    log "attempt $i/$MAX_ATTEMPTS: connect '$AP_SSID'"
    if do_connect; then
        sleep 2
        cur=$(current_ssid || true)
        if [ "$cur" = "$AP_SSID" ] && check_target; then
            log "connected: '$AP_SSID'; $MIYOO_IP:$SSH_PORT reachable"
            exit 0
        fi
        log "current SSID='$cur'; target not reachable yet"
    else
        log "connect command failed"
    fi
    if [ "$i" -lt "$MAX_ATTEMPTS" ]; then sleep "$RETRY_DELAY"; fi
    i=$((i + 1))
done

log "failed after $MAX_ATTEMPTS attempt(s)"
log "check: Miyoo hotspot (B mode), AP name/password, PC WiFi adapter, MIYOO_IP"
exit 1
