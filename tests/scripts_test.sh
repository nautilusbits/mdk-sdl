#!/bin/sh
# Level scripts: objects spawn, animate, move and doors open, close to the Godot port's --profile
# output (references below, taken from godot-mdk at the same game time, about 3 ticks apart).
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/scripts_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk-sdl}
SHOT=${TMPDIR:-/tmp}/mdk_scripts.bmp
FAILED=0

run() {
	timeout 60 "$MDK" --mute --profile --screenshot="$SHOT" "$@" 2>&1
}

# The line of an object at a profile second: profile <output> <second> <object>.
profile() {
	echo "$1" | sed -n "/^Profile $2s:/,/^Profile/p" | grep "^  $3 "
}

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

# Field n of a profile line (1 name, 2 arena, 3-5 x y z, 15 frame), without brackets and commas.
field() {
	echo "$1" | sed 's/^ *//' | tr -d '(),' | cut -d' ' -f"$2"
}

# Level 7's start: 7 objects (door, beams, turbines, a pickup); the turbines turn (animation).
OUT=$(run --level=7 --wait=3)
echo "$OUT" | grep "^Profile 3s"
COUNT=$(echo "$OUT" | grep "^Profile 3s" | sed 's/.*objects \([0-9]*\),.*/\1/')
[ "${COUNT:-0}" -ge 7 ]; check "level 7 spawns its objects" $?
A=$(profile "$OUT" 1 XTURBINE_1); B=$(profile "$OUT" 2 XTURBINE_1)
echo "$A"; echo "$B"
[ -n "$A" ] && [ "$(field "$A" 15)" != "$(field "$B" 15)" ]; check "turbines animate" $?
H=$(profile "$OUT" 3 SW_H150_1002)
echo "$H"
echo "$H" | grep -q "(-11.0, -13.0, 0.0) yaw 0 "; check "runner pickup stays put (Godot: -11, -13, 0)" $?

# Level 3: the XF flies its path, pickups fall with chutes (Godot at 5 s: XF -41 557 144, bones z 272).
OUT=$(run --level=3 --wait=5)
XF=$(profile "$OUT" 5 XF_1004); BONES=$(profile "$OUT" 5 SW_BONES_1000)
echo "$XF"; echo "$BONES"
X=$(field "$XF" 3); Y=$(field "$XF" 4); Z=$(field "$XF" 5)
awk "BEGIN { exit !(($X + 41) ^ 2 + ($Y - 557) ^ 2 + ($Z - 144) ^ 2 < 400) }"; check "XF follows its path" $?
Z=$(field "$BONES" 5)
awk "BEGIN { exit !(($Z - 272) ^ 2 < 25) }"; check "pickup falls with its chute" $?

# Level 7's first door opens when Kurt comes near and shows the corridor behind it.
OUT=$(run --level=7 --at=0,470,16,90 --wait=3)
DOOR=$(profile "$OUT" 3 X7DOOR_1000)
echo "$DOOR"
echo "$DOOR" | grep -q "door 51$"; check "door opens" $?
echo "$OUT" | grep "^Profile 3s" | grep -q "second CDANT_1 (active)"; check "corridor behind the door shown" $?

# Level 4: the end of corridor CMEAT_6 (a box at y 13585-13590) lifts Kurt into MEAT_7, 1971 higher
# (teleport_player_keep, opcode 173: his place plus 130, 845, 1971).
OUT=$(run --level=4 --at=-125,13620,-1975,270 --delay=0.5 --walk=2 --wait=4)
echo "$OUT" | grep "^Kurt at"
echo "$OUT" | grep "^Kurt at" | grep -q " arena MEAT_7 "; check "corridor lifts Kurt into MEAT_7" $?

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
