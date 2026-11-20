#!/bin/sh
# The fall before a level (--fall=N): its difficulty values and pickups for each level, and a whole
# fall with "forward" held for a second (--walk=1 --delay=1): Kurt reaches the top limit, is pulled
# back to the centre after 30 s while the camera brakes, lands at 33 s, and the level starts with
# his health and pickups. Expected numbers from the Godot port (game/fall/fall.gd, run headless
# with a probe printing the same values; --fixed-fps 60).
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/fall_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
MDK=${MDK:-build/linux-x64/mdk}
OUT_DIR=${TMPDIR:-/tmp}
SHOT=$OUT_DIR/mdk_fall.bmp
MDK_USER_DIR=$OUT_DIR/mdk_fall_user
export MDK_USER_DIR
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

# Difficulty: 0 easy, 1 normal, 2 hard (settings.cfg).
difficulty() {
	rm -rf "$MDK_USER_DIR"
	mkdir -p "$MDK_USER_DIR"
	echo "difficulty=$1" > "$MDK_USER_DIR/settings.cfg"
}

header() {
	OUT=$(run --fall="$1" --wait=0.1)
	echo "$OUT" | grep "^Fall:"
	echo "$OUT" | grep -qx "$2"; check "LEVEL$1 fall, $3" $?
}

difficulty Normal
header 7 "Fall: index 0 difficulty 1 beam 117.65 missiles 2 spread 6.50 missile gap 32 radar gap 63 pickups SW_HOME,SW_GATT,SW_HOME" normal
header 6 "Fall: index 1 difficulty 1 beam 141.18 missiles 2 spread 5.50 missile gap 29 radar gap 56 pickups SW_GATT,SW_HBOMB,SW_SGREN,SW_HOME" normal
header 8 "Fall: index 4 difficulty 1 beam 211.76 missiles 3 spread 2.50 missile gap 20 radar gap 35 pickups SW_GATT,SW_HBOMB,SW_SGREN,SW_HOME" normal
difficulty Easy
header 8 "Fall: index 4 difficulty 0 beam 164.71 missiles 2 spread 3.50 missile gap 28 radar gap 51 pickups SW_GATT,SW_HBOMB,SW_SGREN,SW_HOME" easy
difficulty Hard
header 8 "Fall: index 4 difficulty 2 beam 274.51 missiles 4 spread 1.50 missile gap 12 radar gap 27 pickups SW_GATT,SW_HBOMB,SW_SGREN,SW_HOME" hard

# The whole fall (5 s of intro, 33 s of fall), then the level.
difficulty Normal
OUT=$(run --fall=7 --profile --walk=1 --delay=1 --wait=40)
echo "$OUT" | grep "^Fall t \(2\|30\|31\|32\)\.0 \|^Fall ended\|^Level starts"

# Godot: "Fall t 2.0 kurt 0.00 35.29 5135.5 camera 5145.5 wind 12".
echo "$OUT" | grep -q "^Fall t 2.0 kurt 0.00 35.29 513[5-7]\.[0-9] camera 514[5-7]\.[0-9] wind 12 "; check "steers to the top limit" $?
# Godot: "Fall t 31.0 kurt 0.00 4.09 3202.0 camera 3229.5 wind 5".
echo "$OUT" | grep -q "^Fall t 31.0 kurt 0.00 4.09 320[1-3]\.[0-9] camera 3229.5 wind 5 "; check "pulled back, camera brakes" $?
# Godot: "Fall t 32.0 kurt 0.00 0.43 3135.3 camera 3213.7 wind 0".
echo "$OUT" | grep -q "^Fall t 32.0 kurt 0.00 0.43 313[4-6]\.[0-9] camera 3213.7 wind 0 "; check "camera stopped, no wind" $?
# Godot: "Fall ended t 33.0 kurt 0.00 0.05 3068.7 camera 3213.7 wind 0".
echo "$OUT" | grep -q "^Fall ended t 33.0 kurt 0.00 0.05 30[67][0-9]\.[0-9] camera 3213.7 wind 0 health [1-9][0-9]* "; check "lands at 33 s" $?

# The level keeps the fall's health and pickups (the ones of LEVEL7's fall).
HEALTH=$(echo "$OUT" | sed -n 's/^Fall ended .* health \([0-9]*\) pickups.*/\1/p')
PICKUPS=$(echo "$OUT" | sed -n 's/^Fall ended .* pickups \(.*\)$/\1/p')
echo "$OUT" | grep -qx "Level starts with the fall's health $HEALTH and pickups $PICKUPS"; check "level keeps health $HEALTH, pickups '$PICKUPS'" $?
echo "$PICKUPS" | grep -qx "\(\(SW_HOME\|SW_GATT\),\?\)*"; check "pickups of FALLPU_1" $?
echo "$OUT" | grep -q "^Level 7:"; check "level 7 follows" $?

# Esc opens the pause menu, as in a level (the original's abortcon.c, 0x403cf8); the fall waits
# under it for 2 s and goes on after Esc again (#24). The intro takes 5 s.
OUT=$(run --fall=7 --profile --press=Menu.Back@7,Menu.Back@9 --wait=12)
echo "$OUT" | grep -qx "Menu: Resume | Options | Main menu | Quit"; check "Esc: pause menu" $?
! echo "$OUT" | grep -q "^Fall ended"; check "Esc: no skip" $?
LAST=$(echo "$OUT" | sed -n 's/^Fall t \([0-9]*\)\.0 .*/\1/p' | tail -n 1)
[ "$LAST" -le 5 ]; check "fall waits under the menu (last t $LAST)" $?

rm -rf "$MDK_USER_DIR"
[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
