# 项目约定

给在这个仓库里干活的 agent 和人。约定比工具可靠，但机器能兜住的就别靠记性。

## 提交与推送

1. **提交前必须过一遍清单**：先 `git status --short`，再 `git diff --cached --stat`。
   不要盲用 `git add -A`；一旦用了，就必须逐个文件读 staging 列表。
   （2026-11-08 就是这么把 `build-bench*/` 几千个文件塞进提交的。）
2. **构建产物、日志、临时目录永不入库**：`build-*/`、`*.o`、`*.a`、`*.log`、
   `*.ppm`、`__pycache__/`，以及任何"临时装配目录"（例如把 App 目录拷到 `/tmp`
   改完再拷回来的副本）。`.gitignore` 里的 `/build-*/` 是兜底，不是许可。
3. **推送是最后一步，不是提交的同义词**：先本地提交，确认 `git log --oneline -3`
   与 `git status` 干净，再 `git push`。历史改写（`--force`）只在「刚推上去、
   确认没人拉过」时用，且改完要立刻把共享裸仓库和 Windows 镜像一起 reset。
4. **一次提交一件事**：功能改动里混进构建产物，比晚提交十分钟更糟。

## 目录归属

- `engine/` 引擎（可独立成仓）；`games/<name>/` 游戏（资产、工具、测试、部署包各归其位）。
- 引擎里不允许出现任何游戏的名字（`grep -ri <游戏名> engine/` 必须为 0），
  `engine/tests/package_consumer` 是这条边界的机器守卫。

## 验证基线（改动前后都要跑）

```bash
ctest --test-dir build-native            # 5/5
./build-native/bubble_level1 --sim-only 5   # checksum=0x582b06875d67e228
```
