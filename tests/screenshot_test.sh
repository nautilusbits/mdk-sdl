#!/bin/sh
# Smoke test: every level renders a frame that isn't one flat colour.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/screenshot_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk}
OUT=${TMPDIR:-/tmp}
FAILED=0
for LEVEL in 3 4 5 6 7 8; do
	FILE="$OUT/mdk_level$LEVEL.bmp"
	rm -f "$FILE"
	timeout 60 "$MDK" --level=$LEVEL --mute --screenshot="$FILE" > /dev/null 2>&1
	# Distinct byte values in the pixel data (after the 54-byte header).
	VALUES=$(tail -c +55 "$FILE" 2>/dev/null | od -An -v -tu1 | tr -s ' ' '\n' | sort -u | wc -l)
	if [ "$VALUES" -lt 16 ]; then
		echo "level $LEVEL: FAILED ($VALUES distinct values)"
		FAILED=1
	else
		echo "level $LEVEL: ok"
	fi
done
# --frames: 3 frames, every 2nd, then the game quits.
FRAMES="$(mktemp -d)"
timeout 60 "$MDK" --level=3 --mute --wait=1 --frames="$FRAMES,3,2" > /dev/null 2>&1
if [ "$(ls "$FRAMES" | wc -l)" -ne 3 ] || [ ! -s "$FRAMES/frame002.bmp" ]; then
	echo "frames: FAILED"
	FAILED=1
else
	echo "frames: ok"
fi
rm -rf "$FRAMES"
[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
