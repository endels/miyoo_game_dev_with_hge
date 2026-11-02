# HGE-SDL — HGE 1.81 的双平台图形/平台层

把 HGE 1.81 的 **Direct3D 8 + Win32** 后端换成 **SDL2 + OpenGL ES 2.0**，
一套代码出 Windows / Linux / 嵌入式 Linux（kmsdrm）三个目标。

当前进度：**M1 完成** —— 可编译骨架，渲染链路已验证。

---

## 1. 快速开始（Windows）

> Linux/WSL（Miyoo 交叉编译推荐）：CMake/工具链/SDL2 依赖的安装见
> `docs/BUILD_MATRIX.md` 的“环境准备”。

```powershell
# 配置（SDL2 源码由 CMake 自动在下面两个位置查找）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# 编译
cmake --build build --config Release --parallel

# 自检（不需要人看画面，校验像素后返回 0/1）
build\Release\m1_demo.exe --selftest

# 正常跑 120 帧后退出；不带参数则 ESC 退出
build\Release\m1_demo.exe --frames 120
```

SDL2 源码查找顺序：

1. `<repo>/third_party/SDL2`
2. `G:/HGE_Dev/third_party/SDL2`（当前机器上的实际位置）
3. 手动指定：`-DHGE_SDL2_SOURCE=<路径>`

> 放在 `G:\HGE_Dev` 而不是仓库内的原因：仓库路径含中文，bsdtar 解压
> SDL2 目录树时会因长路径 + 非 ASCII 失败。SDL2 属于外部依赖，单独放一条
> 短路径更省事。交叉编译 Linux 时用同一份源码。

## 2. M1 验证了什么

`--selftest` 的输出（本机 RTX 2070 / 581.15 驱动）：

```
[m1] renderer: GL 4.6.0 NVIDIA 581.15 | NVIDIA GeForce RTX 2070 SUPER | maxtex 32768
[m1] px(60,60)   = FFCC2020  want ~CC2020  (scissor inside)
[m1] px(300,538) = FF101018  want ~101018  (scissor clipped)
[m1] px(centre)  = FFF0E0A0  want ~F0E0A0  (sprite fill)
[m1] px(200,200) = FF2E4A2E  want tile, not background
[m1] SELFTEST PASS
```

逐项对应的能力：

| 检查 | 验证的能力 |
|---|---|
| 像素 **精确** 等于 `FF2E4A2E` | ARGB→RGBA 上传 + shader `.bgra` swizzle 正确（通道没串） |
| 中心是精灵填充色 | 动态 VBO 批量提交 + HGE 语义的 2D 变换（绕 (x,y) 缩放/旋转） |
| 裁剪区内 = 红、区外 = 背景 | `glScissor` 的 y 翻转与开关正确 |
| 96×96 纹理可用 | NPOT 纹理（ES2 硬性要求 `CLAMP_TO_EDGE` + 无 mipmap） |
| 20×11 共 220 个 48px 图块一帧内完成 | 批量路径：按状态切换 flush，不是每图块一次 draw call |

`m1_demo.exe` 静态链接 SDL2，**单文件 1.7 MB，无任何 DLL**。

## 3. 目录结构

```
engine/include/hge/        引擎公共头（types / gfx / audio / resource / system / app / libretro）
engine/src/platform/       平台接口与 desktop / miyoo / libretro 后端
engine/src/gfx/            IGfx 实现：gfx_gl / gfx_sdl / gfx_blit
engine/src/core/           主循环、后端工厂、系统服务
engine/src/audio/          混音器
engine/src/resource/       pak 加载
engine/src/host/           libretro 通用宿主
games/bubble/              泡泡龙业务层（逻辑 / 渲染 / AI / 入口 / 元数据）
examples/m1_demo/          M1 演示 + 自检
examples/hello_game/       公共 API 边界探针
tests/                     主机侧一致性测试与 golden 数据
docs/                      架构/测试/移植说明
cmake/toolchains/          Miyoo / MinGW 交叉工具链
CMakePresets.json          linux-native / miyoo-cross / libretro-* / windows-mingw
NOTICE / LICENSE-HGE.txt   zlib 授权要求的"修改版声明"与原始许可
```

