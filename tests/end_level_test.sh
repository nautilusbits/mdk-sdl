#!/bin/sh
# The end of a level (end_level.gd): special_event 1 at 1 s on LEVEL7. The arena's triangles are
# torn off (0-7 a tick) and fly up; Kurt rises 0.025 per tick² until 3 per tick (after 121 ticks,
# about 5 s), then the view tilts up to (-60 - the arena's pitch 0) / 2 = -30° at 22.5°/s, then the
# screen goes white (tests/flow_test.sh checks what follows).
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/end_level_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk-sdl}
SHOT=${TMPDIR:-/tmp}/mdk_end_level.bmp
FAILED=0

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

OUT=$(timeout 120 "$MDK" --mute --level=7 --event=1 --profile --screenshot="$SHOT" --wait=7 2>&1)
echo "$OUT" | grep "^end of level"
LINE4=$(echo "$OUT" | grep "^end of level" | sed -n 4p)
LINE7=$(echo "$OUT" | grep "^end of level" | tail -1)
echo "$LINE4" | grep -q "rise 2.25, tilt 0.0, flash 0"; check "rising slowly at 4 s, no tilt" $?
echo "$LINE7" | grep -q "tilt -30.0, flash [1-9]"; check "tilted up, flashing at 7 s" $?
TORN=$(echo "$LINE7" | sed 's/.*torn \([0-9]*\),.*/\1/')
[ "$TORN" -gt 300 ]; check "triangles torn off" $?
echo "$OUT" | grep "^Kurt at"

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
