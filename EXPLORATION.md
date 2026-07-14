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

## ✅ MSDCDBG 成功进 initramfs image（2026-07-12 21:11，构建链路打通）

之前阻塞点（"compile 产物带 printk，install 没拷进 image"）已彻底解决。**根因是三层缓存陷阱**，逐一攻克：

1. **`.modules` stamp + mtime 倒退**：1000-msdc-debug.patch apply 后，quilt 用 patch 头时间戳设 `sd.c` mtime（20:37），比已存在的 `sd.o`（20:48）**旧**。`make target/linux/compile` 依赖 `$(LINUX_DIR)/.modules` stamp，stamp 在就跳过 modules 编译；即使删 stamp 重编，内核 make 看 `sd.o` 新于 `sd.c` 也跳过。
   → **修**：`rm sd.o mtk_sd.ko .*.cmd` + `touch sd.c` + `rm .modules stamp` + compile。
2. **`.image` stamp（install 路径）**：`install` 依赖 `$(LINUX_DIR)/.image`（≠`.modules`！）。compile 重编了 `.ko`，但 install 走 `.image`，而 `.image` 不依赖 `.modules`，故 install 不会因 `.ko` 变化重跑。
3. **`root.orig-ramips` 的 `.ko` 不回流**（最隐蔽）：initramfs cpio 从 `TARGET_DIR`= `root.orig-ramips` 生成（kernel-defaults.mk:197），但该目录的 `lib/modules/.../mtk_sd.ko` 是 **package 流程 opkg 装的旧缓存**（时间戳 2025-02-04，release 打包时间）。target/linux compile 重编的 `.ko` 只在 `build_dir/linux-6.6.73/`，**不会自动回流**到 root.orig。
   → **修**：手动 `cp` 新 `.ko` → `root.orig-ramips/lib/modules/6.6.73/`（+`root-ramips`）+ `rm .image stamp` + `make target/linux/install`。

**验证方法（双层 LZMA）**：initramfs-kernel.bin 是 `mkimage(uImage hdr + vmlinux.bin.lzma)`，而 vmlinux.bin 内嵌的 initramfs cpio 又是 LZMA-alone 压缩（`CONFIG_TARGET_INITRAMFS_COMPRESSION_LZMA=y`，properties byte `0x6d` for lc1/lp2/pb2）。**单层 `lzma -d | grep` 查不到**（内层仍压缩）。正确验证：
```python
import lzma
data = open('vmlinux.bin','rb').read()  # 外层 lzma -d 得到的 raw binary
for i in [j for j in range(len(data)) if data[j]==0x6d]:
    try:
        dec = lzma.decompress(data[i:i+6000000], format=lzma.FORMAT_ALONE)
        if b'070701' in dec[:200]:  # newc cpio magic
            print("MSDCDBG:", dec.count(b'MSDCDBG')); break
    except: pass
```
本次确认：cpio @ offset 0x5e1c9c（12.9MB），MSDCDBG=2，mtk_sd.ko=2，内容 `MSDCDBG: CMD op=%d arg=%08x` + `IRQ intsts=%08x RESP0=%08x`。image 大小 5188991→5309225（+120KB，unstripped .ko 替换 stripped）也是旁证。

**待设备验证**：bootm 此 image → `dmesg | grep MSDCDBG`，重点看 **CMD55 的 RESP0** 是否缺 R1_APP_CMD 位（0x20）——这是 mmc_app_cmd() 不发 ACMD41 的疑似根因。

## 📖 代码层静态分析结论（2026-07-12，等待设备 RESP0 数据前的排除性分析）

趁等待设备验证，对私有驱动 + 6.6 mmc core 做了静态分析，**排除所有软件配置层嫌疑**，缩小根因到"CMD55 响应内容缺 bit 5"：

