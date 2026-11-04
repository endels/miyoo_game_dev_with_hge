# 测试与回归

目标：**不依赖“看窗口/看截图”**。宿主机 GUI 只用于人工冒烟，自动化检查靠断言、校验和和固定场景。

## 分层

| 层级 | 命令 | 检查内容 |
|---|---|---|
| L0 编译矩阵 | `cmake --build --preset ...` | x86 / Miyoo / libretro 全目标编译链接；`hello_game` 只依赖公共头 |
| L1 逻辑自检 | `bubble_level1 --selftest` | 网格/物理不变量，退出码 0/1 |
| L2 确定性模拟 | `bubble_level1 --sim-only 5` | 固定 1/60 步长跑纯战斗模拟，输出状态 + 校验和 |
| L3 确定性渲染 | `bubble_level1 --render-test --frames N --shot out.ppm` | 固定时间步长渲染，不依赖 wall clock |
| L4 后端一致性 | `gfx_conformance_blit` / `gfx_conformance_sdl` / `m1_demo --selftest` | 真实像素断言（blit / gfx_sdl / GL） |
| L5 真机冒烟 | 手动 | 启动、出图、按键、声音、退出 |

## 常用命令

```bash
# 主机全量测试（CMake 注册的 3 个）
ctest --test-dir build-native --output-on-failure

# 纯逻辑模拟（无窗口、无 GL、无音频）
./build-native/bubble_level1 --sim-only 5
./build-native/bubble_level1 --sim-only 5 --expect-checksum 0x0ca91d40f23485bc

# 确定性渲染（同一二进制重复跑应逐像素一致）
./build-native/bubble_level1 --render-test --frames 60 --shot /tmp/a.ppm
./build-native/bubble_level1 --render-test --frames 60 --shot /tmp/b.ppm
cmp /tmp/a.ppm /tmp/b.ppm

# 脚本输入回放（固定步长）
./build-native/bubble_level1 --render-test --frames 46 --replay tests/replay/smoke.txt

# 机器人自动对局（真实窗口/音频，长期压测用）
./build-native/bubble_level1 --autoplay
./build-native/bubble_level1 --render-test --autoplay --frames 18000

# 后端一致性
./build-native/gfx_conformance_blit
./build-native/gfx_conformance_sdl
./build-native/m1_demo --selftest
```

## 参数

| 参数 | 作用 |
|---|---|
| `--sim-only N` | 不初始化窗口/GL/音频，跑 N 秒战斗模拟后退出 |
| `--expect-checksum 0x...` | 与 `--sim-only` 联用，校验和不匹配返回 1 |
| `--render-test` | 固定 `1/60` 步长；默认至少 1 帧；配合 `--frames`/`--shot` |
| `--fixed-dt S` | 指定固定步长，渲染时间改为虚拟时间 |
| `--shot-frame N` | 指定截图帧（默认 40） |
| `--replay FILE` | 按帧注入输入，格式见下 |
| `--autoplay` | 开发用：机器人接管玩家侧（模拟候选落点选最优解，自动 Draft/重试） |
| `--retries N` | 每关允许的连续失败次数（默认 3），用尽后回到第一关 |
| `--mute` | 静音运行：音频回调照常走（`cb` 诊断还在），但不发声 |

## Replay 格式

```
# frame action [args]
0  aim 300 200
5  fire
20 aim 500 150
25 fire
45 quit
```

动作：`aim x y`、`fire`、`restart`、`quit`、`left`、`noleft`、`right`、`noright`。
动作按帧号顺序应用；同一帧可写多行。

## Golden 更新

`tests/golden/sim_5.txt` 是 `--sim-only 5` 的完整输出。逻辑改动导致校验和变化时，先确认变化符合预期，再执行：

```bash
./build-native/bubble_level1 --sim-only 5 > tests/golden/sim_5.txt
```

## 无线程构建

`linux-nothreads` preset 关闭 `HGE_FEATURE_THREADS`，用于验证 blitter 的
单线程路径（MCU 预留）：

