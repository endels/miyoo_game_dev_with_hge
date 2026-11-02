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
- MMIYOO 的 SDL renderer 没有 `RenderCopyEx`。`gfx_sdl` 把带
  `SetTransform` 的 quad 先用 CPU 光栅化到 scratch texture，再走普通
  `RenderCopy`；`gfx_blit` 直接按三角形光栅化。这样三个后端语义一致。

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

## 路线图

| 阶段 | 内容 | 状态 |
|---|---|---|
| P0 | 目录/构建/服务拆分，CMake preset + 工具链 | 已完成 |
| P1 | 平台/主循环/语义输入/config/后端一致性测试 | 已完成 |
| P2 | 公共 API 收口、hello_game 边界探针、通用 libretro 宿主、CI | 已完成 |
| 迭代期 | 实际游戏需求驱动 API 稳定；修 bug、补测试、考虑第二个游戏 | 进行中 |
| P3 | 引擎/游戏物理拆仓、资产 Release、SDL2 submodule、版本 tag | **暂缓**（第二个游戏验证后再评估） |
| P4 | RT-Thread / STM32H7 MCU 可行性 spike（只保留架构，不实现） | 未开始 |

原则：先让 API 在真实迭代中稳定，再拆仓；不为了拆而拆。P3 之前继续在
monorepo 内演进，公共头变更需同步 `docs/ARCHITECTURE.md` 与边界探针。
