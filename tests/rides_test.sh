#!/bin/sh
# The rides (rides.gd, snowboard.gd, bomber.gd), compared with the Godot port's runs of the same
# options (tests/walker_ride_test.sh, snowboard_test.sh, bomber_test.sh there):
# - LEVEL7's XD2: Kurt rides it for 12 s and stays on the floor (he doesn't collide with the walker
#   he sits in). Godot: riding XD2, Kurt at (71, 4903, -27).
# - LEVEL4's snowboard of CMEAT_3: Kurt lands on it, rides it and breaks through the ice wall
#   (group 3): past y 4500 after 12 s. Godot: riding XSNOWB, Kurt at (239, 4858, -654).
# - LEVEL7's XE: the comm device calls it, Kurt drops onto it, the view sinks into it, the script
#   unlocks the controls and Kurt drops a bomb. Godot: "bomber view 0.0, locked false, bombs 9",
#   XBN_BOMB at (121, 2394, -53).
# - LEVEL6's cannon (XBGUN) of OLYM_4: Kurt lands on it and fires; it fires twice and flies him
#   through the glass into COLYM_4 (if_player_on_me_low 0x458a59).
# Run from the project folder after `dotnet build`: sh tests/rides_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=src/Mdk.App/bin/Debug/net10.0/mdk_sdl
SHOT=${TMPDIR:-/tmp}/mdk_rides.bmp
FAILED=0

run() {
	timeout 120 "$MDK" --mute --profile --screenshot="$SHOT" "$@" 2>&1
}

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

# The last "Kurt at x y z" coordinate n (1-3).
kurt() {
	echo "$1" | grep "^Kurt at" | tail -1 | cut -d' ' -f$(($2 + 2))
}

OUT=$(run --level=7 --delay=0.5 --at=46,4807,-25,270 --ride=XD2 --walk=12 --wait=13)
echo "$OUT" | grep "^riding" | tail -1
echo "$OUT" | grep "^Kurt at"
echo "$OUT" | grep -q "^riding XD2"; check "rides the XD2" $?
awk "BEGIN { exit !($(kurt "$OUT" 3) > -40 && $(kurt "$OUT" 3) < 0) }"; check "stays on the floor" $?

OUT=$(run --level=4 --delay=0.5 --teleport=CMEAT_3,420,4285,-570 --wait=12.5)
echo "$OUT" | grep "^riding" | tail -1
echo "$OUT" | grep "^Kurt at"
echo "$OUT" | grep -q "^riding XSNOWB"; check "rides the snowboard" $?
awk "BEGIN { exit !($(kurt "$OUT" 2) > 4500) }"; check "breaks through the ice wall" $?

# Uphill out of a bend of CMEAT_3 the board dips under the floor; the original draws an object
# after the floor it stands on: its white shows under Kurt (bright pixels of the lower middle).
run --level=4 --delay=0.5 --teleport=CMEAT_3,420,4285,-570 --wait=9 --size=640x480 > /dev/null
BOARD=$(tail -c +55 "$SHOT" | od -An -v -tu1 | tr -s ' ' '
' | grep -v '^$' |
	awk '{ c = (NR - 1) % 4; if (c == 0) { m = $1 } else if (c < 3) { if ($1 < m) m = $1 } else {
		p = int((NR - 1) / 4); x = p % 640; y = int(p / 640)
		if (m > 170 && x >= 240 && x < 400 && y >= 240) n++ } } END { print n + 0 }')
echo "  board pixels: $BOARD"
[ "$BOARD" -ge 200 ]; check "the board over the floor" $?

# The ride's end in CMEAT_1: MEAT_3's shell lies above the tube's floor; the original draws the
# camera's arena over the other one, so Kurt shows whole (his dark legs in the lower middle).
for LOOK in --original --enhanced; do
	run --level=4 --delay=0.5 --teleport=CMEAT_1,62.35,2582.81,-392.90 --wait=2 --size=640x480 $LOOK > /dev/null
	LEGS=$(tail -c +55 "$SHOT" | od -An -v -tu1 | tr -s ' ' '
' | grep -v '^$' |
		awk '{ c = (NR - 1) % 4; if (c == 0) { m = $1 } else if (c < 3) { if ($1 > m) m = $1 } else {
			p = int((NR - 1) / 4); x = p % 640; y = int(p / 640)
			if (m < 40 && x >= 290 && x < 370 && y >= 340 && y < 440) n++ } } END { print n + 0 }')
	echo "  Kurt's legs ($LOOK): $LEGS"
	[ "$LEGS" -ge 1000 ]; check "the tube over the shell ($LOOK)" $?
done

OUT=$(run --level=7 --delay=0.5 --teleport=DANT_5,100,2320,-60 --bomber=drop --wait=13.5)
echo "$OUT" | grep "XBN_BOMB" | head -1
echo "$OUT" | grep -q "^riding XE"; check "rides the XE" $?
echo "$OUT" | grep -q "^bomber view 0.0, locked false, bombs 9"; check "view in the XE, a bomb dropped" $?
echo "$OUT" | grep -q "XBN_BOMB.* DANT_5"; check "the bomb falls" $?

OUT=$(run --level=6 --delay=0.5 --teleport=OLYM_4,-1732,92,-1260 --fire --wait=6)
echo "$OUT" | grep "XBGUN_.* flags 8" | tail -1
echo "$OUT" | grep -q "XBGUN_.* COLYM_4 .* path 35601"; check "the cannon flies Kurt" $?

OUT=$(run --level=4 --delay=0.5 --teleport=MEAT_7,0,14780,30 --walk=4 --jump --wait=4.5)
echo "$OUT" | grep "^Kurt at"
awk "BEGIN { exit !($(kurt "$OUT" 2) < 14822) }"; check "the door stops a jump" $?

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
