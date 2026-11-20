#!/bin/sh
# Combat on level 3: Kurt stands facing the first grunt (XG_1005, which walks into his chain gun's
# cone) and holds fire: it's hit (and often dies); standing without firing, the grunts' shots hurt him; a grenade
# he throws flies, lands and blows up. The Godot port, from the same spot (--level=3
# --at=0,120,150,90 --fire --profile=10): XG_1005 dead, Kurt in K_SHOT with 94 health.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/combat_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk}
SHOT=${TMPDIR:-/tmp}/mdk_combat.bmp
SPOT=--at=0,120,150,90
FAILED=0

run() {
	timeout 60 "$MDK" --level=3 --mute --profile --screenshot="$SHOT" "$SPOT" "$@" 2>&1
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

OUT=$(run --fire --wait=15)
profile "$OUT" 1 XG_1005
profile "$OUT" 15 XG_1005
echo "$OUT" | grep "^Kurt at"
profile "$OUT" 1 XG_1005 | grep -q "health 60 "; check "grunt there at full health" $?
# Whether it dies depends on where the aliens' random moves leave it (the same in the Godot port).
LAST=$(profile "$OUT" 15 XG_1005)
[ -z "$LAST" ] || ! echo "$LAST" | grep -q "health 60 "; check "chain gun hits the grunt" $?
echo "$OUT" | grep "^Kurt at" | grep -q " Shot "; check "Kurt fires (K_SHOT)" $?

OUT=$(run --wait=15)
echo "$OUT" | grep "^Kurt at"
HEALTH=$(echo "$OUT" | grep "^Kurt at" | sed 's/.* health \([0-9]*\)$/\1/')
[ "${HEALTH:-100}" -lt 100 ]; check "grunts' shots hurt Kurt" $?

# Thrown at 1 s (flags 0x818a6), the grenade lands about 140 units ahead and explodes.
OUT=$(run --give=SW_HBOMB --use --wait=6)
profile "$OUT" 2 "SW_HBOMB_[0-9]*" | grep "flags 818a6"
profile "$OUT" 2 "SW_HBOMB_[0-9]*" | grep -q "flags 818a6"; check "grenade thrown" $?
[ -z "$(profile "$OUT" 6 "SW_HBOMB_[0-9]*" | grep "flags 8")" ]; check "grenade blows up" $?

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
