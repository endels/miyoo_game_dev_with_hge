# Bubble Versus — OnionOS / Miyoo Mini+ 部署

两种入口共用同一套 `engine/` + `games/bubble/` 代码：

| 入口 | 启动方式 | 渲染后端 |
|---|---|---|
| **MainUI（推荐）** | `config.json` → `launch.sh` → RetroArch + `bubble_libretro.so` | CPU blit（libretro） |
| Standalone（可选） | 手动执行 `launch_standalone.sh` | Miyoo SDL 软件渲染 + `/dev/fb0` |

MainUI 菜单实际走 `launch.sh`；`launch_standalone.sh` 只用于直连调试。

## 1. 环境准备与构建

前置环境（CMake、编译依赖、Miyoo 工具链、SDL2 源码）见
[`docs/BUILD_MATRIX.md`](../docs/BUILD_MATRIX.md) 的“环境准备”一节。
最小安装（Ubuntu/WSL）：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config python3 git sshpass
```

然后：

```bash
cd ~/miyoo_dev/hge-sdl

# standalone
cmake --preset miyoo-cross
cmake --build --preset miyoo-cross -j

# libretro core
cmake --preset libretro-arm
cmake --build --preset libretro-arm -j
```

产物：

- `build-miyoo/bubble_level1`
- `build-libretro/bubble_libretro.so`

把产物放进部署目录：

```bash
cp build-miyoo/bubble_level1      package/App/Bubble/bin/
cp build-libretro/bubble_libretro.so package/App/Bubble/
```

## 2. 包结构（映射到 SD 卡 `/mnt/SDCARD/App/Bubble/`）

```
App/Bubble/
├── config.json             ← MainUI 菜单项
├── launch.sh               ← MainUI 入口：RetroArch + bubble_libretro.so
├── launch_standalone.sh    ← 可选：直接 /dev/fb0 调试
├── bubble_libretro.so      ← libretro core
├── bin/bubble_level1       ← ARM standalone 可执行文件
├── lib/libSDL2-2.0.so.0    ← 随包 SDL2
├── game.pak                ← 烘焙素材
├── audio.pak               ← 烘焙音频
├── icon.png                ← 菜单图标
├── bubble.log              ← standalone 日志
└── ra.log                  ← RetroArch 日志
```

## 3. 部署（SSH / scp）

前提：Onion 已开 WiFi + SSH；PC 已连到设备热点或同一网络。
`IP` 按实际替换（B 模式热点默认 `192.168.100.100`）。

```bash
IP=192.168.100.100
cd hge-sdl/package

ssh onion@$IP "mkdir -p /mnt/SDCARD/App/Bubble/bin /mnt/SDCARD/App/Bubble/lib"

scp App/Bubble/config.json              onion@$IP:/mnt/SDCARD/App/Bubble/
scp App/Bubble/launch.sh                onion@$IP:/mnt/SDCARD/App/Bubble/
scp App/Bubble/launch_standalone.sh     onion@$IP:/mnt/SDCARD/App/Bubble/
scp App/Bubble/bubble_libretro.so       onion@$IP:/mnt/SDCARD/App/Bubble/
scp App/Bubble/icon.png                 onion@$IP:/mnt/SDCARD/App/Bubble/
scp App/Bubble/game.pak                 onion@$IP:/mnt/SDCARD/App/Bubble/
scp App/Bubble/audio.pak                onion@$IP:/mnt/SDCARD/App/Bubble/
scp App/Bubble/bin/bubble_level1        onion@$IP:/mnt/SDCARD/App/Bubble/bin/
scp App/Bubble/lib/libSDL2-2.0.so.0     onion@$IP:/mnt/SDCARD/App/Bubble/lib/

ssh onion@$IP "chmod +x /mnt/SDCARD/App/Bubble/launch.sh \
    /mnt/SDCARD/App/Bubble/launch_standalone.sh \
    /mnt/SDCARD/App/Bubble/bin/bubble_level1 \
    /mnt/SDCARD/App/Bubble/bubble_libretro.so"
