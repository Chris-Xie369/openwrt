#
# Copyright (C) 2026
#
# Pin ffmpeg to 4.4.x for the gateway app, which uses the AVStream->codec API
# removed in ffmpeg 5.1+. Overrides feeds/packages/multimedia/ffmpeg (6.1.2).
# Only builds libffmpeg-full with all built-in codecs/demuxers/muxers/protocols
# enabled by default (covers gateway's RTSP/H.264/MP4/swscale/swresample needs).
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
  TITLE:=FFmpeg 4.4 shared libraries (for gateway)
  SECTION:=libs
  CATEGORY:=Libraries
  URL:=https://ffmpeg.org/
  DEPENDS:=+libpthread +zlib
  PROVIDES:=libffmpeg
endef

define Package/libffmpeg-full/description
 FFmpeg 4.4.x shared libraries pinned for the gateway app (uses AVStream->codec
 API removed in 5.1+). All built-in decoders/demuxers/muxers/protocols enabled
 by default (RTSP, H.264, MP4, swscale, swresample). Overrides feeds 6.1.2.
endef

REAL_CPU_TYPE:=$(call qstrip,$(firstword $(subst +, ,$(CONFIG_CPU_TYPE))))

FFMPEG_CONFIGURE:= \
	CFLAGS="$(TARGET_CFLAGS) $(TARGET_CPPFLAGS) $(FPIC) -Wno-error -Wno-error=incompatible-pointer-types" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	./configure \
		--enable-cross-compile \
		--cross-prefix="$(TARGET_CROSS)" \
		--arch="$(ARCH)" \
		$(if $(REAL_CPU_TYPE),--cpu=$(REAL_CPU_TYPE),) \
		--target-os=linux \
		--prefix="/usr" \
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
		--disable-asm \
		--disable-runtime-cpudetect \
		--enable-small

define Build/Configure
	( cd $(PKG_BUILD_DIR); $(FFMPEG_CONFIGURE) )
endef

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		DESTDIR="$(PKG_INSTALL_DIR)" \
		all install
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include $(1)/usr/lib $(1)/usr/lib/pkgconfig
	$(CP) $(PKG_INSTALL_DIR)/usr/include/lib{avcodec,avfilter,avformat,avutil,swresample,swscale} $(1)/usr/include/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/lib{avcodec,avfilter,avformat,avutil,swresample,swscale}.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/pkgconfig/lib{avcodec,avfilter,avformat,avutil,swresample,swscale}.pc $(1)/usr/lib/pkgconfig/
endef

define Package/libffmpeg-full/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/lib{avcodec,avfilter,avformat,avutil,swresample,swscale}.so.* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libffmpeg-full))