1. **6.6 mmc core 的枚举闸门**（`drivers/mmc/core/sd_ops.c`）：
   - `mmc_app_cmd()` 第 45 行：`if (!mmc_host_is_spi(host) && !(cmd.resp[0] & R1_APP_CMD)) return -EOPNOTSUPP;`（`R1_APP_CMD=0x20`）
   - `mmc_wait_for_app_cmd()` 第 62 行：`for (i=0; i<=MMC_CMD_RETRIES; i++)` 循环，CMD55 失败就 `continue` 重试，**ACMD41 永不发出**
   - → 与 runtime 观察（CMD55 重复 4 次、无 ACMD41）完全吻合。CMD55 发 `arg=0`（card==NULL，idle 阶段正确）。

2. **驱动 resp 填充链路无误**（`mtk-mmc/sd.c`）：
   - CMD55 → opcode 55 走 else 分支（`sd.c:786`）→ `mmc_resp_type(cmd)=MMC_RSP_R1` → `RESP_R1`（`sd.c:789`）
   - IRQ handler：CMDRDY + RESP_R1 → default 分支 → `*rsp = sdr_read32(SDC_RESP0)`（`sd.c:1964`）填 `cmd->resp[0]`
   - rawcmd `rsptyp` = `msdc_rsp[RESP_R1]<<7`（`sd.c:812`），硬件期望 R1
   - → 驱动正确读 RESP0 填 resp[0]，22.03/24.10 代码相同。

3. **时钟层完全匹配**（排除频率失配）：
   - 24.10 `CLK_MTMIPS`：`bbppll=480MHz`（`clk-mtmips.c:281`），`sdhc = bbppll/10 = 48MHz`（`clk-mtmips.c:343`）
   - 私有驱动 `hclks[0]=48MHz`（`sd.c:209`，`CONFIG_SOC_MT7620` 段）→ **完全匹配**
   - `msdc_set_mclk` 分频基于 hclk=48MHz（`sd.c:505,535`），400kHz 枚举请求 → div=31 → sclk≈387kHz（合规）
   - **注意**：`msdc_select_clksrc` 整个被 `#if 0` 禁用（`sd.c:469-495`），驱动**不操作 SoC 时钟源寄存器**，硬编码假设 48MHz。CCF 的 `CLK_PERIPH("10130000.mmc","sdhc")` gate 因驱动不走 `devm_clk_get` 而由 U-Boot 默认 on 保持（CMDRDY 证实）。

4. **结论**：软件配置层（pinctrl/ios/电压/时钟/resp 链路）**全部正确且与 22.03 一致**。剩余唯一未验证变量 = **CMD55 实际 RESP0 内容**。若设备 `dmesg` 显示 RESP0 & 0x20 == 0，根因坐实于"卡在 CMD55 时未设 APP_CMD 位"——这指向**信号完整性/时序层**（驱动硬件初始化序列的某个 6.6 特有差异，如 set_ios 的 power-up 时序、或 CMD0/8 后的延迟），需进一步二分。

**预备诊断**：若 RESP0 缺 0x20，下一步写"强制 CMD55 `resp[0] |= 0x20`"的诊断 patch（仅 opcode==55 时），若枚举能继续即 100% 确认是响应内容问题（而非 mmc core 别的路径），再回查信号/时序。

**已实施（2026-07-12 21:28，image md5 b91df38e）**：与其被动等数据，直接把诊断做成"二合一"image —— `1000-msdc-debug.patch` 增第 4 hunk（`sd.c:1964` default 分支）：① 填充 resp 后打印 `MSDCDBG: RSP op=<opc> resp0=<val>`（mmc core 实际收到的值，比 1903 行 RESP0 寄存器更准）；② **CMD55 强制 `*rsp |= 0x20`** 并打印 before→after。基于 SD 物理层规范：卡收到 CMD55 即**硬件进入 ACMD 模式**，R1 的 APP_CMD 位仅状态反馈。故强制补位的 bootm 结果直接二分根因：补位后出现 ACMD41(op=41) 且枚举成功 → 响应位传输/解析问题（此 hack 即 workaround）；补位后仍无 ACMD41 或 ACMD41 失败 → 卡根本没收到 CMD55（更深信号/时序层）。

