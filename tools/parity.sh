#!/bin/sh
# Renders the same frames with the C# (mdk-sdl) and the C++ build and compares them.
#   tools/parity.sh [out-dir] [levels...]      (default levels 3-8)
# Headless: SDL's offscreen driver on Mesa lavapipe (software Vulkan).
cd "$(dirname "$0")/.."
. /work/.toolchain/lavapipe.env
OUT=${1:-build/parity}; [ $# -gt 0 ] && shift
LEVELS=${*:-3 4 5 6 7 8}
CS=/work/MDK/mdk-sdl/src/Mdk.App/bin/Debug/net10.0/mdk_sdl
CPP=build/linux-x64/mdk
mkdir -p "$OUT"
export MDK_HIDDEN=1 MDK_DATA_DIR=/work/MDK SDL_VIDEO_DRIVER=offscreen
for L in $LEVELS; do
  for V in cs cpp; do
    BIN=$CS; [ $V = cpp ] && BIN=$CPP
    MDK_USER_DIR=$(mktemp -d -p "$PWD/$OUT") timeout 120 $BIN --level=$L --mute --screenshot="$OUT/l$L.$V.bmp" > "$OUT/l$L.$V.log" 2>&1
  done
  PYTHONPATH=/work/.toolchain/pylib python3 -I tools/imgdiff.py "$OUT/l$L.cs.bmp" "$OUT/l$L.cpp.bmp" "$OUT/l$L.diff.png" "level $L"
done
