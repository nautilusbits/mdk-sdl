#!/bin/sh
# Frames between the fixed steps (FrameBlend): --fps=240 draws four frames a step, --trace prints
# each frame's Kurt, camera and carrier as drawn ("trace t kurt x y z camera x y z [on TYPE x y z]").
# - LEVEL6's lift (25 a tick): Kurt lands on it as it moves and rides it; he stays put on it and
#   never steps back (his fall missed the lift every other step: he jumped 25 a tick).
# - LEVEL7's XD2, LEVEL7's XE, LEVEL4's snowboard: Kurt moves smoothly, the ride with him (boarding
#   the XE or the board, he stepped back).
# - A console tp and a cutscene cut are drawn at once, never on the way.
# - Issue #16: the objects' animations (LEVEL3's alien and grunt, the decoy) change every frame,
#   not every 8th ("blend pose t TYPE_ID x y z": the middle of the pose as drawn); so do the
#   pieces and the slime of a sniper grenade's kill, and the chain gun's sparks.
# - Sniper mode, looking up: the zoom, the scope's tilt and the clip change every frame, not
#   every 4th; the round cameras (the views at the top) every frame, not every 8th.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/interpolation_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk-sdl}
SHOT=${TMPDIR:-/tmp}/mdk_interpolation.bmp
FAILED=0

run() {
	timeout 180 "$MDK" --mute --trace --fps=240 --screenshot="$SHOT" "$@" 2>&1 | grep "^trace"
}

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

less() {
	awk "BEGIN { exit !($1 < $2) }"
}

# What --trace prints besides Kurt: "blend WHAT t ..." lines.
details() {
	timeout 180 "$MDK" --mute --trace --fps=240 --screenshot="$SHOT" "$@" 2>&1 | grep "^blend $1 " | cut -d' ' -f3-
}

run_all() {
	timeout 180 "$MDK" --mute --trace --fps=240 --screenshot="$SHOT" "$@" 2>&1 | grep "^blend "
}

# How many changes a series makes, frame to frame ("t id x y z"), and the share of them that
# follow a change: drawn every frame 1, every 4th (60 Hz) or 8th (30 Hz) 0.
smooth() {
	echo "$1" | awk '{ v = $3 " " $4 " " $5; c = ($2 in last) && last[$2] != v
		if (c) { n++; if (moved[$2]) s++ }
		moved[$2] = c; last[$2] = v }
		END { printf "%d %.2f", n, n ? s / n : 0 }'
}

# Frames on a carrier of that type.
on() {
	echo "$1" | grep " on $2 "
}

# Frames drawn between steps: Kurt moved since the frame before, at the same step.
blended() {
	echo "$1" | awk '{ if ($2 == t && ($4 != x || $5 != y || $6 != z)) n++; t = $2; x = $4; y = $5; z = $6 }
		END { print n + 0 }'
}

# The largest change, from a frame to the next, of Kurt's place on what carries him.
drift() {
	echo "$1" | awk '{ x = $4 - $13; y = $5 - $14; z = $6 - $15
		if (n++) { d = sqrt((x - px)^2 + (y - py)^2 + (z - pz)^2); if (d > m) m = d }
		px = x; py = y; pz = z }
		END { printf "%.2f", m }'
}

# Frames where Kurt (field 4) or the camera (field 8) turns back (moves of over 0.5 against the
# last), and the largest change of a frame's move from the last's, jumps aside (a stutter).
backs() {
	echo "$1" | awk -v f="$2" '{ x = $f; y = $(f + 1); z = $(f + 2)
		if (n++) { dx = x - px; dy = y - py; dz = z - pz; d = sqrt(dx^2 + dy^2 + dz^2)
			if (d > 0.5 && pd > 0.5 && dx * pdx + dy * pdy + dz * pdz < 0) b++
			pdx = dx; pdy = dy; pdz = dz; pd = d }
		px = x; py = y; pz = z }
		END { print b + 0 }'
}

stutter() {
	echo "$1" | awk -v f="$2" '{ x = $f; y = $(f + 1); z = $(f + 2)
		if (n++) { d = sqrt((x - px)^2 + (y - py)^2 + (z - pz)^2)
			if (n > 2 && d < 50 && pd < 50) { j = d > pd ? d - pd : pd - d; if (j > m) m = j }
			pd = d }
		px = x; py = y; pz = z }
		END { printf "%.2f", m }'
}

# Frames moving Kurt (field 4) or the camera (field 8) more than 50 (a jump: 1500/s a tick), and
# less than that but more than 10 (on the way of a jump).
jumps() {
	echo "$1" | awk -v f="$2" '{ x = $f; y = $(f + 1); z = $(f + 2)
		if (n++) { d = sqrt((x - px)^2 + (y - py)^2 + (z - pz)^2); if (d > 50) j++; else if (d > 10) w++ }
		px = x; py = y; pz = z }
		END { print j + 0, w + 0 }'
}