构建：`cmake --preset <name> && cmake --build --preset <name>`；
`Makefile.miyoo` / `Makefile.libretro` 现在是调用对应 preset 的薄包装。

## 4. 已经固化下来的三条移植红线

1. **`DWORD` 必须是 `uint32_t`**，不能用 `unsigned long`。
   Linux x64 上 `unsigned long` 是 64 位，会静默改掉 `GfxVertex`
   （`3×float + DWORD + 2×float`）的 24 字节布局，编译能过、画面全乱。
2. **颜色一律走 `.bgra` swizzle**，不用 `GL_BGRA`（ES2 里不是核心功能）。
   HGE 的 `0xAARRGGBB` 在小端内存里就是 `B,G,R,A`，上传成 `GL_RGBA`
   再在 shader 里转一次，CPU 零开销。
3. **NPOT + 无 mipmap + `CLAMP_TO_EDGE` 锁死**。素材全是 768×768、
   960×1152 这类非 2 的幂尺寸，ES2 只保证这种组合可用。

另外 `gfx_gl.cpp` 已经给每张纹理保留 **RAM 影子缓冲**：HGE 靠
`Texture_Lock()` 在 CPU 上直接写纹理（字体图集、精灵拼图都这么干），
在 GLES 上做回读会慢到不可用，所以只在 Unlock/上传时同步。

## 5. 下一步（M2）

M2 的目标是把 `hge_impl.cpp / system.cpp / input.cpp / ini.cpp / power.cpp`
接上来，让 `hgeCreate(HGE_VERSION)` → `System_Initiate()` → `hgeSprite` /
`hgeFont` / `hgeParticleSystem` / `hgeGUI` 这些**上层一行不改**就能跑。

映射关系已经确定：

| HGE 原函数（graphics.cpp，275 处 D3D 引用） | 新接口 |
|---|---|
| `Gfx_BeginScene / Gfx_EndScene` | `IGfx::BeginFrame / EndFrame` |
| `Gfx_Clear` | `IGfx::Clear` |
| `Gfx_RenderQuad / RenderTriple / RenderLine` | `IGfx::DrawQuads / DrawTriangles` |
| `Gfx_StartBatch / Gfx_FinishBatch` | 批处理已在后端内部，可做空实现 |
| `Gfx_SetClipping` | `IGfx::SetScissor` |
| `Gfx_SetTransform` | `IGfx::SetTransform` |
| `System_SetState(HGE_TEXTUREFILTER/...)` | `IGfx::SetBlend / SetTextureFilter` |
| `Texture_Create / Load / Lock / Unlock / Free` | `CreateTexture / UpdateTexture / ReleaseTexture` |
| `Gfx_SetTarget`（渲染到纹理） | FBO，入口已在 `gl_api` 预留（可选加载） |

## 6. 嵌入式 Linux

```bash
cmake -S . -B build-arm -DHGE_GLES=ON \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-linux-gnueabihf.cmake
```

`-DHGE_GLES=ON` 会：

- 请求 **OpenGL ES 2.0** 上下文（`SDL_GL_CONTEXT_PROFILE_ES`）
- 编译 `#version 100` + `precision mediump float` 版本的 shader（逻辑相同）

运行时（无桌面环境）：

```bash
SDL_VIDEODRIVER=kmsdrm ./m1_demo
```

注意：`kmsdrm` 依赖 EGL + GBM。Pi / Mali / Vivante 的官方驱动一般都有；
只有 framebuffer 没有 GBM 的板子需要另做兜底路径（M6 实机再处理）。

## 7. 授权

- 原版 HGE 1.81：zlib/libpng，见 `LICENSE-HGE.txt`。
- 本仓库是**修改版**，已在 `NOTICE` 中明确声明，符合 zlib 第 2 条。
- 未使用 BASS（原版随附但不属于 HGE 授权范围，商用需另购）。
  音频层计划用 miniaudio（MIT）+ stb_vorbis。

