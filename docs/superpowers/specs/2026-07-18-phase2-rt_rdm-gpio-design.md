# Phase 2: rt_rdm kmod + GPIO 基础（让 gateway 操作硬件）

## Context

gateway app（bump-detect 网关）在 OpenWrt-24.10 启动成功（Phase 1，pid 2560，commit 5782888），但卡在 `/dev/rdm0`（GPIO 硬件接口层）。gateway 的 `pin.cpp` 通过 `/dev/rdm0`（MediaTek rt_rdm 驱动，主线 OpenWrt 无）ioctl 读写 SoC 物理寄存器，操作 GPIO 方向/电平 + pinmux 复用。

Phase 2 目标（GPIO 基础范围）：提供 rdm0 能力，让 gateway PinConfig 跑通 + 基础 GPIO（LED/BEEP/FEED_DOG/LTE_RST）可控制。传感器（ADXL345）/4G/LoRa 业务后续阶段。

## 约束
- gateway 源码零改（pin.cpp 已用 rdm0，不改）
- 范围：GPIO 基础（rdm0 + PinConfig + 基础 GPIO 可见验证）
- 方案：移植/实现 rt_rdm kmod（用户决策 A）

## ioctl 语义（精确，来自 pin.cpp）

gateway `pin.cpp` 用 3 个 ioctl，rt_rdm kmod 必须精确匹配：

```c
// ra_reg_read(int offset) → ioctl(gpio_fd, RT_RDM_CMD_READ, &offset)
//   驱动：读 offset（输入），ioread32(base+offset)，把值写回 offset（输出）
//   pin.cpp 返回 offset（已被驱动改为读到的值）
//
// ra_reg_write(int offset, int value) → ioctl(gpio_fd, RT_RDM_CMD_WRITE | (offset<<16), &value)
//   驱动：offset = cmd >> 16，从 arg 指针读 value，iowrite32(value, base+offset)
//
// ra_reg_set_base(int offset) → ioctl(fd, RT_RDM_CMD_SET_BASE_SYS, offset)
//   驱动：base = arg（直接值，非指针），设寄存器基址
```

命令码：READ=0x6B03, WRITE=0x6B02, SET_BASE_SYS=0x6B0E。

## 设计

### 1. rdm0 kmod（package/kernel/rt_rdm/）

极简实现（~80 行，6.6 原生），只支持 gateway 用的 3 ioctl：
- `miscdevice` 注册 `/dev/rdm0`（或 MAJOR 223，匹配老 rt_rdm）
- 模块加载时 ioremap MT7628 SYSCTL 区（0x10000000，大小 0x10000 覆盖 GPIO/pinmux 寄存器）
- `unlocked_ioctl`：
  - `SET_BASE_SYS`: base = arg（MT7628 = 0x10000000；允许运行时切 base）
  - `READ`: get_user(offset) → val=ioread32(ioremap_base + offset) → put_user(val, &argp)
  - `WRITE`: offset = cmd>>16 → get_user(val, &argp) → iowrite32(val, ioremap_base + offset)
- 不依赖老 `rt_mmap.h`（基址硬编码 0x10000000 或从 SoC 定义）

### 2. GPIO 验证

gateway `PinConfig()`（pin.cpp:680）跑通：
- `GpioInit()`（ra_reg_set_base + open /dev/rdm0）→ 不再 "Failed to open GPIO device"
- pinmux 映射（I2SMapGpio/Gpio38-41MapGpio/I2CMapGpio/SpiMapGpio 等）→ 不再 "gpio write failed"
- GPIO 方向/电平（LED_SYSTEM=1/BEEP=0/FEED_DOG=36/LTE_RST=11 等）

验证标准（可见）：
- app 启动日志无 "Failed to open GPIO device" / "ioctl read/write failed: Bad file descriptor"
- LED_SYSTEM 亮/灭、BEEP 启动短响、FEED_DOG 喂狗脉冲、LTE_RST 复位 4G

### 3. pinmux 冲突（关键风险）

gateway 改 `REG_MODE_ADD1/2`（0x60/0x64，pinmux）通过 rdm0 **直接物理写**，覆盖主线 pinctrl（DTS probe 时设）。

rt_rdm 的 iowrite32 绕过 pinctrl 框架（直接写寄存器），pinctrl 不 lock 物理寄存器。gateway 运行时覆盖生效。

需验证：
- DTS 的 uart1/i2c/spi 节点 status（gateway 要用的 GPIO 引脚不被这些节点占用——如 uart1 status=okay 会占 pin15/16，与 gateway GPIO 冲突）
- gateway 改 pinmux 后，对应引脚确实变 GPIO（读回 REG_MODE_ADD 验证）

### 4. 集成

- `package/kernel/rt_rdm/Makefile`（kmod 包：`PKG_NAME:=kmod-rt_rdm`，`KernelPackage` 宏）
- `kmod-rt_rdm` 加 HLK-7688A `DEVICE_PACKAGES`（mt76x8.mk）
- `.config` `CONFIG_PACKAGE_kmod-rt_rdm=y`
- 固件含 kmod，开机自动 insmod /dev/rdm0

## 实施（关键文件）

- **新建** `package/kernel/rt_rdm/Makefile`（OpenWrt KernelPackage 定义）
- **新建** `package/kernel/rt_rdm/src/rt_rdm.c`（~80 行，miscdevice + 3 ioctl + ioremap）
- **修改** `target/linux/ramips/image/mt76x8.mk`（DEVICE_PACKAGES += kmod-rt_rdm）

参考：老 rt_rdm.c（keenetic/kernel-49:drivers/net/rt_rdm/rdm.c，290 行）的 ioctl 语义；gateway pin.cpp 的调用方式。

## 验证（端到端）

1. `make package/rt_rdm/compile V=s` → kmod-rt_rdm ipk
2. kmod-rt_rdm 进固件（DEVICE_PACKAGES）+ sysupgrade 重烧
3. 设备 `/dev/rdm0` 存在（`ls /dev/rdm0`）
4. app 启动 PinConfig 无 GPIO 错误（dmesg / app 日志）
5. 基础 GPIO 可见效果（LED/BEEP/FEED_DOG/LTE_RST）

## 风险

| 风险 | 等级 | 缓解 |
|------|------|------|
| pinmux 冲突（pinctrl lock / DTS 占用引脚） | 中 | rt_rdm 物理写覆盖；验证 DTS uart1/i2c/spi status；读回 REG_MODE_ADD 确认 |
| ioremap 区大小（SYSCTL 0x10000000 + ?） | 低 | ioremap 0x10000 覆盖所有 GPIO/pinmux 寄存器（最大 offset 0x700 内） |
| ioctl 语义不匹配 gateway | 中 | 精确按 pin.cpp 的 READ/WRITE/SET_BASE_SYS 语义实现（offset 双向 / cmd 编码 / arg 指针） |
| ioread32/iowrite32 对 MT7628 寄存器 | 低 | MIPS 标准，ioremap 后 ioread32/iowrite32 直接工作 |
