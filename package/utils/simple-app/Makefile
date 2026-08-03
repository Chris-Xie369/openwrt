#
# Copyright (C) 2026
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=simple-app
PKG_VERSION:=1.0
PKG_RELEASE:=1

# Same source as gateway, compiled with -DSIMPLE_MODE (conditional compilation)
USE_SOURCE_DIR:=/home/otn/bump-detect/worktree/openwrt-24.10-port/gateway/huaweicloud-sdk20210107

include $(INCLUDE_DIR)/package.mk

define Package/simple-app
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Gateway simple app (MQTT + IPC + network, no video/radar/LPR)
  DEPENDS:=+libpaho-mqtt-c +libopenssl +zlib +libcurl +libpthread +libstdcpp +libubox +libuci
  PKGARCH:=mipsel_24kc
endef

define Package/simple-app/description
 Stripped-down gateway app for fallback when app_enc_tar is corrupted.
 Keeps MQTT, network probe, monitor IPC, SubG (ignores STRK/REQD).
 Removes video/ffmpeg, LPR, radar, sound barrier, EWS, HTTP upload.
 Compiled via Makefile.simple with -DSIMPLE_MODE.
endef

# Gateway internal include paths (same as full app)
SIMPLE_INC := \
	-I./inc/util -I./inc/third_party/zlib/ -I./inc/third_party/cjson/ \
	-I./inc/third_party/posix/ -I./inc/third_party/sm4/ \
	-I./inc/http -I./inc/iota/base -I./inc/iota/agentlite \
	-I./inc/iota/service -I./inc/iota/protocol -I./inc/iota/conn \
	-I./inc/periph -I./inc/common -I./inc/video -I./inc/ftp -I./inc/file \
	-I./inc/data_trans -I./inc/lib -I./inc/mov -I./inc/my_rtsp -I./inc/nlohmann -I./inc/iota \
	-I./inc/lora -I./inc/dtu -I./inc/monitor -I./inc -I./inc/process \
	-I./inc/structure -I./inc/socket -I./inc/uart -I./inc/vibration -I./inc/log

# Cleaned LIBS: no FFmpeg (swresample/swscale/avformat/avutil/avcodec), no polarssl, no tar
SIMPLE_LIBS := \
	-L$(STAGING_DIR)/usr/lib \
	-lpaho-mqtt3as -lssl -lcrypto -lz -lrt -lm -lcurl -lpthread -latomic

define Build/Compile
	# Remove stale artifacts from any prior build
	$(MAKE) -C $(PKG_BUILD_DIR) -f Makefile.simple clean || true

	# Build simple_app with Makefile.simple + SIMPLE_MODE define
	$(MAKE) -C $(PKG_BUILD_DIR) -f Makefile.simple all \
		CC="$(TARGET_CC)" \
		CXX="$(TARGET_CXX)" \
		CFLAGS="$(TARGET_CFLAGS) -std=c99 -fPIC -fsigned-char -ffunction-sections -fdata-sections -w -DSIMPLE_MODE -D_GNU_SOURCE" \
		CXXFLAGS="$(TARGET_CXXFLAGS) -std=c++11 -fPIC -fexceptions -fsigned-char -ffunction-sections -fdata-sections -w -DSIMPLE_MODE -D_GNU_SOURCE" \
		HEADER_PATH="-I$(STAGING_DIR)/usr/include $(SIMPLE_INC)" \
		LIB_PATH="-L$(STAGING_DIR)/usr/lib" \
		LIBS="$(SIMPLE_LIBS)"
endef

define Package/simple-app/install
	$(INSTALL_DIR) $(1)/home/config
	# Package as simple_app.tar.gz for monitor.sh fallback mechanism
	cd $(PKG_BUILD_DIR) && tar czf simple_app.tar.gz simple_app
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/simple_app.tar.gz $(1)/home/config/simple_app.tar.gz
endef

$(eval $(call BuildPackage,simple-app))
