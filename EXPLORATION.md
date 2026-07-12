# HLK-7688A OpenWrt 24.10 SD 回归探索记录

> **2026-07-12 突破**：找到 24.10 可行解法 —— 切换到 OpenWrt 已内置的私有 mtk-mmc 驱动（`kmod-sdhci-mt7620`），已验证 6.6 编译，待设备验证 SD 枚举。原"SD 未解决"结论已更新。详见下方「🎯 突破」。

## 🎯 2026-07-12 突破：切换私有 mtk-mmc 驱动（可行解法，待设备验证）

**根因精确化**：不是"mainline mtk-sd CMD 回归修不了"，而是 **24.10 默认选错了驱动**。OpenWrt 在 `target/linux/ramips/modules.mk` 定义了**两个互斥 kmod 包**：
- `kmod-mmc-mtk`（line 7-26）：mainline mtk-sd（`CONFIG_MMC_MTK`，`mtk-sd.ko`）—— **对 MT7628 SD 有 CMD 通信回归**（issue #21879 上游 2026-07 仍 open）
- `kmod-sdhci-mt7620`（line 47-61）：22.03 的私有 mtk-mmc（`CONFIG_MTK_MMC`，`mtk-mmc/mtk_sd.ko`，John Crispin Ralink SDK）—— **22.03 验证工作**，`CONFLICTS:=kmod-mmc-mtk` 官方标注互斥

24.10 默认用 kmod-mmc-mtk（坏）。私有驱动源码早已由 `patches-6.6/830` apply 到内核，只是 kmod 包没启用。

**解法**（OpenWrt 官方支持，已实施）：
1. `.config` 切包：`CONFIG_PACKAGE_kmod-sdhci-mt7620=y` + `# CONFIG_PACKAGE_kmod-mmc-mtk is not set`（CONFLICTS 自动处理）
2. `dts/mt7628an_hilink_hlk-7688a.dts` 的 `&sdhci`：`compatible = "ralink,mt7620-sdhci"`（override dtsi，让私有驱动 of_match probe）
3. `make target/linux/compile` —— OpenWrt 据 kmod-sdhci-mt7620 自动设 `CONFIG_MTK_MMC=m` + `MTK_AEE_KDUMP=n` + `MTK_MMC_CD_POLL=n`

**坑（踩过的）**：
- 别手动在 config-6.6 设 `CONFIG_MTK_MMC=y` —— 漏 `MTK_AEE_KDUMP` 子选项 → syncconfig 遇 NEW 失败。必须走 kmod 包机制。
- 别只改 config-6.6 的 `# CONFIG_MMC_MTK is not set` —— 被 modules.mk 的 kmod-mmc-mtk 包 KCONFIG 覆盖。必须禁 `.config` 的 `CONFIG_PACKAGE_kmod-mmc-mtk`。
- mainline `mtk-sd.ko` 与私有 `mtk_sd.ko` 内核模块名都是 `mtk_sd`，不能共存（platform driver name 不同：mainline `mtk-msdc` vs 私有 `mtk-sd`）。

**已验证**：私有驱动 6.6 编译成功（`mtk_sd.ko` 377KB，vermagic `6.6.73 MIPS32_R2 32BIT` 匹配）。依赖 `!MTD_NAND_RALINK` 满足，`CONFIG_SOC_MT7620=y`，`ralink_regs.h` 在。Fix#4 的 831-01 tuning 值（PAD_TUNE=0x84101010）正是抄自此私有驱动 sd.c —— 旁证它是工作源头。

**待验证**：bootm initramfs 看 `/dev/mmcblk0` 是否出现（需设备）。备选热替换：`bin/targets/ramips/mt76x8/mtk_sd.private.ko`（设备上 unbind mtk-msdc + rmmod mtk_sd + insmod）。

---

## 原探索记录（mainline mtk-sd 路径，仍作参考）

> 原归档声明：SD 未解决（mainline mtk-sd 驱动回归），主线移植已转 22.03.5（SD 工作）。
> 本文档记录 24.10 探索全过程，供将来升级 24.10 或理解回归时参考。

## 探索结论

24.10（内核 6.6）在 HLK-7688A 上**除 SD 外全部工作**：WiFi(mt76) / 4G EC20(option+cdc_ether) / flash 分区 / factory 保护 / GPIO(mt7621 96个) / console / U-Boot 引导。**唯独 SD 卡零枚举**。

根因 = **mainline mtk-sd 驱动对 MT7628 SDXC 的 CMD 通信回归**（OpenWrt 上游 #21879/#17411/#17364/#14042，MT7628 SD 在 23+/24+ 坏），非配置问题。社区最新补丁 Fix#4（Shiji Yang 2025-06）只修传输层，不够不着 CMD 通信。**22.03.5 私有 mtk-mmc 对比证实**（同设备同卡 SD 完美工作）。

## SD 根因证据链（配置层全对，仍零枚举）

24.10 initramfs 下逐层验证，配置全部正确：

| 层 | 证据 | 结论 |
|----|------|------|
| **pinctrl** | `/sys/kernel/debug/pinctrl/.../pinmux-pins`：sdmode pin22-29 **全部** `function sdxc`，owner=10130000.mmc | ✓ 引脚切对，排除引脚冲突 |
| **ios** | `/sys/kernel/debug/mmc0/ios`：vdd 21=3.3~3.4V / signal 3.30V（no-1-8-v + vqmmc-supply=mmc_reg_3v3 生效）/ clock 400000Hz / power on / open-drain / 1-bit | ✓ 电压时钟上电全对 |
| **Fix#4** | patches-6.6/831-01(tuning) + 831-02(禁CMD23) + 831-03(PATCH_BIT默认) 已 apply，mtk-sd.c 含 mips_mt762x 标志 | ✓ 已应用，但只修传输层 |
| **卡枚举** | `/sys/class/mmc_host/mmc0/` 只有 device/subsystem/uevent，**无 mmc0:xxxx 卡节点**；刷屏 `no support for card's volts` + `Card stuck being busy` + `error -22 SDIO card` | ✗ 零枚举 |