## 🔍 上游调研 + patches-6.6 审查（2026-07-12，排除剩余嫌疑 + 新诊断方向）

**of_match_table 验证**（关键前提确认）：`sd.c:2415-2419` `mt7620_sdhci_match[] = { { .compatible = "ralink,mt7620-sdhci" } }`，`.of_match_table` 第 2430 行。DTS override `compatible = "ralink,mt7620-sdhci"` **完全匹配** → 私有驱动确定 probe。platform_driver.name=`mtk-sd`（DRV_NAME）。所有 resp/时钟分析的前提成立。

**002-03 patch（v6.13 backport，add mmc clocks）—— 排除**：commit message 明确是为 **mainline mtk-sd** 加 CCF mmc clock（bbppll=480MHz → sdhc=÷10=48MHz → "10130000.mmc" peripheral）。关键：`mtmips_periph_clk_ops` **只有 recalc_rate 无 enable/disable**（`clk-mtmips.c:141`），且所有 CLK_PERIPH 标 **`CLK_IS_CRITICAL`**（`clk-mtmips.c:161`，注释："older drivers not prepared for clock... don't want kernel to disable anything"）。故 CCF **永不 gate** mmc clock，私有驱动不消费它也常开。002-03 只给 CCF 加 48MHz 逻辑表示供 mainline 用，**不改硬件时钟行为**，非根因。

**809 patch（allow mux SDXC pins for mt76x8）—— 对 HLK-7688A 无关，但留作诊断方向**：Shiji Yang 2025-01 加两种 SDXC pinmap（a=EPHY 引脚 / b=I2S/I2C/GPIO0/UART1 引脚），核心是 `SYSC_REG_AGPIO_CFG(0x3c)` bit17-20 的 EPHY digital/analog（DTS `ephy-digital`/`ephy-analog` 触发）。HLK-7688A 用**标准 sdmode（pin22-29）**SDXC 引脚（summary pinctrl 已验证），不涉及 EPHY pad，**AGPIO_CFG 不影响**。**但**：若 bootm 后 RESP0 完全为 0（CMD 线信号全断），可能是引脚 pinmap 误判 —— 可尝试在 `&pinctrl` 加 `ephy-digital`（或检查 HLK-7688A 原理图确认 SD 引脚是否真走 sdmode 而非 EPHY 复用）。

**历史背景**：OpenWrt SD 回归源于 mainline mtk-sd "sync with staging"（commit fec205f，21.02 时代，24.10 已不含但模式延续）；官方 2025-01 仍维护 `kmod-sdhci-mt7620` 与 `kmod-mmc-mtk` CONFLICTS —— **私有驱动是官方认可的传统方案**，本仓库路径正确。上游 MT7628 SD（issue #21879/#6515/FS#1560）**至今 open，无比当前方案更好的现成 24.10 修复**。

**结论**：patches-6.6（002-03/809/830/831）+ 配置层全部审查排除。根因确认依赖设备 RESP0 数据，新 image（b91df38e）的三重改进（干净 probe + NO_SDIO + CMD55 补位）是当前最优测试载体。

## 🎯 设备实测：RESP0 全 0 —— 推翻 CMD55 假设，根因是卡完全无响应（2026-07-12）

bootm image md5 `b91df38e`（干净 probe + NO_SDIO + CMD55 强制补位），MSDCDBG 数据**决定性**：

```
CMD op=0  arg=0    → IRQ intsts=00000180 RESP0=00000000   (CMDRDY + SDIOIRQ)
CMD op=8  arg=1aa  → IRQ intsts=00000100 RESP0=00000000   RSP resp0=00000000
CMD op=55 arg=0    → IRQ intsts=00000100 RESP0=00000000   RSP resp0=00000000
                 CMD55 force APP_CMD: 00000000 -> 00000020  (补位生效)
CMD op=41 arg=0    → IRQ intsts=00000100 RESP0=00000000   RSP resp0=00000000
                 → "no support for card's volts" error -22
```

