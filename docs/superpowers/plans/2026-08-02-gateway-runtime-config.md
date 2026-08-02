# Gateway 运行时配置优化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox (`- [ ]`) for tracking.

**Goal:** 为 HLK-7688A 固件补齐 gateway 运行时配置：crontab 守护+NTP、CST 时区、固化配置文件、诊断命令工具，并更新 gateway 源码 VPN 启动方式。

**Architecture:** 改动分两个独立 git 仓库域。固件仓库（openwrt-24.10）：uci-defaults 扩展时区+crontab、gateway-deploy 包扩展固化配置文件、DEVICE_PACKAGES 加命令工具。gateway 源码 worktree：3 处 VPN 启动改 `gw-vpn start`。本项目无测试框架，验证 = 构建+grep+设备检查。

**Tech Stack:** OpenWrt 24.10 构建系统（make/KernelPackage/uci-defaults）、busybox crond/ntpd、C++ gateway app。

## Global Constraints

- gateway 源码改动在 worktree `/home/otn/bump-detect/worktree/openwrt-24.10-port`（分支 `openwrt-24.10-port`），**不可改主仓库 main**
- 固件仓库在 `/home/chris/workspace/openwrt-24.10`（分支 `wip/24.10-sd-debug`）
- 固化配置文件源在 `/tmp/app/gateway/`（当前存在，拷入包目录后不再依赖）
- commit message 末尾加 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
- 全量内核编译 >10min 超后台 timeout；package 编译快，只改 base-files/mt76x8.mk/.config 无需重编内核

---

### Task 1: gateway-deploy 包固化配置文件（固件仓库）

**Files:**
- Create: `package/utils/gateway-deploy/files/home/config/{ca.pem,emqx.key,emqx.pem,format.cfg}`
- Create: `package/utils/gateway-deploy/files/opt/data/{params.json,sys_params.json,...}`（8 个 JSON）
- Modify: `package/utils/gateway-deploy/Makefile`（install 段）

**Interfaces:**
- Produces: 固件 rootfs 含 `/home/config/`（4 证书/cfg）+ `/opt/data/`（8 JSON），gateway app 和 gw-vpn 启动时可读

- [ ] **Step 1: 拷贝固化文件到包目录**

```bash
cd /home/chris/workspace/openwrt-24.10
mkdir -p package/utils/gateway-deploy/files/home/config
mkdir -p package/utils/gateway-deploy/files/opt/data
cp /tmp/app/gateway/home/config/* package/utils/gateway-deploy/files/home/config/
cp /tmp/app/gateway/opt/data/* package/utils/gateway-deploy/files/opt/data/
```

- [ ] **Step 2: 验证文件拷贝完整**

```bash
ls package/utils/gateway-deploy/files/home/config/  # 应有 4 个文件
ls package/utils/gateway-deploy/files/opt/data/     # 应有 8 个 JSON
```
Expected: home/config/ 有 ca.pem/emqx.key/emqx.pem/format.cfg；opt/data/ 有 8 个 .json

- [ ] **Step 3: 修改 gateway-deploy Makefile install 段**

将 `Package/gateway-deploy/install` 整段替换为：

```makefile
define Package/gateway-deploy/install
	$(INSTALL_DIR) $(1)/home/bump_detect $(1)/home/config $(1)/opt/data
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dec.bin $(1)/home/bump_detect/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/app_enc_tar $(1)/home/bump_detect/
	$(INSTALL_DATA) $(CURDIR)/files/home/config/* $(1)/home/config/
	$(INSTALL_DATA) $(CURDIR)/files/opt/data/* $(1)/opt/data/
endef
```

- [ ] **Step 4: 验证 Makefile 改动**

```bash
grep -A8 'define Package/gateway-deploy/install' package/utils/gateway-deploy/Makefile
```
Expected: install 段含 home/config 和 opt/data 的 INSTALL_DATA 行

- [ ] **Step 5: Commit**

```bash
git add package/utils/gateway-deploy/files/ package/utils/gateway-deploy/Makefile
git commit -m "feat(gateway-deploy): 固化 /home/config + /opt/data 配置文件进固件

gateway app（证书/emqx）和 gw-vpn（params.json 提 SN）启动依赖这些文件，
之前未固化需 scp，现烧固件即自带。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: uci-defaults 时区 + crontab（固件仓库）

**Files:**
- Modify: `target/linux/ramips/base-files/etc/uci-defaults/99-hlk-7688a-setup`（末尾 `exit 0` 前追加）

**Interfaces:**
- Produces: 设备首启后时区=CST，crond enabled，`/etc/crontabs/root` 含 dec.bin 守护 + ntpd 同步

- [ ] **Step 1: 在 99-hlk-7688a-setup 末尾（`exit 0` 前）追加配置**

在 `mkdir -p /mnt/mmcblk0p1` 行之后、`exit 0` 之前，插入：

```sh
# --- 时区 CST（UTC+8，Asia/Shanghai）---
uci set system.@system[0].timezone='CST-8'
uci set system.@system[0].zonename='Asia/Shanghai'
uci commit system

