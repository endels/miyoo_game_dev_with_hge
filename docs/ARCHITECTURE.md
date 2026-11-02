# 架构

## 分层

```
games/*            业务层：只 include engine/include/hge
    │  IGame / Input / Run
engine/include/hge 公共 API（唯一允许游戏依赖的头）
engine/src         引擎实现（对游戏不可见）
    ├── core        主循环、后端工厂、系统服务
    ├── gfx         IGfx 实现（gl / sdl / blit）
    ├── audio       混音器
    ├── resource    pak 加载
    ├── platform    plat.h + desktop / miyoo / libretro 后端
    └── host        libretro 通用宿主
```

依赖方向只能向下：游戏 → 公共 API → 引擎实现 → 平台/后端。游戏不得 include
`engine/src` 或 `<SDL.h>`。

## 三个正交维度

| 维度 | 取值 | 作用 |
|---|---|---|
| `HGE_PLATFORM` | `desktop` / `miyoo` | 窗口、输入、呈现、文件、时间 |
| `HGE_GFX` | `gl` / `sdl` / `blit` | IGfx 后端 |
| `HGE_HOST` | `standalone` / `libretro` | 谁驱动帧循环 |

平台后端按文件选择（`plat_sdl2_desktop.cpp` / `plat_sdl2_miyoo.cpp` /
`plat_libretro.cpp`），不在共享代码里写平台宏。

## 公共 API

| 头 | 内容 |
|---|---|
| `hge_types.h` | 固定宽度类型、ARGB 辅助 |
| `hge_config.h` | CMake 生成的平台/feature 常量 |
| `hge_gfx.h` | `IGfx`、`GfxVertex`、`CreateGfx()` |
| `hge_audio.h` | 混音器接口 |
| `hge_resource.h` | `Pack` / `Image`（烘焙资产） |
| `hge_system.h` | 时间、文件、BasePath、MessageBox |
| `hge_app.h` | `IGame`、`Input`、`RunConfig`、`Run()` |
| `hge_libretro.h` | 通用 libretro 宿主契约（游戏实现 CreateGame/GetGameInfo） |

## 引擎/游戏边界

- 引擎拥有窗口、计时、输入语义化、截图、退出和资源/系统服务。
- 游戏实现 `IGame::Init/Update/Render/OnResize/Shutdown`，只消费语义输入。
- 后端选择由 `hge::CreateGfx()` 完成，游戏不感知 GL/SDL/blit。
- 示例：`examples/hello_game` 是边界探针，只能使用公共头。

## 平台与呈现

- SDL2 平台拆成 common + desktop + miyoo；common 只做事件分发和共享服务。
- Miyoo：软件渲染 + `MI_GFX`/`/dev/fb0` 呈现。
- 桌面：GL 直绘。
- blit：CPU 光栅化到 ARGB 帧缓冲，供 libretro 与低功耗目标使用。

## libretro 宿主

`engine/src/host/libretro_core.cpp` 提供通用 `retro_*` 入口；游戏只需：

```cpp
namespace hge { namespace libretro {
IGame* CreateGame();
const GameInfo& GetGameInfo();
}}
```

`plat_libretro` 用 coroutine 把 `hge::Run` 的 `SwapBuffers` 映射为
`retro_run` 的一帧。

## Feature flags

`hge_config.h` 生成 `HGE_FEATURE_AUDIO/THREADS/FILESYSTEM`。目前 `THREADS`
已接入 blitter（单线程/多线程路径）；音频/文件系统的裁剪留给 MCU 阶段。