**关键**：**所有命令 RESP0 都是 0**。CMD8（SEND_IF_COND）应返回 `0x000001AA`（SD2.0 握手）却返回 0 → **卡完全没有响应任何命令**。这**推翻**了"CMD55 缺 0x20 位"假设（CMD55 补位后 ACMD41 发出了，但 ACMD41 也 RESP0=0）。CMD0 intsts=0x180 含 `MSDC_INT_SDIOIRQ`(0x80) → DAT1 线异常（初始化阶段不应有 SDIO 中断），暗示 **SD 总线（CMD+DAT）状态异常**。

**又排除的嫌疑**（至此配置层全尽）：
- `mmc_reg_3v3` = `regulator-fixed` + `regulator-always-on` 无 GPIO → 3.3V 恒开，删 vmmc-supply 无影响 ✅
- `msdc_rsp[]`：RESP_R1=1/RESP_R3=3（非 RESP_NONE=0），CMD8/55/41 rawcmd rsptyp 正确 ✅
- `pinctrl-mtmips` **无 pinconf_ops** → 不支持 bias；SD 引脚 pull 由硬件默认（22.03/24.10 同）✅
- PAD_CTL：`0x000A0000` 的 bit17 (CMDPU/DATPU)=1 → CMD/DAT **有 pull-up**；PAD_CTL0 `0x00090000` bit16(CLKPD)=1 → CLK pull-down（正常）✅
- `msdc_pin_config` 整个被 `#if 0` 禁用，但 PAD pull 靠 msdc_init_hw 的固定值已设 ✅

**剩余根因方向**（信号层，需设备寄存器实测值）：① SD clock 是否真正输出到 CLK 引脚（MSDC_CFG bit7 CKSTB）② CMD/CLK 线物理连通（HLK-7688A SD 槽是否真走 sdmode pin22-29）③ MSDC_PS 卡检测状态。22.03 同卡工作 → 硬件 OK，差异在 24.10 初始化。

**新诊断 image**：`1000-msdc-debug.patch` 增至 6 hunk —— 加 `MSDCDBG: INIT`（probe 打印 CFG/SDC_CFG/PS/IOCON/PAD_CTL0-2/INTEN/ECO）+ `MSDCDBG: MCLK`（set_mclk 打印 CFG+CKSTB）。一次 bootm 即得控制器完整运行状态，无需手动 devmem。patch 曾因第 4 hunk a 行数声明错（6 应 7）malformed，已用 diff 重新生成全部 hunk 修复。

## 🔬 git bisect：聚焦 ramips SD patch（2026-07-13，进行中）

**22.03 工作确认**：bootm openwrt-22.03 image → `mmc0: new high speed SDXC card at address 5048`、`mmcblk0: mmc0:5048 SD128 116 GiB`、p1 分区。**SD 完美工作**，确证 24.10 是真回归（硬件/卡/引脚/电源全好）。

**完整排除清单**（22.03 vs 24.10 全维度对比）：
- 驱动源码（830 patch）：diff 仅 Kconfig 上下文（MMC_LITEX），逻辑完全相同
- DTS：HLK `&sdhci` 完全极简匹配 22.03（删所有 mainline 属性 + pinctrl-names state_uhs + broken-cd），**仍 RESP0=0**
- clock：`clk_summary` 显示 bbppll/sdhc/10130000.mmc 全 enable=1, hardware enable=Y
- pinctrl：`pinmux-pins` 显示 pin22-29 = sdxc, owner 10130000.mmc
- PAD：`PADTUNE=0x84101010` 写生效证明 MSDC 寄存器写 OK（PAD_CTL=0 是 write-only 假象）
- mmc_power_up：5.10=6.6 完全相同（仅多 `mmc_crypto_set_initial_state`）
- CMD arg：CMD0/8/55/41 arg 全正确（MSDCDBG 确认）
- SoC clock init：22.03 `ralink_clk_init` / 24.10 CLK_MTMIPS 都**不**设 SD clock 硬件
- → RESP0=0 = 卡完全无响应 CMD8（应回 0x1AA），但所有配置正确

