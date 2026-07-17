#
# Copyright (C) 2026
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=paho-mqtt-c
PKG_VERSION:=1.3.13
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://github.com/eclipse/paho.mqtt.c/archive/refs/tags/v$(PKG_VERSION)
PKG_HASH:=47c77e95609812da82feee30db435c3b7c720d4fd3147d466ead126e657b6d9c
PKG_BUILD_DIR:=$(BUILD_DIR)/paho.mqtt.c-$(PKG_VERSION)

PKG_LICENSE:=EPL-1.0 EDL-1.0
PKG_LICENSE_FILES:=epl-v10 edl-v10

PKG_BUILD_DEPENDS:=libopenssl
CMAKE_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/libpaho-mqtt-c
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=Eclipse Paho MQTT C client library
  URL:=https://www.eclipse.org/paho/
  DEPENDS:=+libopenssl +libpthread
endef

define Package/libpaho-mqtt-c/description
 Eclipse Paho C client library for the MQTT protocol.
 Provides the async SSL variant (libpaho-mqtt3as) used by the gateway app.
endef

CMAKE_OPTIONS += \
	-DPAHO_WITH_SSL=TRUE \
	-DPAHO_BUILD_SHARED=TRUE \
	-DPAHO_BUILD_STATIC=FALSE \
	-DPAHO_BUILD_DOCUMENTATION=FALSE \
	-DPAHO_BUILD_SAMPLES=FALSE \
	-DPAHO_ENABLE_TESTING=FALSE \
	-DPAHO_HIGH_PERFORMANCE=TRUE \
	-DOPENSSL_INCLUDE_DIR=$(STAGING_DIR)/usr/include \
	-DOPENSSL_SSL_LIBRARY=$(STAGING_DIR)/usr/lib/libssl.so \
	-DOPENSSL_CRYPTO_LIBRARY=$(STAGING_DIR)/usr/lib/libcrypto.so \
	-DPAHO_DISABLE_SSL=FALSE

define Build/InstallDev
	$(call Build/InstallDev/cmake,$(1))
endef

define Package/libpaho-mqtt-c/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libpaho-mqtt3a*.so* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libpaho-mqtt-c))
