# LoRa(subg)初始化失败调试 handoff

> ## ✅ 已根治（2026-07-19 设备验证）
> **根因**：OpenWrt 24.10 的 `8250_of` 驱动 probe uart1 时不 apply pinctrl，GPIOMODE(0x60) bit24-25 保持默认 01(GPIO 模式)，GPIO45/46(UART1 TX/RX) 被切走 → **UART1 RX 物理断开**，收不到 EFR32 ready。14.10 的 UART 驱动 apply 了 pinctrl 所以正常——**纯驱动层回归**（非波特率/DTS/app 配置）。铁证：`pinctrl-mt76x8.c:25 MT76X8_GPIO_MODE_UART1=24` + `FUNC("uart1",0)/FUNC("gpio",1)`。
> **修复**（app workaround，全在 `openwrt-24.10-port`，main 严格不动）：`subg_service.cpp` HwReboot 开头 `ra_reg_write(0x60, g & ~(0x3<<24))` 清 bit24-25=00 恢复 uart1。配套：`config.h` ttyS0→ttyS1、`pin.cpp` GPIO39/40/41 pinmux KN→AN bit + `1<<pin` UB、`uart_process.cpp` stty→cfsetspeed(B460800)、HwReboot SLEEP 预唤醒（GPIO41 拉低 PB11）。
> **验证链**：D3 诊断 `uart1 1→0` + `rx 0→28` + `fr=0` + 收到 `"V1.0.230423>ready,10,1,128,\r"` → CMD SETTING(90,5,128) → EFR32 持久化 `Setting ok` → `ready,90,5,128` → **`Lora initialization success!`**
> **教训**：调试诊断代码（DIAG 块切 GPIO46 pinmux 采样）会**自己破坏被诊断的 RX** 制造假象（GPIO46 flips=0 是自产，不可信）。诊断必须只读不写寄存器。此前折腾的波特率（B500000/B460800）方向全错——只要有 DIAG 块任何波特率都收不到。
>
> 下文为调试过程原始记录（保留供回顾）。

**日期**:2026-07-19
**目标**:修复 gateway app 从 openwrt-14.10 移植到 openwrt-24.10 后 LoRa(subg/EFR32)初始化失败循环

## 问题描述

HLK-7688A(MT7688A)上,gateway app + EFR32 sub-g 模组在 openwrt-14.10 长期稳定运行。升级到 openwrt-24.10 后,LoRa 初始化循环 failed:`Lora initialization failed! → HwReboot → 循环`。

EFR32 程序(V1.0.230423)和硬件电路完全没改,确认正常(调试串口 USART0 @921600 可通讯)。

## 环境

- **openwrt-24.10** 分支:`wip/24.10-sd-debug`
- **gateway 仓库**:`/home/otn/bump-detect/`,子目录 `gateway/huaweicloud-sdk20210107/`
- **14.10 app**:gateway 仓库 `main` 分支
- **24.10 app**:gateway 仓库当前工作目录(已有多处修改,未 commit)
- **build**:`/home/chris/workspace/openwrt-24.10/package/utils/gateway/build-gateway.sh`
- **打包**:`cd gateway 源码目录 && bash pack.sh --no-build` → app_enc_tar
- **部署**:`scp app_enc_tar root@<设备IP>:/home/bump_detect/` → 设备 `killall app; rm -f app; ./dec.bin`
- **EFR32 固件**:`/home/otn/bump-detect/subg/proj/`(SiLabs EFR32FG14P,Gecko SDK 3.2.3)

## 硬件接线(用户 + 原理图确认)

