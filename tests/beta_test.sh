#!/bin/sh
# The 1996 beta demo's levels (961, 963, 966; godot-mdk docs/beta96.md): each loads, Kurt lands,
# its BSP nodes hold their triangles, every opcode is run; its menu page, a teleport and a roll.
# Skipped without the demo (MDK_BETA_DIR, or beta in mdk_paths.cfg next to the program).
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/beta_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk}
SHOT=${TMPDIR:-/tmp}/mdk_beta.bmp
FAILED=0

run() {
	timeout 120 "$MDK" --mute --screenshot="$SHOT" "$@" 2>&1
}

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

OUT=$(run --level=961 --wait=1)
if echo "$OUT" | grep -q "beta demo not found"; then
	echo "SKIPPED (the 1996 beta demo wasn't found; set MDK_BETA_DIR)"
	exit 0
fi

for LEVEL in 961 963 966; do
	OUT=$(run --level=$LEVEL --profile --wait=4)
	echo "$OUT" | grep "^Level\|^Kurt at"
	echo "$OUT" | grep -q "^Level $LEVEL: "; check "$LEVEL loads" $?
	! echo "$OUT" | grep -q "Exception"; check "$LEVEL runs without exceptions" $?
	! echo "$OUT" | grep -q "BSP nodes don't hold"; check "$LEVEL's BSP nodes are read right" $?
	! echo "$OUT" | grep -q "unimplemented opcodes"; check "$LEVEL runs every opcode" $?
	echo "$OUT" | grep -q "^Kurt at .* floor True"; check "$LEVEL: Kurt stands" $?
done

OUT=$(run --menu --beta-levels --wait=1)
echo "$OUT" | grep -q "^Menu: Beta Levels | 96 Level 1: City | 96 Level 3: Wheel Boss | 96 Level 6: Olympus | Back$"; check "the Beta Levels page" $?

OUT=$(run --level=961 --delay=1 --beta-teleport=4 --wait=3)
echo "$OUT" | grep -q "^Teleport 4: ARENA_4"; check "teleport 4: the top of ARENA_4" $?
echo "$OUT" | grep -q "^Kurt at .* arena ARENA_4"; check "Kurt is in ARENA_4" $?

OUT=$(run --level=963 --delay=2 --roll=left --wait=2.1)
echo "$OUT" | grep -q "^Kurt at .* RollLeft"; check "Z rolls Kurt left" $?

exit $FAILED
