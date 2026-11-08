# Miyoo 调试工具

这个目录是 **PC 侧** 和 **Miyoo 设备侧** 的无线调试工具，配合
`package/DEPLOY.md` 使用。

## 文件

| 路径 | 位置 | 说明 |
|---|---|---|
| `miyoo_connect.sh` | PC | 连接 Miyoo 热点并检查 SSH 端口 |
| `miyoo_bench_hotspot.sh` | PC | 通过 SSH 安装/开关/卸载 B 模式热点 |
| `miyoo_net.conf.example` | PC | 本地配置模板；复制为 `miyoo_net.conf` 后填写密码 |
| `miyoo_hotspot_default.md` | — | 热点原理、排障、回滚的完整说明 |
| `device/bench_hotspot_boot.sh` | Miyoo | 拉起 hostapd + dnsmasq（开机/退出游戏后） |
| `device/bench_hotspot_install.sh` | Miyoo | 给 Onion `runtime.sh` 加钩子，备份 `.bench.bak` |
| `device/bench_hotspot_on.sh` | Miyoo | 创建 `.bench_hotspot` 标记 |
| `device/bench_hotspot_off.sh` | Miyoo | 删除 `.bench_hotspot` 标记 |
| `device/bench_hotspot_uninstall.sh` | Miyoo | 恢复 `runtime.sh` 并清理 |

`miyoo_net.conf`、`.miyoo_connect.state`、锁目录和日志都在 `.gitignore` 中，
**不要把真实密码提交到仓库**。

## 首次使用

```bash
cd hge-sdl/bench_tools
cp miyoo_net.conf.example miyoo_net.conf
# 编辑 AP_SSID / AP_PASS / MIYOO_IP / MIYOO_USER / MIYOO_PASS / WIFI_IFACE

./miyoo_connect.sh --force
./miyoo_bench_hotspot.sh install
./miyoo_bench_hotspot.sh status
```

设备端脚本会被安装到 `/mnt/SDCARD/.tmp_update/script/`，并在
`/mnt/SDCARD/.tmp_update/runtime.sh` 的 `start_networking()` 里加一行钩子。
安装脚本是幂等的，重复执行不会重复插入。

## 游戏退出后的热点恢复

`package/App/Bubble/launch.sh` 和 `launch_standalone.sh` 在游戏退出后会后台执行：

```sh
/mnt/SDCARD/.tmp_update/script/bench_hotspot_boot.sh
```

前提是设备上已安装这个脚本、`.tmp_update/config/.bench_hotspot` 存在，并且
Onion 的 WiFi 开关为开启状态。这样不需要每次退出游戏都重启机器。

## 常见问题

- **过一会儿 SSH 就连不上（Connection timed out）**：热点还活着，是 **Windows 把隐藏 AP
  的关联丢了**。`netsh wlan show interfaces` 会显示「已断开连接」。重跑一次即可，约 20 秒：
  ```bash
  ./miyoo_connect.sh --force
  ```
  注意设备的 AP 是**隐藏 SSID**，`netsh wlan show networks` **扫不到它** —— 扫不到不代表
  热点没起来，直接按 profile 连就行。
- **部署被拒绝，说目录不是本脚本建的**：目标目录已存在且没有 `.hge_deploy` 标记
  （例如手动放进去的 App）。先备份、让它腾位再部署：
  ```bash
  ssh miyoo 'cd /mnt/SDCARD/App && mv Bubble Bubble.bak'
  ./miyoo_deploy_app.sh /path/to/App/Bubble Bubble
  ```
- **Windows 一直停在“正在验证”**：旧 WLAN profile 可能缓存了错误密码。删除后重连：
  ```powershell
  netsh.exe wlan delete profile name="MiyooMiniAP"
  ```
  然后重新运行 `./miyoo_connect.sh --force`。
- **热点没起来**：在设备上检查 `hostapd`、`dnsmasq`、`wlan1`，并查看
  `/mnt/SDCARD/.tmp_update/bench_hotspot_boot.log`。
- **Onion 更新后**：`runtime.sh` 可能被覆盖，重新执行
  `./miyoo_bench_hotspot.sh install`。
