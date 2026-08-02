#
# Copyright (C) 2026
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=monitor
PKG_VERSION:=1.0
PKG_RELEASE:=1

# Zero-copy: symlink PKG_BUILD_DIR to the external monitor source (untouched)
USE_SOURCE_DIR:=/home/otn/bump-detect/worktree/openwrt-24.10-port/monitor

include $(INCLUDE_DIR)/package.mk

define Package/monitor
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Gateway monitor daemon (MQTT heartbeat + watchdog)
  DEPENDS:=+libpaho-mqtt-c +libopenssl +zlib +libcurl +libpthread
  PKGARCH:=mipsel_24kc
endef

define Package/monitor/description
 C monitor daemon for HLK-7688A gateway: MQTT heartbeat uplink, flock
 singleton, crontab watchdog every 2 min. Cross-compiled from external
 source via USE_SOURCE_DIR.
endef

# monitor internal include paths (mirror of its Makefile HEADER_PATH ./inc/... part)
MONITOR_INC := \
	-I./inc/base -I./inc/util -I./inc/third_party/zlib/ \
	-I./inc/third_party/cjson/ -I./inc/third_party/posix/ \
	-I./inc/http -I./inc/iota/base -I./inc/iota/agentlite \
	-I./inc/iota/service -I./inc/iota/protocol -I./inc/iota/conn \
	-I./inc/periph -I./inc/common -I./inc/file \
	-I./inc/third_party/sm4/ -I./inc/monitor -I./inc

# Cleaned LIBS: dropped -lpolarssl (dead, no API calls) and -lpwrapper (pwrapper.c self-impl)
MONITOR_LIBS := \
	-L$(STAGING_DIR)/usr/lib \
	-lpaho-mqtt3as -lssl -lcrypto -lz -lrt -lm -lcurl -lpthread

define Build/Compile
	# Remove stale artifacts from any prior (old toolchain) build in the source dir
	$(MAKE) -C $(PKG_BUILD_DIR) clean || true

	# Build with full toolchain override (GNU make cmdline > Makefile '=' assignment)
	$(MAKE) -C $(PKG_BUILD_DIR) all \
		CC="$(TARGET_CC)" \
		CXX="$(TARGET_CXX)" \
		CFLAGS="$(TARGET_CFLAGS) -std=c99 -fPIC -fsigned-char -ffunction-sections -fdata-sections -w" \
		CXXFLAGS="$(TARGET_CXXFLAGS) -std=c++11 -fPIC -fexceptions -fsigned-char -ffunction-sections -fdata-sections -w" \
		HEADER_PATH="-I$(STAGING_DIR)/usr/include $(MONITOR_INC)" \
		LIB_PATH="-L$(STAGING_DIR)/usr/lib" \
		LIBS="$(MONITOR_LIBS)"
endef

define Package/monitor/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/monitor $(1)/usr/bin/monitor
endef

$(eval $(call BuildPackage,monitor))
