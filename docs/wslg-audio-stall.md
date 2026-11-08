# WSL 下音频卡顿：判定与结论

> 日期：2026-11-08 · 记录人：DSH
> 用途：以后在 WSL 里跑 `bubble_level1` 遇到"声音卡/爆音"时，先看这份，别当成游戏 bug 去查混音器。

## 1. 现象

在 WSL（WSLg + llvmpipe 软件渲染）里跑游戏，音频会间歇性卡顿、爆音，尤其在**大消除、大团掉落**（粒子与音效瞬间变多）之后最明显。游戏逻辑不掉帧。

## 2. 实测数据（同一份代码）

| 环境 | 混音回调速率（`[audio] cb=`） | 一秒内无回调（`STALL`） |
|---|---|---|
| WSL 里手动玩，约 3 分钟 | **约 0.6 次/秒**（应 5.4） | **31 次** |
| Windows 原生，60 秒 autoplay（74 发） | **5.33 次/秒**（理论 5.4） | **0 次** |

同一份二进制逻辑（3 发/回合、零冻结版），唯一变量是音频后端：

- WSL：`pulseaudio`，经 **WSLg 的 RDP 音频桥**（`/mnt/wslg/PulseServer`）转给 Windows 侧；
- Windows：`wasapi`，由 `SDL_OpenAudioDevice` 直接持有设备。

## 3. 怎么判定（不用改代码）

游戏每秒打印一行 `[audio]` 诊断（`engine/src/audio/audio.cpp` 的 `Tick()`）：

```
[audio] t=126.1 STALL cb=653 music=1 active=0 peak=14501 focus=1 min=0
```

- `cb=` 是混音回调累计次数。设备缓冲 8192 样本 @44100Hz ≈ **5.4 次/秒**；明显低于这个值就是设备在被"饿死"。
- `STALL` = 这一整秒回调次数没有增加，等同于音频断流。
- `HGE_AUDIO_MUTE=1` 或 `--mute` 可以关掉混音（回调仍会跑），用来区分"混音器慢"还是"设备不来数据"。

## 4. 根因

WSLg 的音频桥是**弱实时**通道：音频数据要经 RDP 通道交给 Windows 侧的 `PulseAudioRDPSink`。游戏用 llvmpipe 软件渲染时主线程会长时间占满一个核（实测约 71% CPU、单核），加上大消除时粒子和音效暴涨，这条通道就会断供——回调次数掉到实时的十分之一，缓冲被抽干，于是听到卡顿/爆音。

这不是混音器的问题：混音器在 Windows 上同样的负载下回调稳定在 5.4 次/秒。

## 5. 结论

- **WSL 只用于调试**（构建、`--duel-sim`、`--auto-play`、截图、断点），不要用它判断音频/帧率手感。
- 要听声音、要感受手感：用 **Windows 版**（wasapi，已实测 60 秒 / 74 发 / 0 STALL）或目标掌机（Miyoo，SDL_Renderer 软件光栅）。
- 若必须在 WSL 里听声音，可以把设备缓冲加大（例如 16384 样本 ≈ 370ms），代价是音效延迟明显上升，不适合玩——目前只作为临时手段，未做成正式开关。

## 6. 相关位置

- 音频设备打开与缓冲：`engine/src/audio/audio.cpp`（`Init()`，`want.samples = 8192`）
- 每秒诊断输出：同文件 `Tick()`（`[audio] ... STALL`）
- 帧预算日志：引擎 `HGE_FRAME_LOG=<ms>`，超预算帧打印 `[slow] f=... update=... render=... present=...`
