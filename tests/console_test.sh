#!/bin/sh
# The console on level 3 (--console opens it and runs commands): pos prints options that start
# there, god survives --die, onehit kills a grunt with the chain gun at once, tp moves Kurt to an arena, save and load a full save, map loads
# another level, kill and unknown commands answer; the game's own lines still print. Outside a
# level (the menu, the stream, the fall) the game's commands work and the level's answer.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/console_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings and saves, not the player's.
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk}
SHOT=${TMPDIR:-/tmp}/mdk_console.bmp
FAILED=0

run() {
	timeout 60 "$MDK" --level=3 --mute --screenshot="$SHOT" "$@" 2>&1
}

# Any screen: no --level.
run_on() {
	timeout 60 "$MDK" --mute --screenshot="$SHOT" "$@" 2>&1
}

# expect <name> <pattern>: $OUT has a line matching the pattern.
expect() {
	echo "$OUT" | grep -q -- "$2" && echo "  $1: ok" || { echo "  $1: FAILED"; FAILED=1; }
}

OUT=$(run --wait=1 --console="pos;bogus;kill")
expect "game lines still print" "^Level 3: 17 arenas"
expect "command echoed" "^\] pos;bogus;kill"
expect "pos" "^level 3 arena HMO_1 at "
expect "pos as --at" "^--level=3 --at=-4.00,0.00,"
expect "pos as --teleport" "^--level=3 --teleport=HMO_1,-4.00,0.00,"
expect "unknown command" "^Unknown command: bogus"
expect "kill" "^Killed [0-9]* enemies"

OUT=$(run --wait=2 --die --console="god")
expect "god survives" "^Kurt at .* health 100"
echo "$OUT" | grep -q "^Kurt died" && { echo "  god: FAILED (died)"; FAILED=1; }

# The grunt XG_1006 in front of Kurt: hurt by the chain gun, or blown up with onehit.
GRUNT="--at=-2,172,135,20 --fire --delay=0.5 --profile --wait=1.6"
OUT=$(run $GRUNT --console="pos")
expect "chain gun hurts" "^  XG_1006 .* health [1-9]"
OUT=$(run $GRUNT --console="onehit")
expect "onehit" "^onehit on"
echo "$OUT" | grep -q "^  XG_1006 " && { echo "  onehit kills: FAILED"; FAILED=1; } || echo "  onehit kills: ok"

OUT=$(run --wait=1.5 --console="tp HMO_4;health 40;save console_t")
expect "tp to an arena" "^Kurt at .* arena HMO_4 health 40"
expect "save" "^Saved console_t"

OUT=$(run --wait=1 --console="load console_t")
expect "load" "^restored hash"

OUT=$(run --wait=1 --console="map 6")
expect "map" "^Level 6: "

OUT=$(run_on --menu --wait=1 --console="fps;pos;map 6")
expect "menu: fps" "^overlay on"
expect "menu: pos" "^Not in a level"
expect "menu: map starts the level" "^Level 6: "

OUT=$(run_on --stream=7 --wait=1 --console="help;give all;god;onehit")
expect "stream: help" "^map <level> \[arena\]"
expect "stream: give" "^Not in a level"
expect "stream: god" "^god on from the next level"
expect "stream: onehit" "^onehit on from the next level"
expect "stream: flies on" "^Saved "

OUT=$(run_on --fall=7 --wait=1 --console="kill;difficulty easy")
expect "fall: kill" "^Not in a level"
expect "fall: difficulty" "^difficulty easy"

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