→ 配置 100% 正确，卡却零枚举 = 驱动层 CMD 通信回归。

错误解读：SD 存储卡本不该响应 CMD5（SDIO 命令），它"响应"了 = 信号层不稳导致线上噪声被误判为响应；mmc core 反复试 SDIO/SD/MMC 三路径，每次乱码响应→不同错误（volts/busy/-22）交替刷屏。

## bootm 打通（LZMA ERROR → 高地址）

- 首次 `tftpboot 0x80100000` + bootm → `LZMA ERROR 1`
- 误判排查：Checksum OK（镜像完整），`Uncompressing...` 打印了（解压已启动）→ 中途失败
- **真因**：加载地址 0x80100000 离解压目标 Load Address 0x80000000 仅 1MB；内核+initramfs 解压后十几 MB，写到第 1MB 顶上压缩源（0x80100000+），覆盖未读完的压缩数据 → LZMA ERROR 1。U-Boot 1.1.3 无重叠保护、bootm 原地解压。
- **解法**：`tftpboot 0x82000000`（高地址，隔 32MB）→ 解压输出追不上源 → 成功
- 旁证：旧 14.07 固件从 flash(0xbc050000) 启动 OK，因源在 flash、目标在 RAM 物理不重叠

## U-Boot erase 复位 bug

- Option 2 烧 sysupgrade：`raspi_erase offs:50000 len:510000` 循环擦 firmware 区
- 稳定在第 **13 个 64KB block**（~832KB，约 2-3s）系统复位 → write 没执行，firmware 区擦空成 FFFFFFFF
- 根因：raspi_erase 长循环不喂看门狗，wdt 超时复位（时间相关，每次相同 block 数）
- **结论**：绝不能用 U-Boot Option2/5 烧 flash；持久化走**系统内 mtd write**（Linux 喂狗正常）
- U-Boot 本身完好（mtd0 独立），能进 Option 4 命令行

## 999 debug patch（已生成，未验证 runtime）

`patches-6.6/999-mmc-msdc-debug.patch`：mtk-sd.c 加两处 `MSDC-DBG` printk：
- `msdc_start_command`（line ~1336）：`pr_info("MSDC-DBG: START cmd=%d arg=%08x rawcmd=%08x")`
- `msdc_irq`（line ~1666）：CMD/DATA 中断时 `pr_info("MSDC-DBG: IRQ events=%08x mask=%08x")`

- dry-run 通过，compile 产物 `mtk-sd.ko` 带 MSDC-DBG（确认 patch 生效）
- MSDC_INT bit：CMDRDY=0x100 / CMDTMO=0x200 / RSPCRCERR=0x400 / XFER_COMPL=0x1000 / DATTMO=0x4000 / DATCRCERR=0x8000
- **未验证 runtime**（构建环境问题，见下）

## ⚠️ 构建环境损坏（未修复）

B 线折腾 printk 进 image 时，构建环境被搞坏：
- `build_dir/target-mipsel_24kc_musl` 曾 `rm -rf`（清旧 .ko 缓存 02-04 残留），连带删了 package 构建产物 + stamp
- `make target/linux/install` 系统性失败：
  - `opkg_conf_load: Could not create lock file .../root.orig-ramips//tmp/opkg.lock`（缺 tmp 目录，mkdir 可修）
  - rm 后 package 产物缺失，opkg 装包连锁失败
  - root.orig-ramips 装不上 kernel modules（lib/modules 空）
- **printk 没进 initramfs image**（compile 产物带，install 没拷进 rootfs）——这是没继续定位 CMD 卡点的唯一阻塞点
- 修复：`make` 全量重建 target（~30min）或 `make dirclean`（~1h 含 toolchain）

## 教训

1. bootm initramfs 必须 tftp 到**高地址**（≥0x81000000），U-Boot 1.1.3 无重叠保护
2. `Checksum OK` = 镜像完整；`LZMA ERROR 1` 是解压环节，先查加载地址别怀疑固件
3. 验证固件要 bootm 实跑看 dmesg，不只看 uImage magic / 编译退出码
4. 先查社区 issue（#17411/#21879/#17364/#14042）再动手，别闭门 debug
5. U-Boot 1.1.3 不能稳定烧大块 flash（erase 循环不喂狗）→ 持久化走系统内 mtd write
6. initramfs 是绝佳调试环境（debugfs/pinctrl 可读，反复 tftp 不刷 flash），先定位根因再改 flash
7. **OpenWrt 改内核源码调试，用正式 patch（patches-*/）+ clean compile，别直接改 build_dir**（compile/install 会覆盖或缓存不刷新）；改 build_dir 后 modules_install 的 .ko 拷贝有缓存陷阱
8. `rm -rf build_dir/target-*` 太激进，连 package 产物一起删，破坏 install——清缓存要精准

## 下一步（若续 24.10）

1. `make dirclean` 或 `make` 全量重建，修构建环境
2. 确认 999 patch printk 进 image（root.orig mtk-sd.ko 带 MSDC-DBG）
3. bootm → `dmesg | grep MSDC-DBG` 定位 CMD 卡点
4. 据 CMD 卡点写内核补丁修 mainline mtk-sd 对 MT7628 CMD 通信（深度，不确定）
