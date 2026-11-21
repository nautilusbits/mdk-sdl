#!/bin/sh
# The stream between levels (--stream=N): its tube (Watcom's rand(), seed 1), Kurt's flight up to
# his first wall hit, Bones' rescue, the Gunter tube after LEVEL8 and Kurt's death in it.
# The expected numbers come from the Godot port (game/stream/stream.gd, stream_tube.gd) run headless
# at 60 fps with its tube's rand() replaced by Watcom's.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/stream_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk-sdl}
OUT_DIR=${TMPDIR:-/tmp}
SHOT=$OUT_DIR/mdk_stream.bmp
FAILED=0

run() {
	timeout 180 "$MDK" --mute --screenshot="$SHOT" "$@" 2>&1
}

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

# After LEVEL7 (index 0, normal): Bones comes after segment 177, then the statistics.
OUT=$(run --stream=7 --wait=37)
echo "$OUT" | grep "^Stream\|^Statistics"
echo "$OUT" | grep -q "^Stream after LEVEL7: Normal, turn 8, radius 10-17, 27 lights, ring 30 at -79.963 249.386 101.282$"; check "tube as Godot's" $?
echo "$OUT" | grep -q "^Stream: first wall hit at segment 43, t 44.350 x 6.072 z -5.636 yaw 122.98 speed 5.400 "; check "first wall hit as Godot's" $?
echo "$OUT" | grep -q "^Stream: Bones comes at segment 178,"; check "Bones' rescue" $?
echo "$OUT" | grep -q "^Stream ended after LEVEL7: Survived,"; check "stream ends" $?
echo "$OUT" | grep -q "^Statistics: Intermission$"; check "statistics after the stream" $?
[ -f "$SHOT" ]; check "screenshot" $?

# After LEVEL3 (index 2): a narrower, twistier tube.
OUT=$(run --stream=3 --wait=8)
echo "$OUT" | grep -q "^Stream after LEVEL3: Normal, turn 10, radius 9-15, 27 lights, "; check "LEVEL3 limits" $?
echo "$OUT" | grep -q "^Stream: first wall hit at segment 42, t 43.050 x 5.320 "; check "LEVEL3 first wall hit as Godot's" $?

# After LEVEL8 (index 4): the Gunter tube, straight at its end, then LEVEL5's save prompt.
OUT=$(run --stream=8 --wait=36)
echo "$OUT" | grep "^Stream"
echo "$OUT" | grep -q "^Stream after LEVEL8: Gunter, turn 12, radius 8-13, 27 lights, "; check "Gunter tube" $?
echo "$OUT" | grep -q "^Stream: first wall hit at segment 40, t 41.050 x 4.689 z -4.866 yaw 121.23 speed 5.400 "; check "Gunter first wall hit as Godot's" $?
echo "$OUT" | grep -q "^Stream ended after LEVEL8: Survived, health [0-9]*, segment 184,"; check "Gunter tube ends" $?

# With 1 health the first wall hit kills Kurt in the Gunter tube: the main menu.
OUT=$(run --stream=8 --health=1 --wait=12)
echo "$OUT" | grep "^Stream\|^Menu"
echo "$OUT" | grep -q "^Stream: Kurt died at segment 40,"; check "death in the Gunter tube" $?
echo "$OUT" | grep -q "^Menu:"; check "menu after death" $?

# Then red is added to the scene as it darkens (0x4352ac): not one flat colour.
rm -f "$SHOT"
run --stream=8 --health=1 --wait=7.9 > /dev/null
VALUES=$(tail -c +55 "$SHOT" 2>/dev/null | od -An -v -tu1 | tr -s ' ' '\n' | sort -u | wc -l)
[ "$VALUES" -ge 16 ]; check "red added to the dying scene" $?

rm -f "$SHOT"
[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