```bash
cmake --preset linux-nothreads
cmake --build --preset linux-nothreads -j
ctest --test-dir build-native-nothreads --output-on-failure
```

## 边界探针

`examples/hello_game` 只 include `engine/include/hge`，不碰 `engine/src`。
它能编译就说明引擎/游戏公共头边界没有泄漏；`linux-native` 里会一起构建。

## gdb 附加调试

`linux-native` 是 Release（`-O3 -DNDEBUG`，无行号、无局部变量），只适合跑性能。
要附加调试用带符号的 `RelWithDebInfo` 构建：

```bash
cmake --preset linux-native-debug        # build-dbg/
cmake --build --preset linux-native-debug -j
```

WSL/Linux 默认 `kernel.yama.ptrace_scope=1`，禁止 attach 到非子进程。两个办法：

1. 直接 attach（`build-dbg` 在编译期开了 `HGE_DEBUG_ATTACH`，无需任何环境变量；
   `build-native` 等发布配置需要启动时带 `HGE_ALLOW_GDB=1`）：

```bash
LP_NUM_THREADS=4 ./build-dbg/bubble_level1 --autoplay
./tools/attach_dump.sh            # 自动挑最新的 bubble_level1
./tools/attach_dump.sh <pid>      # 或指定 pid
```

2. 或临时放开（需要 root）：`sudo sysctl -w kernel.yama.ptrace_scope=0`

`attach_dump.sh` 一次输出：内核线程视图（`comm` + `wchan`）、gdb 线程表、
全部线程 backtrace、混音器计数（`cb`/`active`/`music`/`peak`/queue 深度）、
以及 `SDL_GetAudioDeviceStatus`（0=stopped，1=playing，2=paused）。
附加是暂停-读取-脱离，不会留下调试断点；日志里 `cb` 会继续增长。

直接看某个线程在等什么，不用 gdb 也行（需 `PR_SET_PTRACER` 或同 UID 放宽）：

```bash
cat /proc/<pid>/task/<tid>/syscall
```

例如 `202` 是 `futex`，`7` 是 `poll`；配合 `wchan` 就能区分
“SDL 音频线程在等设备” 和 “在等 PulseAudio mainloop”。

## 静音调试

看视频/录屏时用 `--mute`（等价 `HGE_AUDIO_MUTE=1`）：设备照常打开、回调照常计数，
`[audio]` 行里 `peak=0 active=0 music=0`，不会发出任何声音。

## AI 墙面反弹调试

CPU 在“当前没有可消除的直射点”时有 60% 概率考虑“一次撞墙”的射法，且必须模拟验证
结果不比直射差（且至少消除 1 颗）才会用；`HGE_AI_BOUNCE` 可调节（0 = 关闭）。
调节概率/观察决策：

```bash
HGE_AI_BOUNCE=1.0 HGE_DEBUG_AI=1 ./build-native/bubble_level1 --sim-only 120
```

`HGE_AI_BOUNCE=0` 完全关闭；`HGE_DEBUG_AI=1` 打印每次反弹的瞄准点与两种射法的评分。

## 音频无声 A/B 开关

同一个二进制里保留了两条“旧路径”开关，复现“整体无声”时用来分离变量：

| 环境变量 | 作用 |
|---|---|
| `HGE_AUDIO_LOCKED=1` | `Play`/`StopEffects` 恢复旧的 `SDL_LockAudioDevice` 路径（不再走无锁队列） |
| `HGE_AUDIO_NO_REVIVE=1` | 关闭回调里的 BGM 自动重建 |

诊断日志（每秒一行 `[audio] cb=... music=... active=... peak=...`，回调停住时打 `STALL`）始终生效。

## 不自动化的部分

- 画面“好不好看”、手感/难度曲线、真机性能。
- GL 渲染依赖显示环境；CI 无显示时只跑 blit conformance 和 sim-only。
- Miyoo 真机：通过 SSH + 日志 + `/dev/fb0`/offscreen 做冒烟，不依赖人眼看窗口。
