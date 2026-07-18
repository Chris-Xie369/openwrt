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
