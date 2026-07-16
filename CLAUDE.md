# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 仓库身份

OpenWrt **24.10**（内核 6.6.73，`r28427-6df0e3d02a`）的本地工作副本，origin 指向上游 `openwrt/openwrt`（经 `ghfast.top` 镜像）。当前仅一条本地分支 `wip/24.10-sd-debug`，在上游 v24.10.0 之上加了**一个定制 commit**，专门为 **HLK-7688A**（Hi-Link，MT7688A/MT7628 SoC，ramips/mt76x8，MIPS `mipsel_24kc`）调试 SD 卡回归。

- `.config` 已锁死单目标：`CONFIG_TARGET_ramips_mt76x8_DEVICE_hilink_hlk-7688a=y`，`INITRAMFS`（LZMA）+ `SQUASHFS`。
- 没有 `tests/` 目录；"测试" = 在真机上 bootm initramfs 看 `dmesg`。

## 构建命令

```bash
./scripts/feeds update -a && ./scripts/feeds install -a   # 拉/装 feeds（feeds.conf 固定到特定 commit）
make menuconfig                                           # .config 已配好，通常无需重选
make                                                      # 全量构建（首次含 toolchain，很久）
```

精准重建（日常调试最常用，省去全量）：

```bash
make target/linux/clean && make target/linux/compile      # 只重建内核（改了 patches-6.6/ 后）
make package/kernel/linux/compile                        # 重建 in-tree 内核模块包
make package/index                                        # 刷新 opkg 索引
```

清理三档（按破坏力递增）：`make clean`（产物）→ `make targetclean`（+toolchain/build_dir）→ `make dirclean`（几乎全清，含 toolchain，重建 ~1h）。

产物在 `bin/targets/ramips/mt76x8/`：
- `*-initramfs-kernel.bin` — **调试用**，tftp 到 RAM 跑，不写 flash。
- `*-squashfs-sysupgrade.bin` — 持久化用。

## 内核 patch 工作流（本项目核心，易踩坑）

内核 patch 放在 `target/linux/ramips/patches-6.6/`（应用顺序按文件名数字）。patch 应用后内核源码在 `build_dir/target-mipsel_24kc_musl/linux-ramips_mt76x8/linux-6.6.73/`。

```bash
make target/linux/refresh        # 用 quilt 刷新已 apply 的 patch（交互式改 patch 内容）
```

> **⚠ 陷阱（EXPLORATION.md 教训 #7）**：调试内核**只能**改 `patches-6.6/` 里的正式 patch 然后 clean compile，**绝不能直接编辑 `build_dir/` 下的源码**——compile/install 会覆盖改动，`modules_install` 的 `.ko` 拷贝还有缓存陷阱。改了 `build_dir` 后想看到效果，必须先 clean。

## 架构大图

构建流水线依赖顺序（见顶层 `Makefile`）：**tools（主机工具）→ toolchain（交叉 gcc 13.3.0 + musl）→ target（内核）→ package（用户态）→ image（打包）**，每步靠 `stamp-*` 文件追踪。

| 目录 | 角色 |
|------|------|
| `target/linux/ramips/` | 内核、DTS（`dts/mt7628an_hilink_hlk-7688a.dts`）、image 规则（`image/mt76x8.mk`）、内核 patch（`patches-6.6/`）、board base-files |
| `toolchain/` + `tools/` | 交叉工具链构建定义 |
| `package/` | 所有用户态包 Makefile（`kernel/` 下是内核模块） |
| `feeds/` | 外部包源（packages/luci/routing/telephony），`feeds.conf` 经 `ghfast.top` 国内加速 |
| `include/` + `rules.mk` | 构建系统核心 mk 逻辑 |
| `build_dir/` | 编译中间产物 / `staging_dir/` sysroot / `bin/` 最终固件 |

设备定义：DTS 决定硬件，`target/linux/ramips/image/mt76x8.mk` 的 `Device/hilink_hlk-7688a` 决定镜像格式。

## HLK-7688A SD 调试上下文（项目独有，务必先读）

**先读 `EXPLORATION.md`**——它记录了 SD 回归探索全过程、证据链和教训。要点：

- **根因**：上游 mainline `mtk-sd` 驱动对 MT7628 SDXC 的 CMD 通信回归（OpenWrt issue #21879/#17411/#17364/#14042），卡零枚举，**非配置问题**。配置层（pinctrl/ios/电压时钟）经验证全对。
- **本分支自定义内核 patch**（`patches-6.6/`）：
  - `831-01/02/03-*` — 社区 Fix#4（Shiji Yang 2025-06：tuning / 禁 CMD23 / PATCH_BIT 默认值），只修传输层，不够不着 CMD 通信。
  - `999-mmc-msdc-debug.patch` — 临时 `MSDC-DBG` printk（CMD/IRQ），**compile 产物带，但未确认进 initramfs image**。