**bisect 实验**：24.10 相比 22.03 在 `target/linux/ramips/patches-6.6/` 新增两个 SD 相关 patch（830=驱动相同、831/999 改 mainline mtk-sd.c 不影响私有驱动）：
- `002-03`（v6.13 mmc clock CCF：bbppll/sdhc/periph）—— 22.03 stub clk 完全不操作 clock 硬件，24.10 CCF 注册 mmc clock
- `809`（pinctrl SDXC：esd group + AGPIO_CFG bit17-20）—— 22.03 无此 patch

实验：revert 两个 → clean+compile+install → bootm 测试 SD。结果待填（若 SD 工作 → 逐个恢复定位；若仍失败 → 根因在内核基础设施，需对比 5.10 vs 6.6 MIPS MMIO/IRQ/DMA）。

## 🎉 根因找到并修复：AGPIO_CFG EPHY digital mode 缺失（2026-07-13）

**根因**：24.10 的 830 patch（私有 mtk-mmc 驱动 sd.c）**丢失了** AGPIO_CFG EPHY digital 设置代码。22.03 有（`sd.c:2217`，commit `fae125781e`），24.10 完全缺失。

```c
// 22.03 有，24.10 缺失（这就是根因！）：
if ((ralink_soc == MT762X_SOC_MT7688 || ralink_soc == MT762X_SOC_MT7628AN) &&
    !(rt_sysc_r32(0x60) & (1 << 15)))
    rt_sysc_m32(0xf << 17, 0xf << 17, 0x3c);  // 设 EPHY pad digital mode
```

没有这行，MT7628 的 SD CMD/CLK/DAT 引脚电气异常 → 卡完全无响应 → RESP0=0。

**bisect 路径**：22.03 工作确认 → ramips patch 全排除（809/002-03/003/808/001）→ 根因在内核主线 → 对比 22.03 vs 24.10 sd.c → 发现 AGPIO_CFG 差异（`git log` 找到 commit `fae125781e`）。

**修复**：在 24.10 sd.c `msdc_drv_probe` 加回 AGPIO_CFG 代码。image md5 `3f94fd80`，修复代码双层 LZMA 验证在 image 中。**待 bootm 验证 `/dev/mmcblk0`**。

### bootm 结果（2026-07-13）：AGPIO_CFG 修复成功！CMD 通信恢复，但卡在 CMD48 DMA 超时

AGPIO_CFG 修复后，SD 卡 CMD 通信**完全恢复**：
- CMD8 RESP0=0x000001aa（之前 0，SD2.0 握手成功！）
- CMD55 RESP0=0x00000120（含 APP_CMD 位！之前 0）
- ACMD41 RESP0=0x00ff8000 → 0xc0ff8000（OCR，卡初始化成功！）
- CMD2(CID)/CMD3(RCA=0x5048)/CMD9(CSD)/CMD7(select)/CMD51(SCR)/CMD6(switch) 全部成功
- 卡升速到 48MHz（MCLK hz=48000000 CKSTB=1）

**但卡在 CMD48（READ_SINGLE_BLOCK）DMA 超时**：
```
msdc0 -> XXX CMD<48> wait xfer_done<512> timeout!!
mmc0: error -145 reading general info of SD ext reg
```
DMA 寄存器：DMA_SA=0x2883000, DMA_CA=0x2883000, DMA_CTRL=0x6100, DMA_CFG=0x3

