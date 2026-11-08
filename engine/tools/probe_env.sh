#!/bin/sh
# Collect build-environment facts before porting. Read-only, prints to stdout.
# Usage: sh engine/tools/probe_env.sh 2>&1 | tee env-report.txt

say() { printf '\n===== %s =====\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

say "system"
uname -a
[ -r /etc/os-release ] && cat /etc/os-release
printf 'arch: %s  cpus: %s\n' "$(uname -m)" "$(getconf _NPROCESSORS_ONLN 2>/dev/null)"

say "toolchain"
for c in cc gcc g++ clang clang++ cmake make ninja python3 pkg-config; do
    if have "$c"; then
        printf '%-10s %s\n' "$c:" "$(command -v "$c")"
    else
        printf '%-10s MISSING\n' "$c:"
    fi
done
have gcc && gcc --version | head -n1
have g++ && g++ --version | head -n1
have cmake && cmake --version | head -n1
have python3 && python3 --version

say "cross / embedded env markers"
env | grep -Ei 'CROSS|SYSROOT|TOOLCHAIN|BUILDROOT|YOCTO|PETALINUX|STAGING|_CC=|_CXX=' || echo "(none set in this shell)"
for d in /opt/*/ /usr/local/*toolchain* "$HOME"/x-tools/* /usr/lib/*-linux-gnu*; do
    [ -d "$d" ] && echo "dir: $d"
done 2>/dev/null | head -n 20

say "display / gpu"
printf 'DISPLAY=%s WAYLAND_DISPLAY=%s SDL_VIDEODRIVER=%s\n' \
    "${DISPLAY:-}" "${WAYLAND_DISPLAY:-}" "${SDL_VIDEODRIVER:-}"
ls -l /dev/dri 2>/dev/null || echo "/dev/dri: absent"
have glxinfo && glxinfo -B 2>&1 | head -n 12
have eglinfo && eglinfo 2>&1 | head -n 20
have vainfo && vainfo 2>&1 | head -n 5

say "gl / egl headers and libs"
for h in /usr/include/GLES2/gl2.h /usr/include/EGL/egl.h \
         /usr/include/libdrm/drm.h /usr/include/GL/gl.h; do
    if [ -r "$h" ]; then echo "header OK: $h"; else echo "header MISSING: $h"; fi
done
pkg-config --modversion egl glesv2 2>/dev/null | paste -sd' ' -
pkg-config --exists libdrm && echo "libdrm: $(pkg-config --modversion libdrm)" || echo "libdrm: not in pkg-config"
ldconfig -p 2>/dev/null | grep -E 'libGLESv2|libEGL|libSDL2|libasound' | head -n 10

say "SDL2"
pkg-config --modversion sdl2 2>/dev/null && pkg-config --cflags --libs sdl2 2>/dev/null \
    || echo "no system SDL2 (expected: we vendor SDL2-2.32.10 source)"
ls -d third_party/SDL2 2>/dev/null && echo "vendored SDL2 present" || echo "vendored SDL2 absent"

say "headless test capability"
have Xvfb && echo "Xvfb: yes" || echo "Xvfb: no"
have xvfb-run && echo "xvfb-run: yes" || echo "xvfb-run: no"

say "repo state"
have git && git log --oneline -3 2>/dev/null
ls games/*/assets/*.pak 2>/dev/null || echo "no games/*/assets/*.pak found"

say "cmake configure probe (no build)"
if [ -d third_party/SDL2 ]; then
    cmake -S . -B /tmp/hge_probe -DHGE_GLES=ON 2>&1 | tail -n 25
else
    echo "skipped: third_party/SDL2 not present; pass -DHGE_SDL2_SOURCE=<path>"
fi

say "done"
