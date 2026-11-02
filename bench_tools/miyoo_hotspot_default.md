# Miyoo Mini Plus：WiFi 热点默认开启（调试模式）改造说明

目标：Onion 开机后自动打开 WiFi 热点，方便 PC / AI 助手连接调试。
只改 SD 卡上的 Onion 用户态脚本，不改内核 / DTB / bootloader，不刷机。

---

## 0. 根因（先看这个）

Onion 的 `runtime.sh` 在开机网络初始化时**主动删除**热点状态：

```sh
# /mnt/SDCARD/.tmp_update/runtime.sh
start_networking() {
    rm $sysdir/config/.hotspotState # dont start hotspot at boot
    touch /tmp/network_changed
    sync
    check_networking
}
```

所以即使你在 Tweaks 里把热点打开过，重启后这个标记也会被清掉，热点不会自动起来。

`check_networking` → `update_networking.sh check` → `check_hotspotstate`：
- `.hotspotState` 不存在：关闭热点；
- `.hotspotState` 存在且 hostapd 没跑：`start_hotspot &`；
- `.hotspotState` 存在但 WiFi 设置是关的：删掉标记并关闭热点。

因此“默认开启”的本质是：**让 `.hotspotState` 在开机网络检查时存在，并保证 WiFi 处于开启状态。**

---

## 1. 相关文件与现状

| 路径 | 作用 |
|---|---|
| `/mnt/SDCARD/.tmp_update/runtime.sh` | 开机流程；`start_networking` 启动时会删 `.hotspotState` |
| `/mnt/SDCARD/.tmp_update/script/network/update_networking.sh` | 网络服务总控；`check_hotspotstate` / `start_hotspot` |
| `/mnt/SDCARD/.tmp_update/script/network/hotspot_create.sh` | 独立拉起热点（hostapd + dnsmasq）；可手动执行 |
| `/mnt/SDCARD/.tmp_update/config/.hotspotState` | 热点开关状态标记 |
| `/mnt/SDCARD/.tmp_update/config/hostapd.conf` | 热点 SSID / 密码 |
| `/mnt/SDCARD/.tmp_update/config/dnsmasq.conf` | AP IP / DHCP 网段（常见 `192.168.100.100`，客户端 `101-200`） |

要点：
- 热点用 **`wlan1`** 作为 AP；开启期间会把 **`wlan0` down**（所以没有上游 WiFi / 互联网，这是预期行为）。
- 热点依赖 WiFi 硬件开启：`/customer/app/jsonval wifi` 必须为 `1`；`hotspot_create.sh` 里会 `axp_test wifion`。
- AP 的 SSID / 密码在 `hostapd.conf`；IP 段在 `dnsmasq.conf`。本文不写密码，直接看 `hostapd.conf`。

---

## 2. 方案 A：直接保留热点状态（最小改动）

1. 先在 **Tweaks → Network → WiFi** 确认 WiFi 已开（`/customer/app/jsonval wifi` 返回 `1`），
   并打开一次 **WiFi Hotspot**，确认 `/mnt/SDCARD/.tmp_update/config/.hotspotState` 存在。
2. 备份：
   ```sh
   cp /mnt/SDCARD/.tmp_update/runtime.sh /mnt/SDCARD/.tmp_update/runtime.sh.bench.bak
   ```
3. 编辑 `runtime.sh` 的 `start_networking`，把这一行注释或删掉：
   ```sh
   # rm $sysdir/config/.hotspotState # dont start hotspot at boot
   ```
4. `sync`，重启。
5. 开机后 `.hotspotState` 不再被清，`check_networking` → `update_networking.sh check` → `check_hotspotstate` 会自动 `start_hotspot`。
6. 验证（见第 7 节）。

注意：Onion 更新可能覆盖 `runtime.sh`，升级后需要重做；保留 `.bak`。

---

## 3. 方案 B：开机钩子 + 标记（推荐，抗 Onion 更新）

不改 Onion 的 `rm`，而是在开机网络初始化后重新设置热点状态；用一个标记文件控制“调试模式”是否启用。

1. 创建 `/mnt/SDCARD/.tmp_update/script/bench_hotspot_boot.sh`：
   ```sh
   #!/bin/sh
   sysdir=/mnt/SDCARD/.tmp_update
   sleep 2                              # 等网络初始化稳定
   touch "$sysdir/config/.hotspotState"
   "$sysdir/script/network/update_networking.sh" check
   ```
   然后 `chmod +x /mnt/SDCARD/.tmp_update/script/bench_hotspot_boot.sh`。
2. 在 `runtime.sh` 的 `start_networking` 函数里，`check_networking` 之后加一行：
   ```sh
   [ -f $sysdir/config/.bench_hotspot ] && $sysdir/script/bench_hotspot_boot.sh &
   ```
   （放在函数内只加一次；也可以放在开机调用 `start_networking` 之后。）
3. 启用调试模式：
   ```sh
   touch /mnt/SDCARD/.tmp_update/config/.bench_hotspot
   ```
   关闭调试模式：
   ```sh
   rm /mnt/SDCARD/.tmp_update/config/.bench_hotspot
   ```
4. 同样要保证 WiFi 是开的（Tweaks / 设置里 WiFi on，`jsonval wifi`=1）。WiFi 关闭时
   `check_hotspotstate` 会认为热点应关闭并删除 `.hotspotState`。
5. 重启，验证（见第 7 节）。
6. 回滚：恢复 `runtime.sh.bench.bak`，删除 `bench_hotspot_boot.sh` 和 `.bench_hotspot`，重启。

---

## 4. 方案 C：直接拉起 hotspot_create.sh（应急/手动）

```sh
/mnt/SDCARD/.tmp_update/script/network/hotspot_create.sh
```

