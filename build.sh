#!/bin/sh
# ./build.sh <platform> [targets...]   platform: linux-x64 | linux-arm64 | win-x64 | osx-arm64 | osx-x64
# MDK_BUILD_DIR overrides the build folder (default build/<platform>): parallel builds need their own.
# CI (.github/workflows/build.yml): CMAKE and NINJA name the tools, MDK_NATIVE=1 builds with the
# host's compiler (no toolchain file), MDK_CMAKE_ARGS adds options (-DDXC=..., -DSPIRV_CROSS=...).
set -e
P=${1:-linux-x64}; shift || true
T=/work/.toolchain/android-sdk/cmake/3.31.6/bin  # SDL on Apple needs 3.24+
CMAKE=${CMAKE:-$T/cmake}
NINJA=${NINJA:-$T/ninja}
NINJA=$(command -v "$NINJA" || echo "$NINJA")  # CMake wants a path
TC=cmake/toolchains/$P.cmake
if [ "$MDK_NATIVE" = 1 ]; then
  TC=
fi
cd "$(dirname "$0")"
B=${MDK_BUILD_DIR:-build/$P}
[ -f $B/build.ninja ] || $CMAKE -S . -B $B -G Ninja -DCMAKE_MAKE_PROGRAM=$NINJA \
  ${TC:+-DCMAKE_TOOLCHAIN_FILE=$TC} -DCMAKE_BUILD_TYPE=${BUILD_TYPE:-Release} $MDK_CMAKE_ARGS > /dev/null
$CMAKE --build $B -j$(nproc 2>/dev/null || sysctl -n hw.ncpu) ${1:+--target "$@"}
