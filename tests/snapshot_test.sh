#!/bin/sh
# Full saves (F2): a level is saved after a walk, then loaded; the state taken again right after
# loading must be the same (same hash), and the level must run on. The Godot port, same options
# (tests/snapshot_test.sh there): LEVEL7, 7 objects, Kurt at (0, 59, 3) after loading.
# Saves go to a temporary user folder.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/snapshot_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
MDK=${MDK:-build/linux-x64/mdk-sdl}
OUT_DIR=${TMPDIR:-/tmp}
SHOT=$OUT_DIR/mdk_snapshot.bmp
MDK_USER_DIR=$OUT_DIR/mdk_snapshot_user
export MDK_USER_DIR
rm -rf "$MDK_USER_DIR"
FAILED=0

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

# snapshot <level options...>: saves, loads, compares the hashes.
snapshot() {
	SAVED=$(timeout 120 "$MDK" --mute "$@" --snapshot=TESTSNAP --screenshot="$SHOT" --wait=3.5 2>&1 | grep "^snapshot hash\|Exception")
	LOADED=$(timeout 120 "$MDK" --mute --load=TESTSNAP --screenshot="$SHOT" --wait=3 2>&1 | grep "^restored hash\|^Kurt at\|Exception")
	echo "$SAVED"
	echo "$LOADED"
	[ -n "$SAVED" ] && [ "${SAVED#snapshot }" = "$(echo "$LOADED" | grep '^restored' | sed 's/^restored //')" ]
}

snapshot --level=7 --delay=0.5 --walk=3; check "LEVEL7 after a walk" $?
echo "$LOADED" | grep -q "^Kurt at -0.00 59.53 2.82"; check "Kurt where he was (Godot: 0, 59, 3)" $?
snapshot --level=3 --at=-2,172,135,20 --fire; check "LEVEL3 among grunts" $?
snapshot --level=8 --fire; check "LEVEL8" $?

rm -rf "$MDK_USER_DIR"
[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