# --- crontab：gateway app 守护（dec.bin 每分钟）+ NTP 同步（每 10 分钟阿里云）---
# sysntpd 固件默认 disabled 不冲突；busybox crond 默认 disabled 需手动 enable
# monitor（C 二进制）注释——未移植 24.10(uClibc→musl)，待移植后取消注释
mkdir -p /etc/crontabs
cat > /etc/crontabs/root << 'CRONEOF'
# */2 * * * * monitor  # TODO: monitor 未移植 24.10，待移植后取消注释
* * * * * /home/bump_detect/dec.bin
*/10 * * * * ntpd -gq -p ntp.aliyun.com
CRONEOF
/etc/init.d/cron enable
/etc/init.d/cron start
```

- [ ] **Step 2: 验证追加内容**

```bash
tail -20 target/linux/ramips/base-files/etc/uci-defaults/99-hlk-7688a-setup
```
Expected: 末尾含 timezone CST-8、crontabs/root heredoc、cron enable/start，最后 exit 0

- [ ] **Step 3: Commit**

```bash
git add target/linux/ramips/base-files/etc/uci-defaults/99-hlk-7688a-setup
git commit -m "feat(hlk-7688a): 时区 CST + crontab（dec.bin 守护 + NTP 阿里云）

时区从默认 UTC 改 Asia/Shanghai(CST-8)；crond 启用，crontab 含：
- dec.bin 每分钟守护 gateway app 进程
- ntpd 每 10 分钟同步 ntp.aliyun.com
monitor C 二进制待 24.10 移植后加入。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: DEVICE_PACKAGES 加诊断命令工具（固件仓库）

**Files:**
- Modify: `target/linux/ramips/image/mt76x8.mk:276-281`（HLK-7688A DEVICE_PACKAGES）
- Modify: `.config`（make defconfig 后验证/手动翻 y）

**Interfaces:**
- Produces: 固件含 `file`（文件类型识别）、`lsusb`（USB 设备列表）、`lsblk`（块设备列表）命令

- [ ] **Step 1: mt76x8.mk DEVICE_PACKAGES 追加命令工具**

在 `DEVICE_PACKAGES :=` 行末尾的 `kmod-rt_rdm gateway-deploy luci openvpn-openssl libopenssl-legacy` 后追加 ` file usbutils lsblk`。

改后该行应为：
```makefile
  DEVICE_PACKAGES := kmod-usb2 kmod-usb-ohci kmod-usb-ledtrig-usbport kmod-sdhci-mt7620 \
    kmod-mt76 wpad-basic-mbedtls \
    kmod-usb-net-cdc-ether kmod-usb-serial kmod-usb-serial-option \
    kmod-fs-f2fs f2fs-tools block-mount kmod-nls-cp437 kmod-nls-iso8859-1 \
    libpaho-mqtt-c libffmpeg-full libcurl libopenssl openssl-util zlib libstdcpp libatomic \
    kmod-rt_rdm gateway-deploy luci openvpn-openssl libopenssl-legacy file usbutils lsblk
```

- [ ] **Step 2: make defconfig 让 .config 同步**

```bash
make defconfig
```

- [ ] **Step 3: 验证 .config 翻 y（LuCI 教训：feeds 残留 not-set 可能不翻）**

```bash
grep -E 'CONFIG_PACKAGE_(file|usbutils|lsblk)=' .config
```
Expected: 三行都 = y。若有 `is not set`，手动设：
```bash
sed -i 's/# CONFIG_PACKAGE_file is not set/CONFIG_PACKAGE_file=y/' .config
sed -i 's/# CONFIG_PACKAGE_usbutils is not set/CONFIG_PACKAGE_usbutils=y/' .config
sed -i 's/# CONFIG_PACKAGE_lsblk is not set/CONFIG_PACKAGE_lsblk=y/' .config
make defconfig
```

- [ ] **Step 4: Commit**

