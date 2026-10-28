#!/bin/sh
# Kurt's other moves against the Godot port's numbers (godot-mdk, --headless, same options):
# - level 3: running and jumping east he grabs the ledge at x 83, climbs it and runs on to the wall
#   (Godot: 93, 446, 145; the y comes from facing the ledge's edge, 15°). Jump held past the top
#   opens the chute there (0x4646a4), and he grabs the ledge under it as the original does
#   (damp_ledge_grab 0x469868 ignores the chute), sinking to it at 8 u/s: hanging by 2 s, not 1.8;
# - level 6: the first wind zone slides him down the tunnel (Godot after 4 and 6 s: -1138, -599,
#   -185 and -1138, -498, -241: on that line);
# - level 7: the wall behind him pushes him away from it (camera_clearance; Godot: x 7.0, below 9);
# - level 3: falling 108 units onto the pad hurts 10 (no Godot reference: it has no hard landings),
#   and 50 below the arena he dies;
# - level 8: falling short of the walkway's edge (no side wall), he ends on its top (z -228), not
#   2 below it as in the original (issue #30).
# Run from the project folder after `dotnet build`: sh tests/kurt_moves_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=src/Mdk.App/bin/Debug/net10.0/mdk_sdl
SHOT=${TMPDIR:-/tmp}/mdk_kurt_moves.bmp
FAILED=0

run() {
	timeout 60 "$MDK" --mute --screenshot="$SHOT" "$@" 2>&1 | grep -E "^Kurt (at|died)"
}

check() {
	if [ "$1" = 0 ]; then echo "  $2: ok"; else echo "  $2: FAILED"; FAILED=1; fi
}

# Coordinates of "Kurt at x y z ...".
xyz() {
	echo "$1" | cut -d' ' -f3-5
}

LEDGE="--level=3 --at=67.6,441.8,133.5,0 --delay=0.5 --walk=3 --jump"
OUT=$(run $LEDGE --wait=2)
echo "$OUT"
echo "$OUT" | grep -q " Hang floor False" ; check $? "hangs from the ledge (under the chute)"
OUT=$(run $LEDGE --wait=5)
echo "$OUT"
set -- $(xyz "$OUT")
awk "BEGIN { exit !(($1 - 93) ^ 2 + ($2 - 446) ^ 2 + ($3 - 145) ^ 2 < 2.25) }"; check $? "climbs it and runs on (Godot 93, 446, 145)"

OUT=$(run --level=6 --at=-1150,-790,-85,90 --wait=6)
echo "$OUT"
echo "$OUT" | grep -q " Slide floor True" ; check $? "slides"
set -- $(xyz "$OUT")
awk "BEGIN { z = -185 - 56 / 101 * ($2 + 599); exit !($1 > -1145 && $1 < -1135 && ($3 - z) ^ 2 < 9) }"; check $? "on the Godot port's path"

OUT=$(run --level=7 --at=12,100,12,180 --wait=1.5)
echo "$OUT"
set -- $(xyz "$OUT")
awk "BEGIN { exit !($1 < 9) }"; check $? "pushed from the wall behind (Godot x 7.0)"

OUT=$(run --level=3 --at=-4,0,300 --wait=4)
echo "$OUT"
echo "$OUT" | grep -q " health 90$" ; check $? "hard landing: 10 damage"

OUT=$(run --level=3 --delay=0.5 --teleport=HMO_1,-4,0,-2000 --wait=7)
echo "$OUT"
echo "$OUT" | grep -q "^Kurt died" ; check $? "dies below the arena"

OUT=$(run --level=8 --at=60,2774,-220,90 --walk=1 --wait=2)
echo "$OUT"
set -- $(xyz "$OUT")
awk "BEGIN { exit !(($3 + 228) ^ 2 < 0.0025) }"; check $? "on the walkway, not in it (z -228)"

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