```

重启 MMP 后，Apps 菜单出现 **Bubble**。从 MainUI 启动即可。

## 4. 调试热点与工具目录

相关工具已收录在仓库的 **`bench_tools/`**：

| 路径 | 运行位置 | 作用 |
|---|---|---|
| `bench_tools/miyoo_connect.sh` | PC | 连接 Miyoo AP，并检查 `192.168.100.100:22` |
| `bench_tools/miyoo_bench_hotspot.sh` | PC | 安装/开关/卸载 B 模式热点 |
| `bench_tools/miyoo_net.conf.example` | PC | 本地配置模板（`miyoo_net.conf` 被 git 忽略） |
| `bench_tools/miyoo_hotspot_default.md` | — | 热点原理、排障、回滚说明 |
| `bench_tools/device/bench_hotspot_boot.sh` | **Miyoo** | 开机/退出游戏后拉起 hostapd + dnsmasq |
| `bench_tools/device/bench_hotspot_install.sh` | **Miyoo** | 给 `runtime.sh` 加 B 模式钩子（备份 `.bench.bak`） |
| `bench_tools/device/bench_hotspot_{on,off,uninstall}.sh` | **Miyoo** | 开关/卸载 B 模式 |

首次配置：

```bash
cd hge-sdl/bench_tools
cp miyoo_net.conf.example miyoo_net.conf
# 按实际填写 AP_PASS / MIYOO_PASS / WIFI_IFACE；不要提交 miyoo_net.conf

./miyoo_connect.sh --force          # PC 连热点并确认 SSH 可达
./miyoo_bench_hotspot.sh install    # 部署设备脚本 + runtime 钩子 + 开启标记
./miyoo_bench_hotspot.sh status     # 查看 hostapd/dnsmasq/wlan1 状态
```

### 退出游戏后自动恢复热点

`launch.sh` / `launch_standalone.sh` 在游戏退出后会后台调用设备上的
`/mnt/SDCARD/.tmp_update/script/bench_hotspot_boot.sh`，重新拉起热点。
这个钩子依赖：

1. 设备上已安装 `bench_hotspot_boot.sh`（用 `miyoo_bench_hotspot.sh install`）；
2. `config/.bench_hotspot` 标记存在；
3. Onion 的 WiFi 设置为开启（`/customer/app/jsonval wifi` 返回 `1`）。

如果不需要自动热点，执行 `./miyoo_bench_hotspot.sh off`（或删除 `.bench_hotspot`）。

## 5. 操作

| 操作 | 手柄 |
|---|---|
| 瞄准 | 方向键 ← / →（或左摇杆） |
| 发射 | A |
| 重开 | B 或 Y |
| 退出 | START |

## 6. 分辨率

游戏固定 **960×540（16:9）** 逻辑分辨率，按面板比例缩放并居中：

| 面板 | 缩放 | 实际画面 | 黑边 |
|---|---|---|---|
| 640×480 (4:3) | 0.667× | 640×360 | 上下各 60px |
| 752×560 | 0.783× | 752×423 | 上下各 ~68px |

## 7. 日志与排障

```bash
ssh onion@$IP "tail -50 /mnt/SDCARD/App/Bubble/ra.log"
ssh onion@$IP "tail -50 /mnt/SDCARD/App/Bubble/bubble.log"
```

| 现象 | 处理 |
|---|---|
| 菜单不出现 | 检查 `config.json`、`icon.png`、`launch.sh` 执行位 |
| MainUI 启动黑屏 | 看 `ra.log`；确认 `bubble_libretro.so` 已更新并匹配架构 |
| Windows 提示密码错误/一直验证 | 删除旧 WLAN profile 后重连：`netsh.exe wlan delete profile name="MiyooMiniAP"`，再跑 `miyoo_connect.sh --force` |
| 退出游戏后热点不在 | 确认 `bench_hotspot_boot.sh` 已安装、`.bench_hotspot` 存在；看 `.tmp_update/bench_hotspot_boot.log` |
| 无声音 | 正常降级；`audio::Init` 失败不致命 |
| 帧率低 | libretro 走 CPU blit，属预期；可对比 `[core] perf` 日志 |

## 8. 本地验证

```bash
ctest --test-dir build-native --output-on-failure
./build-native/bubble_level1 --selftest
./build-native/bubble_level1 --sim-only 5 --expect-checksum 0x0ca91d40f23485bc
./build-native/bubble_level1 --render-test --frames 45 --replay tests/replay/smoke.txt
```

真机冒烟：SSH 后用 `BUBBLE_OFFSCREEN=1` 跑 `--render-test --shot`，再拉回 PPM/PNG 检查画面。
