#!/bin/sh
# Screens of other shapes than 4:3: a 16:10 tablet (2880 x 1800) and a near-square foldable
# (2344 x 2156). The 2D screens (menu, briefing) fit whole with black bars, never cut: the bars'
# rows are black, the picture's middle row reaches both edges (16:10) or stays between the bars.
# Levels draw at those sizes.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/aspect_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
MDK=${MDK:-build/linux-x64/mdk}
OUT=${TMPDIR:-/tmp}/mdk_aspect
MDK_USER_DIR=$OUT/user
export MDK_USER_DIR
FAILED=0
# The BMP's header; rows top-down, 4 bytes a pixel.
HEADER=54

rm -rf "$OUT"
mkdir -p "$MDK_USER_DIR"

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

# shot <file> <options>
shot() {
	FILE=$1
	shift
	timeout 60 "$MDK" --mute --screenshot="$FILE" "$@" > /dev/null 2>&1
	[ -s "$FILE" ]
}

# row_sum <file> <width> <row>: the sum of a row's colour bytes.
row_sum() {
	od -An -tu1 -v -j $((HEADER + $3 * $2 * 4)) -N $(($2 * 4)) "$1" | awk '{ for (i = 1; i <= NF; i++) if ((n++ % 4) != 3) s += $i } END { print s + 0 }'
}

# lit <file> <width> <row> <column>: whether a pixel isn't black.
lit() {
	od -An -tu1 -v -j $((HEADER + ($3 * $2 + $4) * 4)) -N 3 "$1" | awk '{ exit ($1 + $2 + $3 > 0) ? 0 : 1 }'
}

for SIZE in 2344x2156 2880x1800; do
	W=${SIZE%x*}
	H=${SIZE#*x}
	echo "$SIZE"
	for SCREEN in menu briefing; do
		FILE="$OUT/${SCREEN}_$SIZE.bmp"
		if [ "$SCREEN" = menu ]; then
			shot "$FILE" --menu --wait=1 --size="$SIZE"; check "$SCREEN screenshot" $?
		else
			shot "$FILE" --briefing=3 --wait=1 --size="$SIZE"; check "$SCREEN screenshot" $?
		fi

		[ "$(row_sum "$FILE" "$W" 0)" = 0 ]; check "$SCREEN: a black bar above" $?
		[ "$(row_sum "$FILE" "$W" $((H - 1)))" = 0 ]; check "$SCREEN: a black bar below" $?
		[ "$(row_sum "$FILE" "$W" $((H / 2)))" != 0 ]; check "$SCREEN: the picture between" $?
	done

	shot "$OUT/level_$SIZE.bmp" --level=3 --wait=2 --size="$SIZE"; check "level screenshot" $?
done

# 16:10: the menu's picture is as wide as the screen (scaled to fit, not cut nor pillarboxed).
FILE="$OUT/menu_2880x1800.bmp"
lit "$FILE" 2880 900 2 || lit "$FILE" 2880 700 2 || lit "$FILE" 2880 1100 2; check "16:10 menu reaches the left edge" $?

exit $FAILED