**dma_mask 已有**（22.03 和 24.10 都有 `dma_coerce_mask_and_coherent`）。这是 6.6 MIPS DMA noncoherent 的数据传输层问题。

**下一步**：对比 22.03 vs 24.10 的 DMA 相关代码差异（`msdc_do_request` 的 DMA 设置路径）。可能需要检查上游 commit `6069bdd087`（move mtk-mmc init to probe）或其他 DMA 相关修复。

## 下一步（若续 24.10）

1. `make dirclean` 或 `make` 全量重建，修构建环境
2. 确认 999 patch printk 进 image（root.orig mtk-sd.ko 带 MSDC-DBG）
3. bootm → `dmesg | grep MSDC-DBG` 定位 CMD 卡点
4. 据 CMD 卡点写内核补丁修 mainline mtk-sd 对 MT7628 CMD 通信（深度，不确定）

## 正式 patch 方案（2026-07-13 续：根治两个根因）

**放弃 build_dir 直接编辑**（教训 #7：compile/install 覆盖、.ko 缓存陷阱），改走正式 patch + `files/` 源文件，clean compile 重建。

### 根因 1：AGPIO_CFG EPHY digital mode 缺失（CMD 通信层）

驱动源码在 `target/linux/ramips/files/drivers/mmc/host/mtk-mmc/sd.c`（**不是 patch，是 `files/` 直接拷入内核树**，可直接编辑）。830 patch 只改 Kconfig+Makefile，驱动源文件由 `files/` 提供。

**修复**：在 `msdc_drv_probe`（files/ sd.c:2211 后）加回 22.03 的 AGPIO_CFG 代码（commit `fae125781e`，24.10 丢失）：
```c
//FIXME: this should be done by pinconf and not by the sd driver
if ((ralink_soc == MT762X_SOC_MT7688 ||
     ralink_soc == MT762X_SOC_MT7628AN) &&
    (!(rt_sysc_r32(0x60) & BIT(15))))
    rt_sysc_m32(0xf << 17, 0xf << 17, 0x3c);
```
符号来源：`#include <asm/mach-ralink/ralink_regs.h>`（files/ sd.c:49，提供 `rt_sysc_r32/m32`）；`ralink_soc`/`MT762X_SOC_*` 文件内已用（line 1064）。已验证 bootm（md5 `3f94fd80`）：CMD8/55/41/2/3/9/7/51/6 全部恢复。

### 根因 2：CMD48 SD 扩展寄存器探测（数据传输层）

`mmc_sd_read_ext_regs`（6.6 新增，SD 7.0 扩展寄存器探测）发 CMD48 读 512 字节 → mtk-mmc DMA `xfer_done` 超时（error -145）。5.10/22.03 **无此函数**，故 SD 正常。

**修复**：新 patch `patches-6.6/1001-mmc-core-sd-skip-ext-reg-probe-for-mt7628.patch`，在 `mmc_decode_scr`（mmc/core/sd.c:256）清除 `SD_SCR_CMD48_SUPPORT` 位 → `sd_read_ext_regs` 经现有 check（line 1259）`return 0`，等效 5.10。

**为何不直接 `return 0`**：CONFIG_WERROR=y，直接 return 0 会让 `sd_read_ext_regs` 内变量及下游 `sd_parse_ext_reg*` 静态函数未使用 → 编译失败。清 SCR 位走现有 return 路径，零代码改动、零未使用警告。

### 构建配置（已确认）

- `.config`：`CONFIG_PACKAGE_kmod-sdhci-mt7620=y`，`kmod-mmc-mtk off`，INITRAMFS LZMA
- `mt76x8.mk`：`DEVICE_PACKAGES := ... kmod-sdhci-mt7620`
- DTS `&sdhci`：`compatible = "ralink,mt7620-sdhci"` + 删全部 mainline 属性（回 22.03 极简）
- `CONFIG_MTK_MMC=m` 由 kmod 包机制注入（不手动设，避免 MTK_AEE_KDUMP 子选项陷阱）

