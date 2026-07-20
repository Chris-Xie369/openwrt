#
# Copyright (C) 2026
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=gateway
PKG_VERSION:=1.0
PKG_RELEASE:=1

# Zero-copy: symlink PKG_BUILD_DIR to the external gateway source (untouched)
USE_SOURCE_DIR:=/home/otn/bump-detect/gateway/huaweicloud-sdk20210107

include $(INCLUDE_DIR)/package.mk

define Package/gateway
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Huawei Cloud IoT Gateway (bump-detect)
  DEPENDS:=+libpaho-mqtt-c +libopenssl +zlib +libffmpeg-full +libcurl +libpthread
  PKGARCH:=mipsel_24kc
endef

define Package/gateway/description
 C++11 IoT gateway for Huawei Cloud (bump detection + MQTT uplink + RTSP video).
 Cross-compiled from external source via USE_SOURCE_DIR; gateway source untouched.
 All toolchain/lib paths overridden at make invocation time.
endef

# Gateway internal include paths (mirror of its Makefile HEADER_PATH ./inc/... part)
GATEWAY_INC := \
	-I./inc/util -I./inc/third_party/zlib/ -I./inc/third_party/cjson/ -I./inc/third_party/posix/ \
	-I./inc/http -I./inc/iota/base -I./inc/iota/agentlite -I./inc/iota/service -I./inc/iota/conn \
	-I./inc/periph -I./inc/common -I./inc/video -I./inc/file \
	-I./inc/third_party/sm4/ -I./inc/vibration -I./inc/log -I./inc/structure -I./inc/socket -I./inc -I./inc/data_trans \
	-I./inc/uart -I./inc/process

# Cleaned LIBS: dropped -lpolarssl (dead) and -ltar (dead, uses system("tar"))
GATEWAY_LIBS := \
	-L$(STAGING_DIR)/usr/lib \
	-lpaho-mqtt3as -lssl -lcrypto -lz \
	-lswresample -lswscale -lavformat -lavutil -lavcodec \
	-lrt -lm -lcurl -lpthread

define Build/Compile
	# Remove stale artifacts from any prior (old toolchain) build in the source dir
	$(MAKE) -C $(PKG_BUILD_DIR) clean || true

	# Build with full toolchain override (GNU make cmdline > Makefile '=' assignment)
	$(MAKE) -C $(PKG_BUILD_DIR) all \
		CC="$(TARGET_CC)" \
		CXX="$(TARGET_CXX)" \
		CFLAGS="$(TARGET_CFLAGS) -std=c99 -fPIC -fsigned-char -ffunction-sections -fdata-sections -w" \
		CXXFLAGS="$(TARGET_CXXFLAGS) -std=c++11 -fPIC -fexceptions -fsigned-char -ffunction-sections -fdata-sections -w" \
		HEADER_PATH="-I$(STAGING_DIR)/usr/include $(GATEWAY_INC)" \
		LIB_PATH="-L$(STAGING_DIR)/usr/lib" \
		LIBS="$(GATEWAY_LIBS)"
endef

define Package/gateway/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/app $(1)/usr/bin/app
endef

$(eval $(call BuildPackage,gateway))
