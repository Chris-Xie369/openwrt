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