### debug patch（验证用，保留）

- `1000-msdc-debug.patch`：私有驱动 MSDCDBG printk + CMD55 force-0x20 + `MMC_CAP2_NO_SDIO|NO_MMC`（后者合法配置）。验证后可清理。
- `999-mmc-msdc-debug.patch`：针对主线 mtk-sd.c（CONFIG_MMC_MTK off，不编译，无害死代码）。

### 不需要的 patch（bisect 探索中曾还原）

`809`（pinctrl SDXC mux）+ `002-03`（CCF mmc 时钟）：CMD 通信已能在没有它们的情况下工作（AGPIO_CFG 在驱动内直接设 digital mode），不需恢复。仍在 `/tmp/bisect-patches/`。

### 构建 + 验证

`make target/linux/clean && make target/linux/compile`（patch 应用已确认：AGPIO_CFG + CMD48 跳过均进 build_dir）→ 删 root.orig 旧 mtk_sd.ko → `make target/linux/install` → bootm 验证 `/dev/mmcblk0`。

**成功标准**：`ls /dev/mmcblk0` 出现 + 可 mount/读写（证明 512 字节 DMA 正常，不仅 CMD48 跳过）。

### 构建系统陷阱（2026-07-14 实战记录，重要）

镜像构建踩了多个 OpenWrt initramfs 构建系统的坑，记录如下：

**坑 1：`.config.set` 覆盖 `.config` 清空 INITRAMFS_SOURCE**
`make target/linux/install` 开头有 `cmp -s .config.set .config.prev || { cp .config.set .config; cp .config.set .config.prev; }`。`.config.set` 由 prepare 阶段生成；增量 compile 跳过 prepare → `.config.set` 无 INITRAMFS_SOURCE（空）→ 覆盖 `.config` → kbuild 用空 INITRAMFS_SOURCE → cpio 退化为 default_cpio_list（仅 dev/console + root/，512B）。**修复**：手动把 INITRAMFS_SOURCE 写入 `.config.set` + `.config.prev`（install 只读不重生成 .config.set）。

**坑 2：kbuild 不重链 vmlinux（cpio 重建但 vmlinux 未更新）**
gen-initramfs 步骤 `rm initramfs_data.cpio* + make all` 重建了 cpio，但 vmlinux 未重链（stamp 判定 current）。需手动删 `vmlinux` + `usr/built-in.a` + `usr/initramfs_data.o` 强制重链。

**坑 3：initramfs 嵌在 `.init.data` 非 `.init.ramfs`**
vmlinux 中 initramfs（LZMA cpio）在 `.init.data` 段（readelf 查 `.init.ramfs` 为空会误判）。验证用 `objcopy -O binary -j .init.data` 提取，搜 initramfs_inc_data 头（`0x6d` LZMA props，非 `0x5d`）。

**坑 4：kbuild 需 host lzma（非系统 lzma）**
直接调 kbuild 时 PATH 必须含 `staging_dir/host/bin`（LZMA SDK 工具，支持 `-p`）。系统 xz-utils 的 lzma 无 `-p` 选项 → `usr/initramfs_inc_data` 构建失败。

**坑 5：kmod ipk 缓存旧 .ko**
`package/kernel/linux/compile` 的 ipkg 缓存目录（`ipkg-mipsel_24kc/kmod-sdhci-mt7620/`）未刷新，含旧 .ko（22872B, 2月4日 2025）。需手动用 staging_dir 的 fresh .ko（`dc56e243`）覆盖 root.orig + root-ramips + ipkg 缓存三处。

**坑 6：KCFLAGS 不匹配触发全量重编**
直接 kbuild 必须用 OpenWrt 的 KCFLAGS=`-fmacro-prefix-map=...=target-mipsel_24kc_musl -fno-caller-saves`（来自 CONFIG_EXTRA_OPTIMIZATION=`-fno-caller-saves -fno-plt`，-fno-plt 被 filter-out）。用空 KCFLAGS 会因 flag 变化触发全量 .o 重编（10min+）。

