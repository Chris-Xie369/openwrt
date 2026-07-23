include $(TOPDIR)/rules.mk

PKG_NAME:=gateway-deploy
PKG_VERSION:=1.0
PKG_RELEASE:=1

# 从 gateway 仓库 cp 部署文件，零复制
USE_SOURCE_DIR:=/home/otn/bump-detect/gateway/huaweicloud-sdk20210107

include $(INCLUDE_DIR)/package.mk

define Package/gateway-deploy
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Gateway deployment files (dec.bin + app_enc_tar)
  DEPENDS:=+kmod-rt_rdm +libpaho-mqtt-c +libffmpeg-full +libcurl +libopenssl +zlib +libstdcpp +libatomic +openssl-util
  PKGARCH:=mipsel_24kc
endef

define Package/gateway-deploy/description
 Gateway deployment files installed to /home/bump_detect/:
  - dec.bin (shc-compiled, BUSYBOXON + LF fixed; internal encrypted monitor.sh logic)
  - app_enc_tar (openssl aes256 encrypted app)
 Burn firmware => /home/bump_detect/ ready, no scp needed.
endef

define Build/Compile
	# dec.bin + app_enc_tar 由 build-gateway.sh 预生成（24.10 工具链），本包只打包不重编译。
	# 源码 Makefile 硬编码 14.10 uClibc 工具链，默认 Build/Compile 跑 app target 会链接失败
	# （14.10 .so 依赖 uClibc LFS64/ctype 符号，musl 不提供）。
endef

define Package/gateway-deploy/install
	$(INSTALL_DIR) $(1)/home/bump_detect
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dec.bin $(1)/home/bump_detect/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/app_enc_tar $(1)/home/bump_detect/
endef

$(eval $(call BuildPackage,gateway-deploy))
