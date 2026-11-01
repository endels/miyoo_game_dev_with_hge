# Bubble Versus — OnionOS / Miyoo Mini+ 部署包

HGE-SDL 泡泡龙，**纯软件渲染**路线（parasyte/Onion SDL2 → `/dev/fb0`）。
渲染只走 SDL 通用 API，后续可零改代码切到 MI_GFX 硬件加速。

## 包结构（映射到 SD 卡 `/mnt/SDCARD/App/Bubble/`）

```
App/Bubble/
├── config.json          ← MainUI 菜单项
├── launch.sh            ← 启动脚本（env/音频/频率/恢复）
├── icon.png             ← 菜单图标
├── bin/bubble_level1    ← ARM ELF 可执行文件（867KB）
├── lib/libSDL2-2.0.so.0 ← 随包 SDL2（mmiyoo fork, 软件渲染）
├── game.pak             ← 烘焙素材（BGRA，平台无关）
├── audio.pak            ← 烘焙音频
└── bubble.log           ← 运行时日志（首次运行后生成）
```

## 部署（SSH / scp）

前提：MMP 已装 Onion，WiFi + SSH 已开（Tweaks → Network → SSH）。

```bash
IP=192.168.51.244        # 换成你的设备 IP
cd hge-sdl/package

ssh -o MACs=hmac-sha1 onion@$IP "mkdir -p /mnt/SDCARD/App/Bubble/bin /mnt/SDCARD/App/Bubble/lib"

scp -o MACs=hmac-sha1 App/Bubble/config.json  onion@$IP:/mnt/SDCARD/App/Bubble/
scp -o MACs=hmac-sha1 App/Bubble/launch.sh    onion@$IP:/mnt/SDCARD/App/Bubble/
scp -o MACs=hmac-sha1 App/Bubble/icon.png     onion@$IP:/mnt/SDCARD/App/Bubble/
scp -o MACs=hmac-sha1 App/Bubble/game.pak     onion@$IP:/mnt/SDCARD/App/Bubble/
scp -o MACs=hmac-sha1 App/Bubble/audio.pak    onion@$IP:/mnt/SDCARD/App/Bubble/
scp -o MACs=hmac-sha1 App/Bubble/bin/bubble_level1      onion@$IP:/mnt/SDCARD/App/Bubble/bin/
scp -o MACs=hmac-sha1 App/Bubble/lib/libSDL2-2.0.so.0   onion@$IP:/mnt/SDCARD/App/Bubble/lib/

ssh -o MACs=hmac-sha1 onion@$IP "chmod +x /mnt/SDCARD/App/Bubble/launch.sh /mnt/SDCARD/App/Bubble/bin/bubble_level1"
```

重启 MMP，Apps 菜单出现 **Bubble**，点击运行。

## 操作

| 操作 | 手柄 |
|---|---|
| 瞄准 | 方向键 ← / →（或左摇杆） |
| 发射 | A |
| 重开 | B 或 Y |
| 退出 | START |

## 分辨率适配

游戏固定 **960×540（16:9）** 逻辑分辨率，由 `SDL_RenderSetLogicalSize` 等比缩放
到设备面板并居中（上下黑边），窗口尺寸取自 Onion 的 `/tmp/screen_resolution`
（launch.sh 导出 `BUBBLE_WIN_W/H`），兜底用 SDL 显示模式：

| 面板 | 缩放 | 实际画面 | 黑边 |
|---|---|---|---|
| 640×480 (4:3) | 0.667× | 640×360 | 上下各 60px |
| 752×560 | 0.783× | 752×423 | 上下各 ~68px |

比例正确（泡泡/瞄准几何不失真）。因 16:9→4:3，必然有上下黑边；
非整数缩放会让像素字略有锯齿。若偏好铺满全屏可改为拉伸（会纵向压扁），
或按 640×480 重排 UI（工作量大）。

## 运行时日志

`launch.sh` 把 stdout/stderr 写到 `App/Bubble/bubble.log`。首次运行后：

```bash
ssh -o MACs=hmac-sha1 onion@$IP "cat /mnt/SDCARD/App/Bubble/bubble.log"
```

关键行：
- `[plat] renderer=...` — 应为 `software`（硬件路线时为 `mmiyoo`）
- `[plat] video driver=...` — 应为 `mmiyoo`
- `[bubble] renderer: SDL2 renderer: ...`

## 已知事项 / 排障

| 现象 | 处理 |
|---|---|
| 菜单不出现 | 检查 `config.json` 存在、`icon.png` 存在、`launch.sh` 有执行位 |
| 黑屏闪退 | 看 `bubble.log`；多为 SDL2 依赖未解析（`libmi_*` 在 `/customer/lib`，`libEGL/libGLESv2` 在 `.tmp_update/lib/parasyte`） |
| 无声音 | 正常降级；`audio::Init` 失败不致命 |
| 帧率低 | 纯软渲染预期；二期切 Drastic 硬件版 SDL2（改 `launch.sh` 的 `LD_LIBRARY_PATH` + 随包换 `libSDL2`，代码不动） |

## 重新构建（WSL）

```bash
cd ~/miyoo_dev/hge-sdl
make -f Makefile.miyoo clean all          # ARM 产物 → build-miyoo/bubble_level1
# 重新打包：cp build-miyoo/bubble_level1 package/App/Bubble/bin/
```

桌面（x86）用原 CMake 流程；本包只涉及 `Makefile.miyoo + gfx_sdl + plat 的 MIYOO 分支`。

## 验证已完成的项（本地，无需设备）

- `--selftest` 在 **ARM 二进制** 上 PASS（qemu-arm，网格/物理逻辑）
- 完整渲染管线在 **x86 同源代码**（软件渲染器）上出图正常（泡泡/文字/叠层/结算画面）
- 设备 SDL2 所需符号全部存在（`libSDL2-2.0.so.0`）
- 无任何 GL/EGL 运行时依赖（`NEEDED` 仅 SDL2 + C/C++ 运行库）
