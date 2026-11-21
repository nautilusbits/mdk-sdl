#!/bin/sh
# Allocations while playing: a level is loaded once (everything on the GPU and in memory), then its
# frames allocate (almost) nothing. Each run measures the frames after a warm-up (--perf) and fails
# when they allocate more than MDK_ALLOC_MAX bytes a frame on average (512) or collect gen 1 or 2.
# The scenes: LEVEL4 MEAT_5 standing (both looks, the enhanced one with 4x anti-aliasing), LEVEL7
# walking and firing, sniper mode.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/alloc_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings and saves, not the player's.
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk-sdl}
SHOT=${TMPDIR:-/tmp}/mdk_alloc.bmp
MAX=${MDK_ALLOC_MAX:-512}
MEAT5="--level=4 --delay=0.5 --teleport=MEAT_5,-288,13559,-1989"
FAILED=0

# run <name> <options>: one run; its "Perf:" lines checked and printed.
run() {
	NAME=$1
	shift
	OUT=$(timeout 120 "$MDK" --mute --screenshot="$SHOT" "$@" 2>&1)
	BYTES=$(echo "$OUT" | sed -n 's/^Perf: allocated \([0-9]*\) B\/frame.*/\1/p')
	COLLECTIONS=$(echo "$OUT" | sed -n 's/^Perf: allocated .*collections \([0-9/]*\) .*/\1/p')
	LATE=$(echo "$COLLECTIONS" | cut -d/ -f2-)
	echo "$OUT" | grep "^Perf:" | sed 's/^/    /'
	if [ -z "$BYTES" ]; then
		echo "  $NAME: FAILED (no measurement)"
		FAILED=1
	elif [ "$BYTES" -gt "$MAX" ] || [ "$LATE" != "0/0" ]; then
		echo "  $NAME: FAILED ($BYTES B/frame, collections $COLLECTIONS; at most $MAX B/frame, no gen 1-2)"
		FAILED=1
	else
		echo "  $NAME: ok ($BYTES B/frame, collections $COLLECTIONS)"
	fi
}

run "LEVEL4 MEAT_5, original" $MEAT5 --original --perf=5 --wait=25
echo "antialiasing=X4" > "$MDK_USER_DIR/settings.cfg"
run "LEVEL4 MEAT_5, enhanced 4x" $MEAT5 --enhanced --perf=5 --wait=25
rm -f "$MDK_USER_DIR/settings.cfg"
run "LEVEL7 walking, firing" --level=7 --walk=4 --fire --perf=10 --wait=40
run "LEVEL7 sniper mode" --level=7 --sniper=0.5,5 --perf=5 --wait=20

rm -rf "$MDK_USER_DIR"
[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
