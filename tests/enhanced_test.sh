#!/bin/sh
# The enhanced look: levels 3, 6 and 7 look different in it, the original look stays the default and
# anti-aliasing changes the frame. With MDK_REFERENCE=<main's mdk>, the original look must also be
# identical to that build's, pixel for pixel (MDK_DATA_DIR if it is outside the game's folder).
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/enhanced_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
MDK=${MDK:-build/linux-x64/mdk}
OUT=${TMPDIR:-/tmp}/mdk_enhanced
FAILED=0

# Fresh settings (the defaults), and anti-aliasing 4x.
rm -rf "$OUT"
mkdir -p "$OUT/defaults" "$OUT/msaa"
echo "antialiasing=X4" > "$OUT/msaa/settings.cfg"

# shot <mdk> <settings folder> <file> <options>
shot() {
	BIN=$1
	USER_DIR=$2
	FILE=$3
	shift 3
	MDK_USER_DIR="$USER_DIR" timeout 60 "$BIN" --mute --wait=2 --screenshot="$FILE" "$@" > /dev/null 2>&1
	[ -s "$FILE" ]
}

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

for LEVEL in 3 6 7; do
	echo "level $LEVEL"
	shot "$MDK" "$OUT/defaults" "$OUT/default_$LEVEL.bmp" --level=$LEVEL; check "default screenshot" $?
	shot "$MDK" "$OUT/defaults" "$OUT/original_$LEVEL.bmp" --level=$LEVEL --original; check "original screenshot" $?
	shot "$MDK" "$OUT/defaults" "$OUT/enhanced_$LEVEL.bmp" --level=$LEVEL --enhanced; check "enhanced screenshot" $?
	cmp -s "$OUT/default_$LEVEL.bmp" "$OUT/original_$LEVEL.bmp"; check "the original look is the default" $?
	! cmp -s "$OUT/original_$LEVEL.bmp" "$OUT/enhanced_$LEVEL.bmp"; check "the enhanced look differs" $?
	if [ -n "$MDK_REFERENCE" ]; then
		shot "$MDK_REFERENCE" "$OUT/defaults" "$OUT/reference_$LEVEL.bmp" --level=$LEVEL
		cmp -s "$OUT/reference_$LEVEL.bmp" "$OUT/original_$LEVEL.bmp"; check "the original look as the reference's" $?
	fi
done

# With MDK_HD_DIR=<a user folder with mods/hd-textures/ (mdk --upscale-textures=3)>, HD textures
# change the enhanced look.
if [ -n "$MDK_HD_DIR" ]; then
	echo "HD textures"
	mkdir -p "$OUT/hd/mods"
	cp -r "$MDK_HD_DIR/mods/hd-textures" "$OUT/hd/mods/"
	shot "$MDK" "$OUT/hd" "$OUT/hd_3.bmp" --level=3 --enhanced; check "HD screenshot" $?
	! cmp -s "$OUT/hd_3.bmp" "$OUT/enhanced_3.bmp"; check "HD textures differ" $?
	shot "$MDK" "$OUT/hd" "$OUT/hd_original_3.bmp" --level=3 --original; check "original screenshot" $?
	cmp -s "$OUT/hd_original_3.bmp" "$OUT/original_3.bmp"; check "the original look ignores them" $?
fi

echo "anti-aliasing 4x"
shot "$MDK" "$OUT/msaa" "$OUT/msaa_original.bmp" --level=3 --original; check "original screenshot" $?
shot "$MDK" "$OUT/msaa" "$OUT/msaa_enhanced.bmp" --level=3 --enhanced; check "enhanced screenshot" $?
! cmp -s "$OUT/msaa_original.bmp" "$OUT/original_3.bmp"; check "smoother edges" $?

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
