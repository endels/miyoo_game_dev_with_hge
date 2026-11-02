# 测试与回归

目标：**不依赖“看窗口/看截图”**。宿主机 GUI 只用于人工冒烟，自动化检查靠断言、校验和和固定场景。

## 分层

| 层级 | 命令 | 检查内容 |
|---|---|---|
| L0 编译矩阵 | `cmake --build --preset ...` | x86 / Miyoo / libretro 全目标编译链接 |
| L1 逻辑自检 | `bubble_level1 --selftest` | 网格/物理不变量，退出码 0/1 |
| L2 确定性模拟 | `bubble_level1 --sim-only 5` | 固定 1/60 步长跑纯战斗模拟，输出状态 + 校验和 |
| L3 确定性渲染 | `bubble_level1 --render-test --frames N --shot out.ppm` | 固定时间步长渲染，不依赖 wall clock |
| L4 后端一致性 | `gfx_conformance_blit` + `m1_demo --selftest` | 真实像素断言（blit / GL） |
| L5 真机冒烟 | 手动 | 启动、出图、按键、声音、退出 |

## 常用命令

```bash
# 主机全量测试（CMake 注册的 2 个）
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

# 后端一致性
./build-native/gfx_conformance_blit
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

## 不自动化的部分

- 画面“好不好看”、手感/难度曲线、真机性能。
- GL 渲染依赖显示环境；CI 无显示时只跑 blit conformance 和 sim-only。
- Miyoo 真机：通过 SSH + 日志 + `/dev/fb0`/offscreen 做冒烟，不依赖人眼看窗口。
