#!/bin/sh
# Builds the release executables and packages them like mdk-sdl's releases (build.yml):
#   linux-x64 ─► mdk-linux-x64.tar.gz   (mdk-sdl, icon.png)
#   win-x64   ─► mdk-win-x64.zip        (mdk-sdl.exe)
#   osx-arm64 ─► mdk-osx-arm64.zip      (mdk-sdl, icon.png)
#   osx-x64   ─► mdk-osx-x64.zip        (mdk-sdl, icon.png)
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

  # The program, the icon, and SDL3's library when it isn't linked in (MDK_STATIC_SDL=OFF;
  # the program's rpath is its folder; Linux's loader looks for the soname, libSDL3.so.0).
  case $P in
    win-*) EXE=mdk-sdl.exe; LIB=SDL3.dll; ICON= ;;
    osx-*) EXE=mdk-sdl; LIB=libSDL3.dylib; ICON=icon.png ;;
    linux-*) EXE=mdk-sdl; LIB=libSDL3.so.0; ICON=icon.png ;;
  esac
  grep -q "^MDK_STATIC_SDL:BOOL=OFF" $B/CMakeCache.txt || LIB=
  cp $B/$EXE ${LIB:+$B/$LIB} ${ICON:+assets/$ICON} $S/

  case $P in
    linux-*) tar -czf $OUT/mdk-$P.tar.gz -C $S $EXE $LIB $ICON ;;
    *) (cd $S && rm -f ../mdk-$P.zip && zip -q ../mdk-$P.zip $EXE $LIB $ICON) ;;
  esac
done

# The Android APK (built separately in android/), if there is one.
APK=$(find android -path '*/build/outputs/apk/*' -name '*.apk' 2>/dev/null | sort | tail -n 1)
if [ -n "$APK" ]; then
  cp "$APK" $OUT/mdk-android.apk
fi

ls -la $OUT
