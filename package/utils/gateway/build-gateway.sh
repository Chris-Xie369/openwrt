#!/bin/bash
# Cross-compile the bump-detect gateway app for OpenWrt-24.10 (HLK-7688A,
# mipsel_24kc / soft-float / musl). Uses the OpenWrt toolchain + staging_dir
# (libpaho-mqtt-c, ffmpeg 4.4, openssl, curl, zlib must already be built and
# staged — run `make package/<each>/compile` once first).
#
# This script REPLACES the OpenWrt package Makefile so NO ipk is produced —
# just the bare `app` binary dropped in the gateway source dir.
#
# Prereq: gateway repo checked out on the `openwrt-24.10-port` branch
# (HMAC migrated to OpenSSL 1.1+ API in string_util.cpp).
#
# CFLAGS mirror OpenWrt 24.10 TARGET_CFLAGS for mipsel_24kc soft-float. If
# OpenWrt is upgraded, re-derive from `make package/gateway/compile V=s` log.

set -e

OWRT=/home/chris/workspace/openwrt-24.10
STAGING=$OWRT/staging_dir/target-mipsel_24kc_musl
TC=$OWRT/staging_dir/toolchain-mipsel_24kc_gcc-13.3.0_musl
export PATH="$TC/bin:$PATH"

CC=mipsel-openwrt-linux-musl-gcc
CXX=mipsel-openwrt-linux-musl-g++

COMMON="-Os -pipe -mno-branch-likely -mips32r2 -mtune=24kc -fno-caller-saves -fno-plt -fhonour-copts -msoft-float -mips16 -minterlink-mips16 -fPIC -fsigned-char -ffunction-sections -fdata-sections -w"
CFLAGS="$COMMON"
CXXFLAGS="$COMMON -std=c++11 -fexceptions -Wno-write-strings -Wno-deprecated-declarations"

INC="-I./inc/util -I./inc/third_party/zlib/ -I./inc/third_party/cjson/ -I./inc/third_party/posix/ \
-I./inc/http -I./inc/iota/base -I./inc/iota/agentlite -I./inc/iota/service -I./inc/iota/conn \
-I./inc/periph -I./inc/common -I./inc/video -I./inc/file -I./inc/third_party/sm4/ \
-I./inc/vibration -I./inc/log -I./inc/structure -I./inc/socket -I./inc -I./inc/data_trans \
-I./inc/uart -I./inc/process"
HEADER_PATH="-I$STAGING/usr/include $INC"
LIB_PATH="-L$STAGING/usr/lib"
LIBS="$LIB_PATH -Wl,-rpath-link,$STAGING/usr/lib -lpaho-mqtt3as -lssl -lcrypto -lz -lswresample -lswscale -lavformat -lavutil -lavcodec -lrt -lm -lcurl -lpthread -latomic"

cd /home/otn/bump-detect/worktree/openwrt-24.10-port/gateway/huaweicloud-sdk20210107
echo "=== branch: $(git -C /home/otn/bump-detect/worktree/openwrt-24.10-port rev-parse --abbrev-ref HEAD) ==="
echo "=== cleaning & building app ==="
make clean || true
make all \
  CC="$CC" CXX="$CXX" \
  CFLAGS="$CFLAGS" CXXFLAGS="$CXXFLAGS" \
  HEADER_PATH="$HEADER_PATH" LIB_PATH="$LIB_PATH" LIBS="$LIBS"

echo
echo "✓ Built: $(pwd)/app"
file app