```bash
git add target/linux/ramips/image/mt76x8.mk .config
git commit -m "feat(hlk-7688a): DEVICE_PACKAGES 加 file/lsusb/lsblk 诊断命令

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: 构建固件 + 验证产物（固件仓库）

**Files:**
- 无新文件；验证 Task 1-3 的构建产物

- [ ] **Step 1: 构建（package + image，不改内核无需 target clean）**

```bash
make package/utils/gateway-deploy/compile V=s 2>&1 | tail -5
make package/index V=s 2>&1 | tail -3
```
> 若 base-files 改动需要刷入 rootfs，全量 `make` 或 `make target/linux/install`。base-files 属 target/linux，最可靠用 `make`。

```bash
make 2>&1 | tail -10
```
Expected: 无报错，生成 bin/targets/ramips/mt76x8/*-squashfs-sysupgrade.bin

- [ ] **Step 2: 验证 manifest 含新命令包**

```bash
grep -E '\b(file|usbutils|lsblk)\b' bin/targets/ramips/mt76x8/openwrt-ramips-mt76x8-hilink_hlk-7688a.manifest
```
Expected: 三行（file / usbutils / lsblk 各一）

- [ ] **Step 3: 验证 rootfs 含固化文件**

```bash
ls build_dir/target-mipsel_24kc_musl/root.orig-ramips/home/config/ 2>/dev/null
ls build_dir/target-mipsel_24kc_musl/root.orig-ramips/opt/data/ 2>/dev/null
```
Expected: home/config/ 有 4 文件；opt/data/ 有 8 JSON

- [ ] **Step 4: 验证 uci-defaults 含时区+crontab**

```bash
grep -c 'timezone.*CST\|crontabs\|cron.*enable' build_dir/target-mipsel_24kc_musl/root.orig-ramips/etc/uci-defaults/99-hlk-7688a-setup 2>/dev/null
```
Expected: ≥3（timezone + crontabs + cron enable 各匹配）

---

### Task 5: gateway 源码 VPN 改 gw-vpn start（gateway worktree）

**Files:**
- Modify: `gateway/huaweicloud-sdk20210107/src/iota/conn/mqtt_process.cpp:151-152`（worktree）
- Modify: `gateway/huaweicloud-sdk20210107/src/socket/socket_server.cpp:298-299`（worktree）
- Modify: `gateway/huaweicloud-sdk20210107/src/socket/socket_server.cpp:1330-1331`（worktree）

**Interfaces:**
- Consumes: `/usr/bin/gw-vpn`（固件已部署，接受 start/stop，内部 killall+openvpn+legacy provider）
- Produces: gateway app 云端命令 openvpn_switch ON / 端口冲突重启 VPN 时走 gw-vpn 脚本

**不改**（关闭/删除语义保持原 `killall openvpn`）：
- `mqtt_process.cpp:159`（openvpn_switch OFF）
- `mqtt_process.cpp:168`（openvpn_del）

- [ ] **Step 1: 进入 worktree 确认分支**

```bash
cd /home/otn/bump-detect/worktree/openwrt-24.10-port
git branch --show-current  # 应为 openwrt-24.10-port
git status --short         # 应 clean
```

- [ ] **Step 2: 改 mqtt_process.cpp（openvpn_switch ON 分支）**

文件 `gateway/huaweicloud-sdk20210107/src/iota/conn/mqtt_process.cpp`，约 151-152 行。

旧：
```cpp
					system("killall openvpn");
					if(system("openvpn /home/config/client_2.ovpn &") == 0)
```

新：
```cpp
					if(system("gw-vpn start") == 0)
```

- [ ] **Step 3: 改 socket_server.cpp 第 1 处（端口冲突重启）**

文件 `gateway/huaweicloud-sdk20210107/src/socket/socket_server.cpp`，约 298-299 行。

旧：
```cpp
					system("killall openvpn");
					system("openvpn /home/config/client_2.ovpn &");
```

新：
```cpp
					system("gw-vpn start");
```

- [ ] **Step 4: 改 socket_server.cpp 第 2 处（另一端口检测路径，约 1330-1331 行）**

同 Step 3 的旧→新替换（内容完全相同）。

- [ ] **Step 5: 验证无残留旧调用**

```bash
grep -rn 'openvpn /home/config/client_2.ovpn' gateway/huaweicloud-sdk20210107/src/
```
Expected: 无输出（3 处全改完）。`killall openvpn` 应仅剩 OFF(159) 和 del(168) 两处。

```bash
grep -rn 'gw-vpn start' gateway/huaweicloud-sdk20210107/src/
```
Expected: 3 行（mqtt_process 1 + socket_server 2）

- [ ] **Step 6: Commit（worktree）**

```bash
git add gateway/huaweicloud-sdk20210107/src/iota/conn/mqtt_process.cpp gateway/huaweicloud-sdk20210107/src/socket/socket_server.cpp
git commit -m "fix: VPN 启动改用 gw-vpn start 脚本（3 处开启/重启）

原 system(\"openvpn /home/config/client_2.ovpn &\") 直接调 openvpn，
现统一走 /usr/bin/gw-vpn start（从 params.json 提 SN + 生成凭证 +
legacy provider 启 BF-CBC）。关闭/删除(openvpn_del)的 killall 不变。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## 设备验证清单（构建完成后用户操作）

烧固件到设备后逐项确认：
1. `date` → 显示 CST（非 UTC）
2. `crontab -l` → dec.bin + ntpd 两条（monitor 注释）
3. `lsusb` / `lsblk` / `file --version` → 可执行
4. `ls /home/config/` → 4 文件；`ls /opt/data/` → 8 JSON
5. gateway app 重编部署后，云端下发 openvpn_switch ON → `pgrep openvpn` 有进程
