# Gateway 运行时配置优化（crontab / 时区 / 固化文件 / 命令工具 / VPN）

## Context

HLK-7688A gateway 固件（24.10）已能启动并跑通 gateway app（Phase 1/3 验证），但有几项运行时配置缺失需补齐：定时任务（守护 + NTP）、时区（当前 UTC）、gateway 配置文件未固化进固件、缺少常用诊断命令（file/lsusb/lsblk）、gateway 源码 VPN 启动方式未更新为新脚本 `gw-vpn`。

## 范围

**本轮含**（5 项）：
1. crontab 三条任务（第 1 条 monitor 注释待移植）
2. 时区 UTC → CST（Asia/Shanghai）
3. 固化 `/home/config/`（4 证书/cfg）+ `/opt/data/`（8 JSON）进固件
4. `file` / `lsusb` / `lsblk` 命令支持
5. gateway 源码 VPN 启动改 `gw-vpn start`

**本轮不含**：
- `monitor` C 二进制 24.10 移植（当前 uClibc 二进制 musl 跑不了，crontab 第 1 条注释，单独排期）
- sysntpd 配置（固件默认 disabled，与 crontab ntpd 不冲突）

## 约束

- 改动分两个独立 git 仓库，分别 commit
- gateway 源码改 worktree `/home/otn/bump-detect/worktree/openwrt-24.10-port`（分支 `openwrt-24.10-port`），**不可直接改主仓库 main**
- `/opt/data/params.json` 含设备 SN（GW2023052801）——固化为默认值，设备实际 SN 由 gateway app 运行时或部署时更新（确认不冲突后固化）

## 设计

### 域 1：固件仓库（/home/chris/workspace/openwrt-24.10）

#### ① `99-hlk-7688a-setup` uci-defaults 扩展（时区 + crontab + crond）

文件：`target/linux/ramips/base-files/etc/uci-defaults/99-hlk-7688a-setup`

在现有脚本末尾（`exit 0` 前）追加：

```sh
# --- 时区 CST（UTC+8，Asia/Shanghai）---
uci set system.@system[0].timezone='CST-8'
uci set system.@system[0].zonename='Asia/Shanghai'
uci commit system

# --- crontab：gateway app 守护 + NTP 同步 ---
# sysntpd 固件默认 disabled，不冲突；busybox crond 默认 disabled 需手动 enable
mkdir -p /etc/crontabs
cat > /etc/crontabs/root << 'EOF'
# */2 * * * * monitor  # TODO: monitor 未移植 24.10(uClibc→musl)，待移植后取消注释
* * * * * /home/bump_detect/dec.bin
*/10 * * * * ntpd -gq -p ntp.aliyun.com
EOF
/etc/init.d/cron enable
/etc/init.d/cron start
```

crontab 三条说明：
- `dec.bin`（shc 编译的 monitor.sh）——每分钟检查 gateway app `/tmp/app/app` 进程，未跑则解密 `app_enc_tar` 重启
- `ntpd -gq -p ntp.aliyun.com`——每 10 分钟同步阿里云 NTP（`-g` 允许大步进，`-q` 同步后退出）
- `monitor` 注释——752KB C 监控程序，uClibc 二进制 24.10 跑不了

#### ② gateway-deploy 包扩展（固化配置文件）

文件：`package/utils/gateway-deploy/Makefile`

源文件（从 `/tmp/app/gateway/` 拷入包目录）：
- `package/utils/gateway-deploy/files/home/config/`（emqx.key / emqx.pem / ca.pem / format.cfg）
- `package/utils/gateway-deploy/files/opt/data/`（params.json / sys_params.json / module_parms.json / rtsp.json / sub_dev_list.json / sound_arrier_params.json / lora_params.json / product_params.json）

Makefile `Package/gateway-deploy/install` 段扩展：

```makefile
define Package/gateway-deploy/install
	$(INSTALL_DIR) $(1)/home/bump_detect $(1)/home/config $(1)/opt/data
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dec.bin $(1)/home/bump_detect/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/app_enc_tar $(1)/home/bump_detect/
	$(INSTALL_DATA) $(CURDIR)/files/home/config/* $(1)/home/config/
	$(INSTALL_DATA) $(CURDIR)/files/opt/data/* $(1)/opt/data/
endef
```

