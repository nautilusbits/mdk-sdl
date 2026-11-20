#!/bin/sh
# Soak test: random but seeded keys (--soak=seed) on every level (3-8) from its start, a tour of
# every arena of each level (--tour), the fall and the stream of each level, the menus and the flow
# screens (random menu keys: new games, options, the pause menu...). Reports exceptions, hangs
# (timeouts), "Soak problem" lines (NaN positions, Kurt standing outside every arena or falling
# through a floor, an open door onto an undrawn arena) and level runs that don't reach their end.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/soak_test.sh [short] [seeds]
#   short: a few seconds per run (about 3 minutes); seeds: how many seeds per level run (default 1).
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
MDK=${MDK:-build/linux-x64/mdk}
OUT_DIR=${TMPDIR:-/tmp}
# Per run: soaks running at the same time must not share these files.
SHOT=$OUT_DIR/mdk_soak_$$.bmp
LOG=$OUT_DIR/mdk_soak_$$.log
MDK_USER_DIR=$OUT_DIR/mdk_soak_user
export MDK_USER_DIR
LEVELS="3 4 5 6 7 8"
FAILED=0

# Game seconds of each kind of run; a soak frame is 0.1 s of game time.
if [ "$1" = short ]; then
	STAY=20; TOUR=60; FALL=40; STREAM=35; SCREEN=3; MONKEY=60
else
	STAY=120; TOUR=300; FALL=45; STREAM=40; SCREEN=10; MONKEY=200
fi
SEEDS=${2:-1}
# Real seconds before a run counts as hung.
HANG=300

# run <name> <check: level|screen> <options...>: one run, its problems printed.
run() {
	NAME=$1; KIND=$2; shift 2
	rm -rf "$MDK_USER_DIR"
	timeout $HANG "$MDK" --mute --screenshot="$SHOT" "$@" > "$LOG" 2>&1
	CODE=$?
	PROBLEMS=$(grep -E "^Soak problem|xception|   at " "$LOG" | head -20)
	[ $CODE = 124 ] && PROBLEMS="hang (no end after $HANG s): $(tail -1 "$LOG")
$PROBLEMS"
	[ $CODE != 0 ] && [ $CODE != 124 ] && PROBLEMS="exit code $CODE
$PROBLEMS"
	if [ "$KIND" = level ] && ! grep -q "^Soak end\|^Kurt died\|^Stream after" "$LOG"; then
		PROBLEMS="no soak end: $(tail -1 "$LOG")
$PROBLEMS"
	fi

	if [ -z "$PROBLEMS" ]; then
		echo "  $NAME: ok $(grep "^Soak end" "$LOG" | cut -d: -f2)"
		return
	fi

	echo "  $NAME: FAILED"
	echo "$PROBLEMS" | sed 's/^/    /'
	FAILED=1
}

SEED=1
while [ $SEED -le "$SEEDS" ]; do
	run "menus seed $SEED" screen --menu --soak=$SEED --wait=$MONKEY
	for L in $LEVELS; do
		run "LEVEL$L seed $SEED" level --level=$L --soak=$SEED --wait=$STAY
		run "LEVEL$L tour seed $SEED" level --level=$L --soak=$SEED --tour --wait=$TOUR
	done
	SEED=$((SEED + 1))
done

for L in 3 4 6 7 8; do
	run "LEVEL$L fall" screen --fall=$L --soak --wait=$FALL
done

for L in $LEVELS; do
	run "LEVEL$L stream" screen --stream=$L --soak --wait=$STREAM
done

run "menu" screen --menu --splash --soak --wait=$SCREEN
run "options" screen --menu --options --soak --wait=$SCREEN
run "controls" screen --menu --controls --soak --wait=$SCREEN
for L in $LEVELS; do
	run "LEVEL$L statistics" screen --stats=$L --soak --wait=$SCREEN
	run "LEVEL$L briefing" screen --briefing=$L --soak --wait=$SCREEN
done
run "end movies" screen --end --soak --wait=$SCREEN

rm -rf "$MDK_USER_DIR" "$SHOT" "$LOG"
[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
