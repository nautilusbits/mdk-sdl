#!/bin/sh
# ./build.sh <platform> [targets...]   platform: linux-x64 | win-x64 | osx-arm64 | osx-x64
# MDK_BUILD_DIR overrides the build folder (default build/<platform>): parallel builds need their own.
set -e
P=${1:-linux-x64}; shift || true
T=/work/.toolchain/android-sdk/cmake/3.31.6/bin  # SDL on Apple needs 3.24+
cd "$(dirname "$0")"
B=${MDK_BUILD_DIR:-build/$P}
[ -f $B/build.ninja ] || $T/cmake -S . -B $B -G Ninja -DCMAKE_MAKE_PROGRAM=$T/ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/$P.cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE:-Release} > /dev/null
$T/cmake --build $B -j$(nproc) ${1:+--target "$@"}
