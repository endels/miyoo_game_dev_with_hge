# 构建矩阵

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
