# 移植指南

## 新增平台（OS/设备）

1. 实现 `plat.h` 的接口（窗口、事件、时间、输入、文件、呈现）。
2. SDL2 路线：在 `engine/src/platform/` 增加 `plat_sdl2_<name>.cpp`，实现
   `plat_sdl2_internal.h` 里的 backend hooks；common 层不需要改。
3. CMake 增加 `HGE_PLATFORM` 取值与对应源文件；不要在共享代码写平台宏。
4. 跑 `gfx_conformance_*` / `--selftest` / `--sim-only` / offscreen 渲染。
5. 设备端记录：启动、出图、输入、声音、退出/返回桌面。

## 新增渲染后端

1. 实现 `IGfx`（`engine/include/hge/hge_gfx.h`）。
2. 提供工厂（如 `CreateGfxXYZ()`），在 `engine/src/core/gfx_factory.cpp` 选择。
3. 用 `gfx_conformance_blit/sdl` 或 `m1_demo --selftest` 的方式写像素断言。
4. 确认 `BeginFrame/Clear/DrawQuads/ReadPixels` 语义一致。

## 新增宿主

1. 在 `HGE_HOST` 增加取值，并实现帧驱动（standalone / libretro 是现有例子）。
2. libretro 路线只需游戏实现 `hge::libretro::CreateGame/GetGameInfo`。
3. 宿主不得让游戏 include 宿主内部头。

## MCU / RT-Thread 预留

- 公共 API 不出现 SDL/OS 类型；服务接口化。
- 资源支持 XIP 常量区 + 文件流式读取，不假设整包读入。
- 标量渲染路径可用；`HGE_FEATURE_THREADS=0` 已支持。
- 线程、分配器、cache/MPU 细节在真正移植时补。
- 第三方库优先 miniz；资产继续 PC 端烘焙，避免运行时解码器。

## 验收清单

- 公共头自包含；`examples/hello_game` 能编译。
- 全 preset 编译；主机 CTest 通过。
- `--sim-only` checksum 稳定；确定性渲染可复现。
- 设备人工冒烟通过。
