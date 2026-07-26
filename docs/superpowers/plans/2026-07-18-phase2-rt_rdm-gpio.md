# Phase 2: rt_rdm kmod + GPIO 基础 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox (`- [ ]`) for tracking.

**Goal:** 实现 rt_rdm kmod 提供 `/dev/rdm0`，让 gateway app 的 PinConfig 跑通 + 基础 GPIO（LED/BEEP/FEED_DOG/LTE_RST）可控制。

**Architecture:** 极简 miscdevice kmod（~90 行），实现 gateway `pin.cpp` 用的 3 ioctl（READ/WRITE/SET_BASE_SYS），ioremap MT7628 SYSCTL 区（0x10000000，0x10000），作为 OpenWrt `kmod-rt_rdm` 包集成进 HLK-7688A 固件 DEVICE_PACKAGES。

**Tech Stack:** Linux 6.6 内核模块（miscdevice + ioremap + ioread32/iowrite32），OpenWrt KernelPackage 构建系统。

## Global Constraints

- gateway 源码零改（pin.cpp 已用 rdm0，commit 5782888）
- MT7628 SYSCTL 物理基址 = `0x10000000`，ioremap 大小 `0x10000`（覆盖 GPIO_CTRL0=0x600 .. GPIO_DATA1 等，最大 offset < 0x10000）
- ioctl 命令码：`READ=0x6B03`, `WRITE=0x6B02`, `SET_BASE_SYS=0x6B0E`
- ioctl 语义（从 pin.cpp 反推，必须精确匹配）：
  - `READ`: `ioctl(fd, READ, &offset)` → 驱动读 offset、读寄存器、把值写回 offset（双向）
  - `WRITE`: `ioctl(fd, WRITE | (offset<<16), &value)` → 驱动 offset=cmd>>16、从指针读 value、写寄存器
  - `SET_BASE_SYS`: `ioctl(fd, SET_BASE_SYS, offset)` → arg 是直接值（非指针），固定 SYSCTL base
- `/dev/rdm0` mode 0666（gateway 用户态访问）

## File Structure

- 创建 `package/kernel/rt_rdm/src/rt_rdm.c`（kmod 代码，~90 行）
- 创建 `package/kernel/rt_rdm/src/Makefile`（内核模块 `obj-m`）
- 创建 `package/kernel/rt_rdm/Makefile`（OpenWrt KernelPackage 定义）
- 修改 `target/linux/ramips/image/mt76x8.mk`（DEVICE_PACKAGES += kmod-rt_rdm）

---

### Task 1: rt_rdm kmod 代码

**Files:**
- Create: `package/kernel/rt_rdm/src/Makefile`
- Create: `package/kernel/rt_rdm/src/rt_rdm.c`

**Interfaces:**
- Produces: `/dev/rdm0`（miscdevice），3 个 ioctl（READ/WRITE/SET_BASE_SYS）

- [ ] **Step 1: 创建 src/Makefile**

```makefile
obj-m += rt_rdm.o
```

- [ ] **Step 2: 创建 src/rt_rdm.c（完整 kmod 代码）**

