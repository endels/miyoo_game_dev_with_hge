#!/bin/sh
# PC-side deploy helper: push one App directory to the Miyoo over SSH.
#
#   ./miyoo_deploy_app.sh <local-app-dir>            # remote name = basename
#   ./miyoo_deploy_app.sh <local-app-dir> QuadBench  # explicit remote name
#   ./miyoo_deploy_app.sh --check                    # only probe SSH, no deploy
#
# Why tar-over-ssh and not rsync: the device only ships busybox, no rsync.
#
# Auth: tries the key from ~/.ssh/config (Host miyoo) first; falls back to
# sshpass + MIYOO_PASS from miyoo_net.conf when no key is installed yet.
#
# Assumes the hotspot is up and miyoo_connect.sh has already been run:
#   ./miyoo_connect.sh --force

set -eu

HERE=$(cd -- "$(dirname -- "$0")" && pwd)
if [ -f "$HERE/miyoo_net.conf" ]; then . "$HERE/miyoo_net.conf"; fi

MIYOO_IP=${MIYOO_IP:-192.168.100.100}
MIYOO_USER=${MIYOO_USER:-onion}
MIYOO_PASS=${MIYOO_PASS:-}
SSH_HOST=${SSH_HOST:-miyoo}          # ~/.ssh/config alias; falls back to user@ip
SDCARD=${SDCARD:-/mnt/SDCARD}

log() { echo "[deploy] $*"; }
die() { echo "[deploy] $*" >&2; exit 1; }

ssh_key_ok() {
    ssh -o BatchMode=yes -o ConnectTimeout=6 "$SSH_HOST" true >/dev/null 2>&1
}

SSH_MODE=""
resolve_ssh() {
    if ssh_key_ok; then
        SSH_MODE=key
        log "ssh: key auth via '$SSH_HOST'"
        return 0
    fi
    [ -n "$MIYOO_PASS" ] || die "key auth failed and MIYOO_PASS is empty in miyoo_net.conf"
    command -v sshpass >/dev/null 2>&1 || die "key auth failed and sshpass is not installed"
    SSH_MODE=pass
    log "ssh: password auth via sshpass (key not installed on the device yet)"
}

rsh() {
    if [ "$SSH_MODE" = "key" ]; then
        ssh -o BatchMode=yes -o ConnectTimeout=8 "$SSH_HOST" "$@"
    else
        sshpass -p "$MIYOO_PASS" ssh -o StrictHostKeyChecking=accept-new \
            -o ConnectTimeout=8 "$SSH_HOST" "$@"
    fi
}

case "${1:-}" in
    --check)
        resolve_ssh
        log "reachable: $(rsh 'uname -a' | cut -c1-60)"
        log "device: $(rsh 'cat /proc/device-tree/model 2>/dev/null || echo unknown')"
        rsh "df -h $SDCARD | tail -1"
        exit 0
        ;;
    ""|-h|--help)
        awk 'NR >= 2 && /^set -eu/ { exit } NR >= 2 { sub(/^# ?/, ""); print }' "$0"
        exit 0
        ;;
esac

LOCAL_APP=${1:?local app dir required}
[ -d "$LOCAL_APP" ] || die "not a directory: $LOCAL_APP"
REMOTE_NAME=${2:-$(basename "$LOCAL_APP")}
REMOTE_DIR="$SDCARD/App/$REMOTE_NAME"

resolve_ssh

# Refuse to overwrite an app that is not ours (e.g. Onion's own entries).
if rsh "test -d '$REMOTE_DIR' && test ! -f '$REMOTE_DIR/.hge_deploy'"; then
    die "$REMOTE_DIR exists and was not deployed by this script; remove it by hand first"
fi

SIZE=$(du -sh "$LOCAL_APP" | awk '{print $1}')
log "deploying $LOCAL_APP ($SIZE) -> $REMOTE_DIR"

rsh "mkdir -p '$REMOTE_DIR'"
tar -C "$LOCAL_APP" -cf - . | rsh "tar -xf - -C '$REMOTE_DIR'"
rsh "touch '$REMOTE_DIR/.hge_deploy'"

# Make the things that must be executable executable; the FAT /mnt/SDCARD does
# not carry the mode bits from the tarball reliably.
rsh "cd '$REMOTE_DIR' && chmod +x launch.sh bin/* *.sh 2>/dev/null; true"

log "on device:"
rsh "cd '$REMOTE_DIR' && ls -la | head -20"
log "done. start it from MainUI, or: ssh $SSH_HOST '$REMOTE_DIR/launch.sh'"
