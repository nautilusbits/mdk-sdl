#!/bin/sh
# Syntax-checks headers on their own: tools/check_headers.sh src/Mdk.Game/Kurt/*.h
# (each header compiled alone, so missing includes show up). Prints errors only.
cd "$(dirname "$0")/.."
TP=third_party
mkdir -p build; tmp=$(mktemp -d -p "$PWD/build")
for h in "$@"; do
  printf '#include "%s"\n' "${h#src/}" > $tmp/t.cpp
  /work/.toolchain/zig/zig c++ -target x86_64-linux-gnu.2.34 -std=c++20 -c -o $tmp/t.o \
    -Isrc -I$TP/SDL/include -I$TP/nlohmann -I$TP/picosha2 -I$TP/doctest -I$TP/miniz $tmp/t.cpp 2>&1 \
    | grep -E 'error' | head -5 | sed "s|^|$h: |"
done
rm -rf $tmp