```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#define RT_RDM_CMD_READ          0x6B03
#define RT_RDM_CMD_WRITE         0x6B02
#define RT_RDM_CMD_SET_BASE_SYS  0x6B0E

/* MT7628/MT7688A SYSCTL 物理基址，覆盖 GPIO/pinmux 寄存器 */
#define RDM_SYSCTL_PHYS   0x10000000UL
#define RDM_SYSCTL_SIZE   0x10000

static void __iomem *rdm_base;

static long rdm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	u32 __user *argp = (u32 __user *)arg;
	u32 val, offset;

	/* SET_BASE_SYS: arg 为直接值（非指针），固定 SYSCTL base。
	 * gateway ra_reg_set_base(0) 调用，arg=0 表示 SYSCTL 区。
	 */
	if (cmd == RT_RDM_CMD_SET_BASE_SYS) {
		/* base 已在 init 时 ioremap SYSCTL，这里无需动作 */
		return 0;
	}

	/* READ: arg=&offset（输入 offset，输出读到的值） */
	if (cmd == RT_RDM_CMD_READ) {
		if (get_user(offset, argp))
			return -EFAULT;
		if (offset >= RDM_SYSCTL_SIZE)
			return -EINVAL;
		val = ioread32(rdm_base + offset);
		if (put_user(val, argp))
			return -EFAULT;
		return 0;
	}

	/* WRITE: cmd = WRITE | (offset<<16), arg=&value */
	if ((cmd & 0xFFFF) == RT_RDM_CMD_WRITE) {
		offset = cmd >> 16;
		if (get_user(val, argp))
			return -EFAULT;
		if (offset >= RDM_SYSCTL_SIZE)
			return -EINVAL;
		iowrite32(val, rdm_base + offset);
		return 0;
	}

	return -ENOTTY;
}

static const struct file_operations rdm_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = rdm_ioctl,
};

static struct miscdevice rdm_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "rdm0",
	.fops  = &rdm_fops,
	.mode  = 0666,
};

static int __init rdm_init(void)
{
	rdm_base = ioremap(RDM_SYSCTL_PHYS, RDM_SYSCTL_SIZE);
	if (!rdm_base) {
		pr_err("rt_rdm: ioremap failed for 0x%08llx\n",
		       (u64)RDM_SYSCTL_PHYS);
		return -ENOMEM;
	}
	pr_info("rt_rdm: mapped SYSCTL 0x%08llx (size 0x%x)\n",
		(u64)RDM_SYSCTL_PHYS, RDM_SYSCTL_SIZE);
	return misc_register(&rdm_miscdev);
}

static void __exit rdm_exit(void)
{
	misc_deregister(&rdm_miscdev);
	iounmap(rdm_base);
}

module_init(rdm_init);
module_exit(rdm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal rdm0 driver (gateway rt_rdm subset) for MT7628/MT7688A");
MODULE_AUTHOR("chris");
```

- [ ] **Step 3: Commit kmod 代码**

```bash
cd /home/chris/workspace/openwrt-24.10
git add package/kernel/rt_rdm/src/
git commit -m "feat(rt_rdm): minimal rdm0 kmod for gateway GPIO access (3 ioctl)"
```

---

### Task 2: OpenWrt KernelPackage 定义

**Files:**
- Create: `package/kernel/rt_rdm/Makefile`

- [ ] **Step 1: 创建 package/kernel/rt_rdm/Makefile**

```makefile
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=kmod-rt_rdm
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define KernelPackage/rt_rdm
  SUBMENU:=Other modules
  TITLE:=Minimal rdm0 driver for gateway (MT7628 rt_rdm subset)
  DEPENDS:=@(TARGET_ramips_mt76x8)
  FILES:=$(PKG_BUILD_DIR)/rt_rdm.ko
  AUTOLOAD:=$(call AutoLoad,30,rt_rdm)
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules
endef

define KernelPackage/rt_rdm/description
 Minimal /dev/rdm0 driver providing READ/WRITE/SET_BASE_SYS ioctl for
 gateway app (pin.cpp) to access MT7628 SYSCTL registers (GPIO/pinmux).
endef

$(eval $(call KernelPackage,rt_rdm))
```

- [ ] **Step 2: Commit Makefile**

```bash
git add package/kernel/rt_rdm/Makefile
git commit -m "feat(rt_rdm): OpenWrt KernelPackage definition"
```

---

### Task 3: 编译 kmod 验证

- [ ] **Step 1: menuconfig 选中 kmod-rt_rdm**

```bash
# .config 加：
echo "CONFIG_PACKAGE_kmod-rt_rdm=y" >> .config
make defconfig
grep CONFIG_PACKAGE_kmod-rt_rdm .config
# 预期: =y
```

- [ ] **Step 2: 编译 kmod**

```bash
make package/rt_rdm/compile V=s 2>&1 | tee /tmp/rt_rdm-build.log | tail -20
```
预期：无 error，产物 `build_dir/.../rt_rdm-1/rt_rdm.ko` 存在。

- [ ] **Step 3: 验证 rt_rdm.ko**

```bash
ls -la build_dir/target-mipsel_24kc_musl/rt_rdm-*/rt_rdm.ko
file build_dir/target-mipsel_24kc_musl/rt_rdm-*/rt_rdm.ko
# 预期: ELF MIPS relocatable
modinfo build_dir/target-mipsel_24kc_musl/rt_rdm-*/rt_rdm.ko 2>/dev/null || \
  strings build_dir/target-mipsel_24kc_musl/rt_rdm-*/rt_rdm.ko | grep -E "rt_rdm|rdm0|GPL" | head
```

