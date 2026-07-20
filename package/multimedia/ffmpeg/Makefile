#
# Copyright (C) 2026
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=ffmpeg
PKG_VERSION:=4.4.1
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.xz
PKG_SOURCE_URL:=https://ffmpeg.org/releases/
PKG_HASH:=eadbad9e9ab30b25f5520fbfde99fae4a92a1ae3c0257a8d68569a4651e30e02
PKG_LICENSE:=LGPL-2.1-or-later GPL-2.0-or-later
PKG_LICENSE_FILES:=COPYING.GPLv2 COPYING.LGPLv2.1

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/libffmpeg-full
  TITLE:=FFmpeg shared libraries (4.4, pinned for gateway)
  SECTION:=libs
  CATEGORY:=Libraries
  URL:=https://ffmpeg.org/
  DEPENDS:=+libpthread +zlib
  PROVIDES:=libffmpeg libffmpeg-mini
endef

define Package/libffmpeg-full/description
 FFmpeg 4.4.x shared libraries pinned for gateway app compatibility.
 Uses AVStream->codec API (removed in ffmpeg 5.1+) needed by gateway.
 Built with default codec set including H.264/RTSP/MP4/swscale/swresample.
endef

REAL_CPU_TYPE:=$(call qstrip,$(firstword $(subst +, ,$(CONFIG_CPU_TYPE))))

FFMPEG_CONFIGURE:= \
	CFLAGS="$(TARGET_CFLAGS) $(TARGET_CPPFLAGS) $(FPIC) -Wno-error=incompatible-pointer-types" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	./configure \
	--enable-cross-compile \
	--cross-prefix="$(TARGET_CROSS)" \
	--arch="$(ARCH)" \
	$(if $(REAL_CPU_TYPE),--cpu=$(REAL_CPU_TYPE),) \
	--target-os=linux \
	--prefix="/usr" \
	--pkg-config="pkg-config" \
	--enable-shared \
	--disable-static \
	--enable-pthreads \
	--enable-zlib \
	--disable-doc \
	--disable-debug \
	--disable-programs \
	--disable-avdevice \
	--disable-lzma \
	--disable-vaapi \
	--disable-vdpau \
	--disable-outdevs \
	--enable-small --disable-asm

ifeq ($(CONFIG_SOFT_FLOAT),y)
FFMPEG_CONFIGURE += \
	--disable-altivec --disable-vsx --disable-power8 \
	--disable-armv5te --disable-armv6 --disable-armv6t2 \
	--disable-fast-unaligned --disable-runtime-cpudetect
endif

define Build/Configure
	( cd $(PKG_BUILD_DIR); $(FFMPEG_CONFIGURE) )
endef

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		DESTDIR="$(PKG_INSTALL_DIR)" \
		all install
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_DIR) $(1)/usr/lib/pkgconfig
	$(CP) $(PKG_INSTALL_DIR)/usr/include/lib{avcodec,avformat,avutil,swresample,swscale} $(1)/usr/include/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/lib{avcodec,avformat,avutil,swresample,swscale}.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/pkgconfig/lib{avcodec,avformat,avutil,swresample,swscale}.pc $(1)/usr/lib/pkgconfig/
endef

define Package/libffmpeg-full/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/lib{avcodec,avformat,avutil,swresample,swscale}.so.* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libffmpeg-full))
