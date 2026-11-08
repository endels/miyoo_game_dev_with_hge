#!/bin/sh
# Assemble a deployable OnionOS App directory for quadbench.
#
#   ./make_app.sh <quadbench-binary> [out-dir]
#
# Produces <out-dir>/App/QuadBench/{bin/quadbench, lib/libSDL2-2.0.so.0,
# config.json, launch.sh}. Deploy with bench_tools/miyoo_deploy_app.sh.
set -eu

HERE=$(cd -- "$(dirname -- "$0")" && pwd)
ENGINE=$(cd -- "$HERE/../.." && pwd)
# libs/ sits beside the engine in a game repo and inside it in the engine repo.
if [ -d "$ENGINE/libs" ]; then ROOT="$ENGINE"; else ROOT=$(cd -- "$ENGINE/.." && pwd); fi

BIN=${1:?usage: make_app.sh <quadbench-binary> [out-dir]}
OUT=${2:-"$PWD/quadbench-app"}
SDL2_LIB=${SDL2_LIB:-"$ROOT/libs/libSDL2-2.0.so.0"}

[ -f "$BIN" ] || { echo "no such binary: $BIN" >&2; exit 1; }
[ -f "$SDL2_LIB" ] || { echo "no such SDL2: $SDL2_LIB" >&2; exit 1; }

APP="$OUT/App/QuadBench"
rm -rf "$APP"
mkdir -p "$APP/bin" "$APP/lib"
cp "$BIN" "$APP/bin/quadbench"
cp "$SDL2_LIB" "$APP/lib/libSDL2-2.0.so.0"
chmod +x "$APP/bin/quadbench"

cat > "$APP/config.json" <<'JSON'
{
  "label": "QuadBench",
  "icon": "../../Icons/Default/app/manual.png",
  "launch": "launch.sh",
  "description": "HGE renderer throughput probe"
}
JSON

cat > "$APP/launch.sh" <<'SH'
#!/bin/sh
# Runs the probe headless (HGE_OFFSCREEN=1) so it never touches /dev/fb0 while
# the menu is up. Two passes: synchronous rasterization (the honest upper bound)
# and the default worker-thread path.
progdir=$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd -P)
cd "$progdir" || exit 1
export HOME=/mnt/SDCARD
export LD_LIBRARY_PATH="$progdir/lib:/mnt/SDCARD/.tmp_update/lib/parasyte:/customer/lib:/config/lib:/lib:/usr/lib:$LD_LIBRARY_PATH"

[ -f /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh ] && \
    . /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh

ARGS="--size 640x480 --frames 60"

HGE_OFFSCREEN=1 HGE_IMMEDIATE=1 ./bin/quadbench $ARGS > report-immediate.txt 2>&1
HGE_OFFSCREEN=1               ./bin/quadbench $ARGS > report-mt.txt        2>&1

sync
cat report-immediate.txt report-mt.txt
SH
chmod +x "$APP/launch.sh"

echo "app dir: $APP"
du -sh "$APP"
ls -la "$APP" "$APP/bin"