- **DTS 调整**（`mt7628an_hilink_hlk-7688a.dts` 的 `&sdhci`）：`no-1-8-v` + `vqmmc-supply = <&mmc_reg_3v3>` + `broken-cd`。
- **🎯 2026-07-12 突破（状态更新）**：找到 24.10 可行解法 —— 切换到 OpenWrt 内置的私有 mtk-mmc 驱动（`kmod-sdhci-mt7620`，`target/linux/ramips/modules.mk` 已定义，`CONFLICTS:=kmod-mmc-mtk`），替代坏的 mainline mtk-sd。私有驱动源码早由 `patches-6.6/830` apply，只是 kmod 包没启用。已验证 6.6 编译（`mtk_sd.ko` vermagic 匹配）+ defconfig 同步（`.config`: `MTK_MMC=m`/`MMC_MTK off`），待设备验证 SD 枚举。详见 `EXPLORATION.md` 突破章节。改动：`mt76x8.mk`（HLK-7688A `DEVICE_PACKAGES += kmod-sdhci-mt7620`）+ DTS（`&sdhci` `compatible = "ralink,mt7620-sdhci"` override）。
- **坑**：① 别手动设 `CONFIG_MTK_MMC`（漏 `MTK_AEE_KDUMP` 子选项 → syncconfig 失败），走 kmod 包机制；② 别只改 config-6.6 的 `# CONFIG_MMC_MTK is not set`（被 `modules.mk` 的 kmod-mmc-mtk 包覆盖），要改 `.config` 的 `CONFIG_PACKAGE_kmod-mmc-mtk` 或 device profile；③ mainline `mtk-sd.ko` 与私有 `mtk_sd.ko` 模块名都是 `mtk_sd`，不能共存。

### initramfs 调试流程（U-Boot 1.1.3，关键陷阱）

- `tftpboot` **必须加载到高地址** `0x82000000`（≥`0x81000000`）再 `bootm`。加载到 `0x80100000` 会导致 `LZMA ERROR 1`——U-Boot 1.1.3 无重叠保护，bootm 原地解压会覆盖未读完的压缩源（教训 #1）。
- `Checksum OK` = 镜像完整；`LZMA ERROR 1` 是解压环节问题，先查加载地址，别怀疑固件。
- **绝不能用 U-Boot Option 2/5 烧 flash**：`raspi_erase` 长循环不喂看门狗，稳定在第 13 个 64KB block 复位（教训 #5）。持久化走**系统内 `mtd write`**（Linux 喂狗正常）。

### 固件更新（持久化烧写）

产物两种：`*-initramfs-kernel.bin`（RAM 调试/救砖，**不写 flash**）、`*-squashfs-sysupgrade.bin`（持久化，写 firmware 分区）。HLK-7688A 已是 OpenWrt（非原厂固件），**一律用 sysupgrade image 烧 flash**。

**A) 日常更新（设备能正常启动）— sysupgrade（首选）**：
```bash
# 开发机：scp 传 sysupgrade image 到设备
scp bin/targets/ramips/mt76x8/*-squashfs-sysupgrade.bin root@<设备IP>:/tmp/
# 设备（从 flash 正常启动的系统）：
sysupgrade /tmp/openwrt-ramips-mt76x8-hilink_hlk-7688a-squashfs-sysupgrade.bin   # 保留配置
# 加 -n 不保留配置（全新安装）
```
initramfs 系统跑 sysupgrade 会报 `Cannot save config while running from ramdisk`（无 jffs2 overlay，正常）；从 flash 启动的系统跑才能保留配置。

**B) sysupgrade 失败 / 底层写 — mtd write**：
```bash
mtd write /tmp/openwrt-ramips-mt76x8-hilink_hlk-7688a-squashfs-sysupgrade.bin firmware && reboot
```

**C) 救砖（设备启动不了，但 U-Boot 完好）— initramfs + mtd write**：
```bash
# U-Boot 串口：tftpboot 0x82000000 *-initramfs-kernel.bin && bootm 0x82000000
# 进 initramfs 系统后：scp/tftp 把 sysupgrade image 传到 /tmp，再 mtd write firmware && reboot
```

**U-Boot Option 2/5 永远别用**（`raspi_erase` 不喂看门狗，擦空 firmware 变砖，教训 #5）。所有可靠路径都基于"Linux 喂狗正常"。

### 构建环境（2026-07-12 已恢复）

之前 `rm -rf build_dir/target-*` 导致的损坏已恢复：package 产物（58 ipk）、opkg 工具、root.orig-ramips 均齐全，`make target/linux/install` 前置条件满足。教训仍有效：清缓存要**精准**，别 `rm -rf build_dir/target-*`（教训 #8）。全量内核编译（clean 后）> 10min，超后台 timeout，需多次续编或前台分步。