它会自己 `check_wifi`（`axp_test wifion`、`wlan0 up`、`wpa_supplicant`）并启动 hostapd / dnsmasq。
适合手动救急验证；不建议作为默认开机方案（绕过了 Onion 的状态机，Tweaks 显示可能不一致）。

---

## 5. 热点参数 / 默认值

- SSID / 密码：`/mnt/SDCARD/.tmp_update/config/hostapd.conf`（本文不写密码）
- AP IP / 网段：`/mnt/SDCARD/.tmp_update/config/dnsmasq.conf` 的 `dhcp-range=`
  - 常见 AP IP：`192.168.100.100`
  - 客户端分配：`192.168.100.101 - 200`
- AP 接口：`wlan1`；热点期间 `wlan0` 被 down
- 查看：
  ```sh
  iw dev wlan1 info
  ifconfig wlan1
  cat /var/run/hostapd.pid
  ```

---

## 6. 稳定性补丁（建议确认）

AP 接口的省电会导致热点“用一会儿自动断”。确认热点启动路径在 `wlan1 up` / hostapd 之后有：

```sh
iw dev wlan1 set power_save off 2>/dev/null
```

需要检查两处（没有就补，先备份、`sh -n` 检查语法）：
- `update_networking.sh` 的 `start_hotspot`
- `hotspot_create.sh` 的 `start_hotspot`

本机此前已修；Onion 更新后需重新确认。

---

## 7. 验证清单

1. 重启，等 30–60 秒。
2. 设备上：
   ```sh
   pgrep hostapd
   pgrep dnsmasq
   ifconfig wlan1
   iw dev wlan1 info
   ```
3. PC 上：能看到热点 SSID，连接后用：
   ```sh
   ssh -o MACs=hmac-sha1 onion@192.168.100.100
   ```
4. 失败排查：
   - `/customer/app/jsonval wifi` 是否 `1`
   - `.hotspotState` 是否还在
   - `ps | grep -E 'hostapd|dnsmasq|wpa_supplicant|udhcpc'`
   - `dmesg | tail`
   - Onion 日志（如 `/mnt/SDCARD/.tmp_update/update_networking.log`）

---

## 8. 回滚

- 方案 A：恢复 `runtime.sh.bench.bak`，`rm .hotspotState`，重启。
- 方案 B：删 `.bench_hotspot` 和 `bench_hotspot_boot.sh`，恢复 `runtime.sh.bench.bak`，重启。
- 或者在 Tweaks 里把 WiFi Hotspot 关掉。

---

## 9. 注意

- 只改 SD 卡；不刷 kernel / DTB / bootloader，不变砖。
- 热点模式下 `wlan0` down、无互联网，属预期。
- Onion 更新会覆盖 `runtime.sh`；升级后重做方案 A，或使用方案 B 的标记方式。
- 本文不包含热点密码；密码见 `hostapd.conf`。

---

## 10. PC 侧连接脚本 + B 模式设备端脚本（已实现）

### 10.1 文件
- `miyoo_connect.sh`：PC 侧；连接宿主 WiFi 到 Miyoo AP，并检查 SSH 端口。
- `miyoo_bench_hotspot.sh`：PC 侧；通过 SSH 安装/开关/卸载 B 模式。
- `device/bench_hotspot_{boot,install,on,off,uninstall}.sh`：设备端脚本。
- `miyoo_net.conf`：本地配置（git-ignored）；模板 `miyoo_net.conf.example`。

### 10.2 配置
```sh
cp miyoo_net.conf.example miyoo_net.conf
# 编辑 WIFI_IFACE（Windows 无线网卡名，留空自动识别）、AP_PASS、MIYOO_PASS
```
- `AP_SSID` / `AP_PASS`：Miyoo 热点；`MIYOO_IP` / `SSH_PORT`：SSH 目标。
- `MAX_ATTEMPTS` / `RETRY_DELAY` / `COOLDOWN`：最大尝试、重试间隔、冷却。
- 密码来自设备 `hostapd.conf`；`miyoo_net.conf` 已在 `.gitignore`。

### 10.3 典型流程
```sh
# 设备已开热点
./miyoo_connect.sh            # PC 连 AP；成功后检查 192.168.100.100:22
./miyoo_connect.sh --check    # 只检查是否可达

# 首次安装/启用 B 模式（设备已可达）
./miyoo_bench_hotspot.sh install
./miyoo_bench_hotspot.sh status

# 重启 Miyoo 验证开机自动热点；PC 掉线就再 connect
./miyoo_connect.sh
ssh -o MACs=hmac-sha1 onion@192.168.100.100
```
- `--force`：忽略冷却；`--check`：只检查。
- 连接会切走 PC 的 WiFi，PC 暂时无互联网；脚本不自动恢复原 WiFi。
- `miyoo_bench_hotspot.sh` 的 `on/off/uninstall` 用于启停/恢复。

### 10.4 失败排查
- `./miyoo_connect.sh --check` 失败：热点未开、PC 网卡、AP 名/IP 不匹配。
- `./miyoo_bench_hotspot.sh status`：`bench marker=no` 未启用；`runtime hook=0` 未安装；`wifi setting=0` 先在 Onion 里开 WiFi。
- 设备端 `runtime.sh.bench.bak` 供 `uninstall` 恢复。

### 10.5 退出游戏后恢复热点
`package/App/Bubble/launch.sh` 和 `launch_standalone.sh` 在游戏退出后会后台重跑
`/mnt/SDCARD/.tmp_update/script/bench_hotspot_boot.sh`，重新拉起 hostapd + dnsmasq。
前提：已执行 `./miyoo_bench_hotspot.sh install`，且 `.tmp_update/config/.bench_hotspot`
存在。日志见设备 `/mnt/SDCARD/.tmp_update/bench_hotspot_boot.log`。