| EFR32 引脚 | 功能 | MT7688 GPIO | 用途 |
|------------|------|-------------|------|
| USART1 TX = PD14(LOC22) | 无线数据 TX | GPIO46(uart1 RX) | EFR32 → gateway 数据 |
| USART1 RX = PD15 | 无线数据 RX | GPIO45(uart1 TX) | gateway → EFR32 数据 |
| RST | 复位 | GPIO40(LORA_RST) | gateway 复位 EFR32 |
| AUX | 状态指示 | GPIO39(LORA_AUX) | EFR32 就绪状态 |
| SLEEP pin = PB11 | 睡眠控制 | GPIO41(LORA_SLEEP) | gateway 唤醒/睡眠 EFR32 |
| CONTROL = PC11 | 数据方向控制 | GPIO38(LORA_CONTROL) | 半双工方向 |
| USART0 TX = debug | 调试口 @921600 | (PC USB-TTL) | 调试用 |

EFR32 波特率:**512000**(USART1,CHANGELOG 2022.09.22)
EFR32 协议:reset 后主动发 `V1.0.230423>ready,addr,ch,len,\r`(main.c:78)
EFR32 sleep:发完 ready 后检测 PB11,高→sleep,低→working mode

## 已修复的 3 个 24.10 回归(对比 main 14.10 发现)

### 1. config.h:ttyS0 → ttyS1
- **14.10**:`LORA_DEV_NAME "/dev/ttyS0"`(14.10 ttyS0=uart1)
- **24.10**:ttyS0=uartlite(console),uart1=ttyS1
- **修复**:`config.h:29` 改 `/dev/ttyS1`
- **验证**:DIAG 确认 ttyS1→10000d00.uart1,8250 probe 成功

### 2. pin.cpp:Gpio39/40/41MapGpio pinmux bit KN→AN
- **14.10**:Gpio39MapGpio 写 `0x64 bit26/27`,Gpio40 写 `bit24/25`,Gpio41 写 `bit22/23`
- **问题**:这些 bit 是 P4/P3/P2LED_KN(GPIO30/31/32),不是 GPIO39/40/41
- **ramips MT76X8 正确映射**(pinctrl-mt76x8.c):
  - GPIO39=P4LED_AN shift42 → 0x64 bit10
  - GPIO40=P3LED_AN shift40 → 0x64 bit8
  - GPIO41=P2LED_AN shift38 → 0x64 bit6
- **修复**:Gpio39MapGpio bit26→bit10,Gpio40 bit24→bit8,Gpio41 bit22→bit6
- **验证**:EFR32 debug 口从 `Sleep pin(PB11) pullup,sleepmode!` 变为 `pull down,working mode!`(GPIO41 能拉低 PB11 唤醒 EFR32)

### 3. uart_process.cpp:波特率 stty → cfsetspeed(B460800)
- **14.10**:`SerialSpeciBaudInit(230400) + system("stty ... 460800")`(busybox stty 设 B460800)
- **24.10 问题**:busybox 无 stty applet(`CONFIG_BUSYBOX_DEFAULT_STTY not set`);custom speed deprecated(dmesg 警告)
- **14.10 注释**:"奇怪,只有460800时才能跟sub-g的512000通讯"
- **修复**:用标准 termios `cfsetspeed(B460800)`(Linux 内置,不依赖 stty)
- **注意**:曾试 B500000(差 EFR32 512000 2.3%)但仍 rx=0。B460800 是和 14.10 完全一致的配置,**待设备验证**

### 附带修复:pin.cpp GpioWriteLevel/GpioGetValue pin_offset
- 原代码 `1<<pin`(pin≥32 是 UB)改用 `pin_offset=pin-32`(像 GpioWriteDirec)
- 实际 objdump 显示 MIPS sllv 模32 新旧都正确(不是根因,但消除 UB)

### 附带修复:serial.c custom_divisor 四舍五入
- 原整数除法 `baud_base/baud` 改 `(baud_base+baud/2)/ba`
- 2500000/512000=4.88→5(500000),原=4(625000)

## 当前状态(待验证 B460800)

**最新 app_enc_tar**:312176 bytes(2026-07-19 20:26 build),含 B460800 + 所有修复 + 诊断代码

