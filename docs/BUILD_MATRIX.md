# 构建矩阵

## 环境准备（WSL2 Ubuntu / 原生 Linux）

要求 **CMake >= 3.20**（`CMakeLists.txt` 的最低版本）；Ubuntu 24.04 的 apt 包是 3.28.x。

### 1. 基础工具

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake pkg-config python3 git sshpass
# 可选：ninja-build（当前 preset 用 Unix Makefiles，不装也能编）
```

验证：

```bash
cmake --version
g++ --version
```

### 2. 桌面 SDL2 依赖

桌面构建会从 `third_party/SDL2` 源码静态编译 SDL2，需要 X11/Wayland/音频开发头：

```bash
sudo apt-get install -y \
    libasound2-dev libpulse-dev \
    libx11-dev libxext-dev libxrandr-dev libxi-dev \
    libxcursor-dev libxinerama-dev libxss-dev libxkbcommon-dev \
    libwayland-dev libdecor-0-dev \
    libdrm-dev libgbm-dev libudev-dev libdbus-1-dev libibus-1.0-dev
```

SDL2 源码查找顺序：

1. `<repo>/third_party/SDL2`
2. `-DHGE_SDL2_SOURCE=<含 CMakeLists.txt 的目录>`

仓库里没有 `third_party/SDL2` 时，下载 SDL2 源码（如 2.32.10）解压到该目录，或配置时指定路径。

### 3. Miyoo 交叉工具链

`miyoo-cross` / `libretro-arm` 固定使用 `/opt/miyoomini-toolchain`：

```bash
sudo tar -xf miyoomini-toolchain.tar.xz -C /opt
# 目录名必须是 /opt/miyoomini-toolchain
/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-gcc --version
```

工具链的 sysroot 需要包含 `mi_gfx.h` / `libmi_gfx.*`（MI_GFX/MI_SYS）：
`/opt/miyoomini-toolchain/arm-linux-gnueabihf/libc/usr/{include,lib}`。
仓库的 `libs/` 是**设备运行时**用的 SDL2/EGL/MI 动态库，不是编译工具链。

### 4. libretro

`third_party/libretro` 已随仓库提供；`libretro-*` preset 只需要这个头目录。

### 5. PC 侧部署工具

- 通用：`ssh`、`scp`；
- 自动连接脚本：`sshpass`（`miyoo_connect.sh`）；Linux 主机还需要 `nmcli`（NetworkManager）；
- Windows/WSL：使用系统自带的 `powershell.exe` / `netsh.exe`。

### 6. 环境自检

```bash
cmake --preset linux-native
cmake --build --preset linux-native -j"$(nproc)"
ctest --test-dir build-native --output-on-failure

cmake --preset miyoo-cross
cmake --build --preset miyoo-cross -j"$(nproc)"
```

设备部署步骤见 `package/DEPLOY.md`。

## Presets

| Preset | 平台 | 渲染 | 宿主 | 产物 |
|---|---|---|---|---|
| `linux-native` | desktop | GL | standalone | `build-native/bubble_level1`、`hello_game`、`m1_demo` |
| `linux-nothreads` | desktop | GL | standalone | 同上，`HGE_FEATURE_THREADS=OFF` |
| `miyoo-cross` | miyoo | SDL_Renderer | standalone | `build-miyoo/bubble_level1` |
| `libretro-arm` | miyoo | blit | libretro | `build-libretro/bubble_libretro.so` |
| `libretro-x86` | desktop | blit | libretro | `build-libretro-x86/bubble_libretro.so` |
| `windows-mingw` | desktop | GL | standalone | `build-win-mingw/bubble_level1.exe`（需 MinGW，暂未验证） |

## 命令

```bash
cmake --preset linux-native
cmake --build --preset linux-native -j
ctest --test-dir build-native --output-on-failure

cmake --preset miyoo-cross
cmake --build --preset miyoo-cross -j

cmake --preset libretro-arm
cmake --build --preset libretro-arm -j
```

`Makefile.miyoo` / `Makefile.libretro` 是对应 preset 的薄包装。

## 依赖

- 桌面：`third_party/SDL2` 源码静态构建；需要 X11/Wayland/音频开发头。
- Miyoo：`/opt/miyoomini-toolchain` + `libs/` 预编译 SDL2/MI 库。
- libretro：`third_party/libretro` 头 + 目标平台的 SDL2。

## 测试

主机侧：`ctest`（blit / gfx_sdl / sim golden）。详见 `docs/TESTING.md`。
Miyoo 用 SSH 部署后跑 `--selftest`、`--sim-only`、`BUBBLE_OFFSCREEN=1 --render-test`，
再由人工做菜单冒烟。
