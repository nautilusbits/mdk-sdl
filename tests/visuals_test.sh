#!/bin/sh
# Music, sky, animated textures, outlines, ropes, twisters and the cutscene camera, from --profile.
# The Godot port (--level=3 --profile=4, with and without --at=-19,654,108 in the corridor CHMO_1):
# one music track at -13dB after 4 s. Its _add_outlines builds 705 outlines for LEVEL3.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/visuals_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk}
SHOT=${TMPDIR:-/tmp}/mdk_visuals.bmp
FAILED=0

# run <level> <options>
run() {
	LEVEL=$1
	shift
	timeout 60 "$MDK" --level="$LEVEL" --mute --profile --screenshot="$SHOT" "$@" 2>&1
}

# The lines of a profile second: profile <output> <second>.
profile() {
	echo "$1" | sed -n "/^Profile $2s:/,/^Profile/p"
}

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

OUT=$(run 3 --wait=4)
echo "$OUT" | grep -q "^Level 3: .* 705 outlines"; check "LEVEL3's outlines as Godot's" $?
profile "$OUT" 4 | grep -q "^  music H1 -13dB"; check "HMO_1's track fades in as Godot's" $?
profile "$OUT" 4 | grep -q "^  sky 0 (Sky)"; check "the sky is shown" $?

OUT=$(run 3 --at=-19,654,108 --wait=4)
profile "$OUT" 4 | grep -q "^  music CORRIDOR -13dB, fading -"; check "a corridor plays CORRIDOR" $?

OUT=$(run 3 --delay=0.5 --teleport=HMO_3,-49,1420,-965 --wait=3)
profile "$OUT" 3 | grep -q "^  sky -1 (Keep)"; check "HMO_3 keeps the last frame" $?
profile "$OUT" 3 | grep -q "^  music H3 .*fading -"; check "HMO_3's track replaces HMO_1's" $?

# DANT_5's M_COMM: 6 frames a second, so whole seconds wrap to the same frame.
OUT=$(run 7 --delay=0.5 --teleport=DANT_5,100,2320,-60 --wait=2.5)
profile "$OUT" 2 | grep -q "^  texture DANT_5 M_COMM frame"; check "M_COMM animates" $?

OUT=$(run 5 --at=440,84,-2250,163 --event=61 --wait=3)
profile "$OUT" 3 | grep -q "^  rope XBN_1 lines 4"; check "the dog's four chains" $?
profile "$OUT" 3 | grep -q "^  cutscene 3d shot 11 camera .* pitch -20"; check "Gunter's cutscene camera" $?

OUT=$(run 3 --at=0,120,150,90 --give=SW_TWIST --use --wait=5)
profile "$OUT" 5 | grep -q "^  twisters [1-9][0-9]*, ribbon triangles [1-9]"; check "twisters draw ribbons" $?

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