**DIAG 诊断**(subg_service.cpp:988-1010,每次 Initialization 循环打印):
- `HwReboot RST off=? on=? SLEEP ?->? AUX=?`:GPIO40 RST toggle + GPIO41 SLEEP + GPIO39 AUX
- `DIAG fd=? AUX=? avail=? rx=? fr=? brk=? ovr=? CUST=? div=? base=? AGPIO=? GMODE=? GMODE2=?`:8250 状态 + GPIO 寄存器
- `GPIO46采样5s flips=? hi=?/500`:GPIO46(uart1 RX 引脚)物理信号直读(切 GPIO 模式)

**最后一次测试结果**(B500000 版):
- SLEEP 0→0(GPIO41 拉低 PB11,wake EFR32)✓
- EFR32 debug 口重复打印(被 HwReboot 复位)✓
- `Sleep pin(PB11) pull down,working mode!` ✓
- **rx=249 fr=0 brk=249**:8250 只检测 break(reset 期间 PD14 低),不收 ready
- **GPIO46 flips=0**:PD14 静态高(reset 后不翻转?或 24.10 uart1 RX pad 问题)
- div=5(base=2500000),CUST=1(注:DIAG 用 flags&0x10 判断 ASYNC_SPD_CUST 是错的,ASYNC_SPD_CUST=0x1000 不是 0x10;CUST=1 是 ASYNCB_SAK 误报)

**待验证**:B460800 是否能收到 ready(rx>0 / Lora initialization success)

## 如果 B460800 仍不 work 的下一步方向

1. **对比 14.10 vs 24.10 的 SerialSetParity**:14.10 main 的 SerialSetParity(termios 配置)是否和 24.10 有差异(`git diff main -- serial.c` 的 SerialSetParity 部分)

2. **ttyS0 vs ttyS1 映射**:14.10 用 ttyS0(可能 14.10 ramips tty 映射 ttyS0=uart1,和 24.10 ttyS1=uart1 不同)。确认 14.10 ramips 的 tty 映射

3. **GPIO46(uart1 RX)pad 配置**:GPIO46 直读 flips=0(PD14 不翻转)是物理层证据。但 EFR32 发 ready(USART1 TX PD14,Usart1_InitToWireless 路由确认)。可能 24.10 对 uart1 RX pad 的 input 配置(schmitt/pull/input enable)和 14.10 不同。查 openwrt-24.10 ramips dts/内核对 uart1 RX pad 的配置

4. **示波器**:物理确认 PD14 reset 后是否翻转 + GPIO46 是否跟随。这是解开物理矛盾的最终手段

5. **清掉诊断代码**:subg_service.cpp 有 DIAG 块 + GPIO46 采样 + HwReboot 监测 + serial.c 注释 + uart_process.cpp 注释。根治后需清理

## 已修改但未 commit 的文件(gateway 仓库)

- `inc/common/config.h`:LORA_DEV_NAME ttyS0→ttyS1
- `src/periph/pin.cpp`:GpioWriteLevel/GpioGetValue pin_offset + Gpio39/40/41MapGpio AN bit
- `src/periph/serial.c`:custom_divisor 四舍五入
- `src/uart/uart_process.cpp`:SerialSpeciBaudInit+stty → cfsetspeed(B460800)
- `src/uart/subg_service.cpp`:PortConfig 用 LORA_DEV_NAME 宏 + 诊断代码(DIAG2/GPIO46采样/HwReboot监测)

## 关键参考

- EFR32 固件:`/home/otn/bump-detect/subg/proj/`(只读,不改)
  - `src/main.c:78`:发 ready
  - `usart/retargetserialconfig.h:18`:RF_UART=USART1
  - `usart/retargetserial.c:245-289`:Usart1_InitToWireless(PD14 路由确认)
  - `config/pin_config.h:108-110`:USART1_TX=PD14 LOC22
- openwrt ramips pinctrl:`build_dir/.../linux-6.6.73/drivers/pinctrl/mediatek/pinctrl-mt76x8.c`
- 设备 EFR32 debug 口:USART0 @921600(用户 USB-TTL 连接)
- 项目规则:`/home/otn/bump-detect/CLAUDE.md` + `/home/otn/bump-detect/gateway/CLAUDE.md`
