#!/bin/sh
# Builds the release executables and packages them like mdk-sdl's releases (build.yml):
#   linux-x64 ─► mdk-linux-x64.tar.gz   (mdk, libSDL3.so.0, icon.png)
#   win-x64   ─► mdk-win-x64.zip        (mdk.exe, SDL3.dll)
#   osx-arm64 ─► mdk-osx-arm64.zip      (mdk, libSDL3.dylib, icon.png)
#   osx-x64   ─► mdk-osx-x64.zip        (mdk, libSDL3.dylib, icon.png)
#   android   ─► mdk-android.apk        (copied when android/ built one)
# Into build/publish/. Usage: tools/publish.sh [platforms...]   (default: all four)
set -e
cd "$(dirname "$0")/.."
OUT=build/publish
PLATFORMS=${*:-linux-x64 win-x64 osx-arm64 osx-x64}
mkdir -p $OUT

for P in $PLATFORMS; do
  BUILD_TYPE=Release ./build.sh $P mdk
  B=build/$P
  S=$OUT/$P
  rm -rf $S && mkdir -p $S

  # The program, SDL3's library next to it (its rpath is the program's folder), the icon.
  case $P in
    win-*)
      cp $B/mdk.exe $B/SDL3.dll $S/
      (cd $S && rm -f ../mdk-$P.zip && zip -q ../mdk-$P.zip mdk.exe SDL3.dll)
      ;;
    osx-*)
      cp $B/mdk $B/libSDL3.dylib assets/icon.png $S/
      (cd $S && rm -f ../mdk-$P.zip && zip -q ../mdk-$P.zip mdk libSDL3.dylib icon.png)
      ;;
    linux-*)
      # The loader looks for SDL's soname, libSDL3.so.0.
      cp $B/mdk $B/libSDL3.so.0 assets/icon.png $S/
      tar -czf $OUT/mdk-$P.tar.gz -C $S mdk libSDL3.so.0 icon.png
      ;;
  esac
done

# The Android APK (built separately in android/), if there is one.
APK=$(find android -path '*/build/outputs/apk/*' -name '*.apk' 2>/dev/null | sort | tail -n 1)
if [ -n "$APK" ]; then
  cp "$APK" $OUT/mdk-android.apk
fi

ls -la $OUT