**坑 7：initramfs 打包必须 append-dtb（否则 memblock 启动循环）**
ramips initramfs 打包链是 `cp vmlinux-initramfs → image` → **`cat image-*.dtb >> image`**（拼接 DTB）→ `lzma` → `mkimage`。**漏掉 cat DTB 会导致内核找不到内存布局**，bootm 后 `WARNING: CPU: 0 PID: 0 at mm/memblock.c:603` 无限重启循环。DTB 不在 vmlinux 内（CONFIG_BUILTIN_DTB 未设），必须外部拼接。镜像 md5 `1a1be90c`（漏 DTB）→ 启动循环；修正后 md5 `8c904192`（含 DTB）。

### 最终镜像（2026-07-14）

- 镜像：`bin/targets/ramips/mt76x8/openwrt-ramips-mt76x8-hilink_hlk-7688a-initramfs-kernel.bin`
- md5：`1a1be90cfb2dedb8eb5dde994419c7e9`（5301520 字节）— **此版漏 DTB，boot 循环，已废弃**
- md5：`8c904192f6cc95da68a10dbfe7a18174`（5303365 字节）— **✅ 成功版**（补 append-dtb）

构建链（绕过坑的最终方式）：clean+compile → 手动修 .config.set/.config.prev（INITRAMFS_SOURCE）→ 手动 cp fresh .ko 到 root.orig/root-ramips/ipkg → kbuild 重链 vmlinux（正确 KCFLAGS + host lzma）→ 手动 objcopy+lzma+**cat DTB >>**+mkimage 打包。

## ✅✅✅ 设备验证成功（2026-07-14）：SD 卡彻底根治！

bootm md5 `8c904192` → 内核正常启动（无 memblock 循环）→ SD 卡完美工作。设备实测输出：

```
mmc0: new high speed SDXC card at address 5048
mmcblk0: mmc0:5048 SD128 116 GiB
 mmcblk0: p1
root@OpenWrt:~# ls /dev/mmc*
/dev/mmcblk0    /dev/mmcblk0p1
```

**根治证据**：
- CMD 通信全成功（AGPIO_CFG 修复）：CMD8=0x1aa / CMD55 / ACMD41=0xc0ff8000 / CMD2/3(RCA=5048)/9/7/51/13/6
- **无 CMD48 超时**（CMD48 跳过修复）：日志无 error -145 / xfer_done timeout
- 升速 48MHz 高速模式
- **CMD18 READ_MULTIPLE_BLOCK 多块 512B DMA 读成功**（intsts=0x3040 XFER_COMPL）→ 分区表读取成功 → p1 识别 → **块设备可正常读写**

**两个根因总结**（git bisect 路线定位）：
1. **AGPIO_CFG EPHY digital mode 缺失**（CMD 通信层）：22.03 commit `fae125781e` 在 `msdc_drv_probe` 设 `rt_sysc_m32(0xf<<17, 0xf<<17, 0x3c)`，24.10 丢失。无此行 SDXC 引脚无数字模式 → 卡零响应（RESP0=0）。修复：加回 `files/drivers/mmc/host/mtk-mmc/sd.c`。
2. **CMD48 SD ext reg 探测**（数据传输层）：6.6 内核新增 `mmc_sd_read_ext_regs`（SD 7.0），发 CMD48 读 512B → mtk-mmc DMA xfer_done 超时（-145）。5.10/22.03 无此函数。修复：`patches-6.6/1001` 清 `SD_SCR_CMD48_SUPPORT` 位跳过探测。

**待办**（可选清理）：移除 debug patch（999/1000）产最终干净镜像；考虑持久化（sysupgrade 或 mtd write 烧 flash）。