---

### Task 4: 集成进 HLK-7688A 固件

**Files:**
- Modify: `target/linux/ramips/image/mt76x8.mk`（hilink_hlk-7688a DEVICE_PACKAGES）

- [ ] **Step 1: mt76x8.mk 加 kmod-rt_rdm**

在 `define Device/hilink_hlk-7688a` 的 DEVICE_PACKAGES 末尾追加 `kmod-rt_rdm`。

```bash
# 找到 hilink_hlk-7688a 的 DEVICE_PACKAGES 行，末尾加 kmod-rt_rdm
grep -n "DEVICE_PACKAGES" target/linux/ramips/image/mt76x8.mk | head
# 手动 Edit 那行末尾加 kmod-rt_rdm（在 libatomic 后）
```

- [ ] **Step 2: make defconfig**

```bash
make defconfig
grep CONFIG_PACKAGE_kmod-rt_rdm .config
# 预期: =y
```

- [ ] **Step 3: 构建固件 sysupgrade（后台）**

```bash
make V=s 2>&1 | tee /tmp/firmware-build.log | tail -20
ls -la bin/targets/ramips/mt76x8/*-sysupgrade.bin
```
预期：固件含 kmod-rt_rdm（rootfs /lib/modules/.../rt_rdm.ko + autoload）。

- [ ] **Step 4: Commit 集成**

```bash
git add target/linux/ramips/image/mt76x8.mk
git commit -m "feat(hlk-7688a): add kmod-rt_rdm to DEVICE_PACKAGES for gateway GPIO"
```

---

### Task 5: 设备验证 GPIO 基础

- [ ] **Step 1: 烧固件 + 验证 /dev/rdm0**

```bash
# 开发机：sysupgrade 烧固件（含 kmod-rt_rdm）
scp bin/targets/ramips/mt76x8/*-sysupgrade.bin root@设备:/tmp/
# 设备（从 flash 启动的系统）：
sysupgrade /tmp/*-sysupgrade.bin
# 重启后：
ls /dev/rdm0
# 预期: /dev/rdm0 存在
lsmod | grep rt_rdm
# 预期: rt_rdm 已 autoload
dmesg | grep "rt_rdm: mapped SYSCTL"
# 预期: mapped SYSCTL 0x10000000
```

- [ ] **Step 2: 部署 app_enc_tar + dec.bin + 跑 app**

```bash
# 设备：dec.bin（修复版，commit 5782888）+ app_enc_tar 已在 /home/bump_detect/
cd /home/bump_detect
./dec.bin
```

- [ ] **Step 3: 验证 PinConfig 跑通（无 GPIO 错误）**

```bash
# app 日志检查：
#   ✗ 不再出现 "Failed to open GPIO device for set base: No such file or directory"
#   ✗ 不再出现 "ioctl read failed: Bad file descriptor" / "gpio write failed"
#   ✓ GpioInit + I2SMapGpio + Gpio38-41MapGpio + GpioWriteDirec/GpioWriteLevel 全成功
```

- [ ] **Step 4: 验证基础 GPIO 可见效果**

```bash
# app 的 PinConfig 末尾 BeepSound(1000,1) → BEEP 启动短响
# LED_SYSTEM 配置 → LED 状态变化
# FEED_DOG → 看门狗喂狗脉冲
# LTE_RST → EC20 4G 复位
# 物理观察：BEEP 响一声、LED 亮/灭
```

**验证通过标准**：app 启动 PinConfig 无 GPIO 错误 + BEEP/LED 可见效果。Phase 2（GPIO 基础）达成。

---

## 风险（实施时验证）

| 风险 | 验证点 |
|------|--------|
| ioctl 语义不匹配 gateway | Task 5 Step 3：app 日志无 "ioctl failed" |
| pinmux 冲突（pinctrl / DTS uart1-i2c-spi 占用引脚） | Task 5 Step 3：PinConfig 的 I2SMapGpio 等成功；若失败查 DTS 节点 status |
| ioremap 区不够（offset 超 0x10000） | Task 1 代码：offset >= 0x10000 返回 -EINVAL，gateway 最大 offset < 0x700 |
| kmod 不 autoload | Task 5 Step 1：lsmod 确认 rt_rdm 加载 |
