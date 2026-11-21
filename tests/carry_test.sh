#!/bin/sh
# What carries Kurt is drawn with him, frame after frame (playtest: "a ghost" on a moving platform
# and in the XD2). The objects move each tick (30/s), Kurt each step (60/s); the camera must follow
# Kurt after the platform carried him (game_frame: objects, then camera), and a ride must sit where
# Kurt is every frame. --trace prints each frame's Kurt, camera and carrier.
# - LEVEL6's lift XTR_1000 (OLYM_8): Kurt rides it; his place in the view never jumps.
# - LEVEL7's XD2: the walker is drawn where Kurt is.
# - LEVEL7's XE: Kurt (and the view) is where the bomber is.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/carry_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk-sdl}
SHOT=${TMPDIR:-/tmp}/mdk_carry.bmp
FAILED=0

run() {
	timeout 120 "$MDK" --mute --trace --screenshot="$SHOT" "$@" 2>&1
}

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

# Frames on a carrier of that type ("trace t kurt x y z camera x y z on TYPE x y z").
frames() {
	echo "$1" | grep "^trace .* on $2 "
}

# The largest distance between the carrier and Kurt in a frame, once on (the first frames board it).
apart() {
	frames "$1" "$2" | awk 'NR > 2 { d = sqrt(($13 - $4)^2 + ($14 - $5)^2 + ($15 - $6)^2); if (d > m) m = d }
		END { printf "%.2f", m }'
}

# The largest change, from a frame to the next, of where Kurt is from the camera.
jump() {
	frames "$1" "$2" | awk '{ x = $4 - $8; y = $5 - $9; z = $6 - $10
		if (n++) { d = sqrt((x - px)^2 + (y - py)^2 + (z - pz)^2); if (d > m) m = d }
		px = x; py = y; pz = z }
		END { printf "%.2f", m }'
}

# The lift moves 25 units a tick at first: a lagging camera jumps by that much.
OUT=$(run --level=6 --delay=0.534 --teleport=OLYM_8,-1658,3381,-1869.9 --wait=4)
JUMP=$(jump "$OUT" XTR)
echo "  lift: $(frames "$OUT" XTR | wc -l) frames, Kurt jumps $JUMP in the view"
[ "$(frames "$OUT" XTR | wc -l)" -gt 100 ]; check "Kurt rides the lift" $?
awk "BEGIN { exit !($JUMP < 1) }"; check "Kurt stays put in the view on the lift" $?

OUT=$(run --level=7 --delay=0.5 --at=46,4807,-25,270 --ride=XD2 --walk=2 --wait=2)
APART=$(apart "$OUT" XD2)
echo "  XD2: $(frames "$OUT" XD2 | wc -l) frames, $APART from Kurt"
[ "$(frames "$OUT" XD2 | wc -l)" -gt 60 ]; check "Kurt rides the XD2" $?
awk "BEGIN { exit !($APART < 0.01) }"; check "the XD2 is drawn where Kurt is" $?

OUT=$(run --level=7 --delay=0.5 --teleport=DANT_5,100,2320,-60 --bomber --wait=12)
APART=$(apart "$OUT" XE)
echo "  XE: $(frames "$OUT" XE | wc -l) frames, $APART from Kurt"
[ "$(frames "$OUT" XE | wc -l)" -gt 60 ]; check "Kurt rides the XE" $?
awk "BEGIN { exit !($APART < 0.01) }"; check "Kurt is where the XE is" $?

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