OUT=$(run --level=6 --delay=0.534 --teleport=OLYM_8,-1658,3381,-1869.9 --wait=3)
LIFT=$(on "$OUT" XTR)
# Every frame from the teleport: some steps miss the lift.
AFTER=$(echo "$OUT" | awk '$2 > 0.55')
echo "  lift: $(echo "$LIFT" | wc -l) frames, $(blended "$LIFT") blended, drift $(drift "$LIFT"), back $(backs "$AFTER" 4), stutter $(stutter "$AFTER" 4)"
[ "$(blended "$LIFT")" -gt 300 ]; check "frames are drawn between the steps" $?
less "$(drift "$LIFT")" 0.5; check "Kurt stays put on the lift" $?
[ "$(backs "$AFTER" 4)" = 0 ] && [ "$(backs "$AFTER" 8)" = 0 ]; check "Kurt and the camera never step back" $?
less "$(stutter "$AFTER" 4)" 4; check "Kurt moves smoothly with the lift" $?

OUT=$(run --level=7 --delay=0.5 --at=46,4807,-25,270 --ride=XD2 --walk=2 --wait=2)
RIDE=$(on "$OUT" XD2)
echo "  XD2: $(echo "$RIDE" | wc -l) frames, $(blended "$RIDE") blended, drift $(drift "$RIDE"), stutter $(stutter "$RIDE" 4)"
[ "$(blended "$RIDE")" -gt 100 ] && less "$(drift "$RIDE")" 0.01 && less "$(stutter "$RIDE" 4)" 0.3; check "the XD2 walks smoothly with Kurt" $?

OUT=$(run --level=7 --delay=0.5 --teleport=DANT_5,100,2320,-60 --bomber --wait=12)
RIDE=$(on "$OUT" XE)
# Once on board (the first frames board it).
FLY=$(echo "$RIDE" | tail -n +5)
echo "  XE: $(echo "$RIDE" | wc -l) frames, drift $(drift "$FLY"), back $(backs "$RIDE" 4)"
less "$(drift "$FLY")" 0.01 && [ "$(backs "$RIDE" 4)" = 0 ]; check "Kurt boards and flies with the XE" $?

OUT=$(run --level=4 --delay=0.5 --teleport=CMEAT_3,420,4285,-570 --wait=4)
RIDE=$(echo "$OUT" | awk '$2 > 1')
echo "  snowboard: $(blended "$RIDE") blended, stutter $(stutter "$RIDE" 4)"
[ "$(blended "$RIDE")" -gt 100 ] && less "$(stutter "$RIDE" 4)" 0.2; check "Kurt boards smoothly" $?

# After the first frames (the camera starts at the origin).
OUT=$(run --level=3 --delay=0.5 --console="tp HMO_4" --wait=1 | awk '$2 > 0.1')
echo "  tp: Kurt's jumps and moves on the way $(jumps "$OUT" 4)"
[ "$(jumps "$OUT" 4)" = "1 0" ]; check "a teleport is drawn at once" $?

OUT=$(run --level=5 --at=440,84,-2250,163 --event=52 --wait=2 | awk '$2 > 0.1')
echo "  cutscene: the camera's jumps and moves on the way $(jumps "$OUT" 8), stutter $(stutter "$OUT" 8)"
[ "$(jumps "$OUT" 8)" = "1 0" ]; check "a cut is drawn at once" $?
less "$(stutter "$OUT" 8)" 2; check "the cutscene camera moves smoothly" $?

OUT=$(details pose --level=3 --at=0,120,150,75.5 --wait=2)
set -- $(smooth "$OUT")
echo "  animations: $1 changes, $2 after a change"
[ "$1" -gt 300 ] && less 0.9 "$2"; check "animations are drawn between their frames" $?

OUT=$(details pose --level=3 --at=0,120,150,75.5 --give=SW_DUMMY --use --wait=4 | grep SW_DUMMY)
set -- $(smooth "$OUT")
echo "  decoy: $1 changes, $2 after a change"
[ "$1" -gt 30 ] && less 0.9 "$2"; check "the decoy walks smoothly" $?

OUT=$(run_all --level=3 --at=0,120,150,75.5 --sniper=1,9 --sniper-fire --give=SW_SGREN --wait=4)
for WHAT in piece effect; do
	set -- $(smooth "$(echo "$OUT" | grep "^blend $WHAT " | cut -d' ' -f3-)")
	echo "  grenade's ${WHAT}s: $1 changes, $2 after a change"
	[ "$1" -gt 200 ] && less 0.9 "$2"; check "the grenade's ${WHAT}s fly smoothly" $?
done

OUT=$(details piece --level=3 --at=0,120,150,75.5 --fire --wait=4)
set -- $(smooth "$OUT")
echo "  sparks: $1 changes, $2 after a change"
[ "$1" -gt 1000 ] && less 0.9 "$2"; check "the chain gun's sparks fly smoothly" $?

OUT=$(run_all --level=3 --at=0,120,150,75.5 --sniper=1,-30 --zoom=1 --sniper-fire --wait=3)
for WHAT in scope round; do
	set -- $(smooth "$(echo "$OUT" | grep "^blend $WHAT " | cut -d' ' -f3-)")
	echo "  sniper's ${WHAT}: $1 changes, $2 after a change"
	[ "$1" -gt 30 ] && less 0.9 "$2"; check "the sniper's ${WHAT} moves smoothly" $?
done

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