> `$(CURDIR)` 在 install 阶段指向包目录（`package/utils/gateway-deploy/`），USE_SOURCE_DIR 只改 PKG_BUILD_DIR 不影响 CURDIR。

#### ③ DEVICE_PACKAGES 加命令工具

文件：`target/linux/ramips/image/mt76x8.mk`（`Device/hilink_hlk-7688a` 的 `DEVICE_PACKAGES`）

追加：`file usbutils lsblk`

- `file`（feeds/packages/libs/file）——文件类型识别
- `usbutils`（feeds/packages/utils/usbutils）——提供 `lsusb`
- `lsblk`（util-linux 子包，依赖 libblkid/libmount/libsmartcols/libncurses 自动拉）——块设备列表

#### ④ `.config` 翻 y

DEVICE_PACKAGES 加包后 `make defconfig` 应自动翻 y。若残留 not-set（LuCI 教训：feeds 残留 not-set 不翻），手动设：
```
CONFIG_PACKAGE_file=y
CONFIG_PACKAGE_usbutils=y
CONFIG_PACKAGE_lsblk=y
```

### 域 2：gateway 源码 worktree（/home/otn/bump-detect/worktree/openwrt-24.10-port）

分支：`openwrt-24.10-port`

#### VPN 启动改 `gw-vpn start`（3 处开启/重启）

精确范围（`gateway/huaweicloud-sdk20210107/src/`）：

| 文件 | 行 | 语义 | 改动 |
|------|-----|------|------|
| `iota/conn/mqtt_process.cpp` | 151-152 | openvpn_switch ON（云端命令开 VPN） | `system("gw-vpn start")` |
| `socket/socket_server.cpp` | 298-299 | 端口冲突后重启 openvpn | `system("gw-vpn start")` |
| `socket/socket_server.cpp` | 1330-1331 | 同上（另一端口检测路径） | `system("gw-vpn start")` |

每处替换 2 行（`killall openvpn` + `openvpn ... &`）为 1 行 `system("gw-vpn start")`。

**不改**（关闭/删除语义，保持原 `killall openvpn`）：
- `mqtt_process.cpp:159`（openvpn_switch OFF）
- `mqtt_process.cpp:168`（openvpn_del，停服务后删文件）

`gw-vpn start`（`/usr/bin/gw-vpn`）已实现：从 `/opt/data/params.json` 提 SN → 生成凭证 → killall openvpn → 启 openvpn（含 legacy provider）。替代 gateway 源码里的手写 killall+openvpn 两行，统一走脚本。

## 验证

### 固件仓库
1. `make` 构建 → 无报错
2. 镜像含新包：`grep -E 'file|usbutils|lsblk' bin/targets/ramips/mt76x8/*.manifest`
3. rootfs 含固化文件：build_dir rootfs 有 `/home/config/ca.pem`、`/opt/data/params.json` 等
4. 烧固件到设备：
   - `date` 显示 CST
   - `crontab -l` 三条（monitor 注释）
   - `lsusb` / `lsblk` / `file` 可执行
   - `/home/config/` 和 `/opt/data/` 文件齐全

### gateway 源码
5. worktree 内 `system("gw-vpn start")` 替换 3 处成功（grep 确认无残留 `openvpn /home/config/client_2.ovpn`）
6. 重编 gateway app（build-gateway.sh）→ 部署 → 云端下发 openvpn_switch ON → `pgrep openvpn` 有进程 + `/tmp/openvpn.log` 有连接日志

## 风险

| 风险 | 等级 | 缓解 |
|------|------|------|
| params.json 含 SN 固化后多设备 SN 冲突 | 低 | 该 SN 为默认值；设备实际 SN 由 gateway app/部署更新；确认后固化 |
| `.config` feeds 残留 not-set 不翻 y | 低 | defconfig 后 grep 确认，手动翻（LuCI 教训） |
| gw-vpn start 返回值与原 system() 返回值语义差异 | 低 | mqtt_process ON 分支检查返回值，gw-vpn start 成功返回 0；socket_server 不检查返回值 |
| gateway app_enc_tar/dec.bin 重编后需重新部署 | 低 | VPN 改动在 gateway 源码，需 build-gateway.sh 重编生成新 dec.bin/app_enc_tar |