---

# 8. 泡泡龙 第一关（bubble_level1）

用现有 FSM 素材做的一个可玩关卡，跑在 M1 那套 `plat` + `IGfx` 上（不经过 HGE API）。

## 运行

```powershell
build\Release\bubble_level1.exe                  # 玩：鼠标瞄准，左键/空格发射，R 重开，ESC 退出
build\Release\bubble_level1.exe --selftest       # 无窗口自检
build\Release\bubble_level1.exe --warmup 40 --shot a.ppm   # 预演40发后截图
```

`game.pak` / `audio.pak` 由 CMake POST_BUILD 嵌入 exe（桌面构建）；dev 时也可放在 exe 旁边。

## 规则

- 16 列六边形错位网格，40px 泡泡，4 色（黄/蓝/绿/红）
- 发射后直线飞行、碰左右墙反弹，碰到泡泡或天花板就吸附到最近的空格
- 同色 ≥3 连通消除；消除后不与第 0 行连通的泡泡整串掉落（+20 分/个）
- 每 16 发整体下压一行；泡泡触到死亡线即失败，清空即通关

## 美术怎么来的

| 元素 | 来源 | 处理 |
|---|---|---|
| 泡泡 | FSM 图块（苔藓/岩壁/芦苇/壁草/洞窟石/珊瑚） | `make_bubble()`：玻璃球壳 + 内芯纹理，见 `tools/preview_assets.py` |
| 背景 | `parallax_Resource_Pack/ForestNight.png`（816x816 俯视树冠） | 裁成 960x540、高斯模糊、压暗 |
| 炮台 | 程序化（石座 + 木桶，带铁箍） | `tools/bake_game_assets.py` |
| 字体 | Consolas 18px 烘成 16x6 图集 | 整数倍缩放绘制（<1 倍会丢笔画） |
| 粒子 | 程序化柔光点 / 圆环 | 同一个 pak |

全部烘焙进 `assets/game.pak`（2.3MB，BGRA 原始像素，运行时零解码依赖）。
换素材只需改 `tools/bake_game_assets.py` 里的坐标再跑一次。

## 自检验证了什么

```
[bubble] SELFTEST PASS: shots=35 rows=14 left=101 score=0 state=LOSE ok
```

- 400 次随机发射期间，每一格颜色合法、不越界、**不存在悬空泡泡**
- 定向用例：3 连消、悬空掉落、单泡泡吸附、清空棋盘正确判定 WIN
- `--warmup` 预演可以复现中局局面，用来肉眼检查吸附位置

未覆盖：真人难度曲线。预演 AI 很笨（会往同一列堆），20~35 发就会触线，属正常。

## 测试

```bash
ctest --test-dir build-native --output-on-failure
./build-native/bubble_level1 --selftest
./build-native/bubble_level1 --sim-only 5
./build-native/bubble_level1 --render-test --frames 60 --shot /tmp/a.ppm
./build-native/bubble_level1 --render-test --frames 46 --replay tests/replay/smoke.txt
```

详见 `docs/TESTING.md`：分层测试、确定性渲染、replay 格式、golden 更新流程。
架构/构建矩阵/移植见 `docs/ARCHITECTURE.md`、`docs/BUILD_MATRIX.md`、`docs/PORTING.md`。

## Miyoo 部署与真机调试

- 部署包结构与完整步骤：`package/DEPLOY.md`
- 热点/SSH 工具目录：`bench_tools/README.md`
  （`miyoo_connect.sh`、`miyoo_bench_hotspot.sh`、设备端 `bench_hotspot_*.sh`）
- 退出游戏后会自动重跑 `bench_hotspot_boot.sh` 恢复热点，不需要重启机器。

## 已知待办

- 音效（FSM 示例工程自带 `Chest1/Magic1/Chime1` 等 SE，可用）
- 中文 UI 字体（mplus 是日文字形；要么找中文字体烘图集，要么走 hgeFont）
- 第 2/3 关：5 色/6 色 + 不同下压节奏
- 泡泡"破裂"用图块纹理做碎片（现在只有白点）