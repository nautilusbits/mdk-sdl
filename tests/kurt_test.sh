#!/bin/sh
# Kurt on level 3: he lands on the start pad, walks down the ramp where the Godot port's Kurt goes
# (-10, 57, 143 after 3 s), and a held jump opens the chute on the way down.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/kurt_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk-sdl}
SHOT=${TMPDIR:-/tmp}/mdk_kurt.bmp
FAILED=0

run() {
	timeout 60 "$MDK" --level=3 --mute --screenshot="$SHOT" "$@" 2>&1 | grep "^Kurt at"
}

OUT=$(run --wait=2)
echo "$OUT"
echo "$OUT" | grep -q " Still floor True" && echo "  stands on the pad: ok" || { echo "  stands on the pad: FAILED"; FAILED=1; }

OUT=$(run --walk=3 --wait=3)
X=$(echo "$OUT" | cut -d' ' -f3); Y=$(echo "$OUT" | cut -d' ' -f4); Z=$(echo "$OUT" | cut -d' ' -f5)
echo "$OUT"
awk "BEGIN { exit !(($X + 10) ^ 2 + ($Y - 57) ^ 2 + ($Z - 143) ^ 2 < 9) }" && echo "  walks down the ramp: ok" || { echo "  walks down the ramp: FAILED"; FAILED=1; }

OUT=$(run --jump --wait=1.2)
echo "$OUT"
echo "$OUT" | grep -q " Chute floor False" && echo "  chute: ok" || { echo "  chute: FAILED"; FAILED=1; }

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
