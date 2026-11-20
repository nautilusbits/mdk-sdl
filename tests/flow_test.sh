#!/bin/sh
# The menus, saves and level flow: the main menu and its options, the statistics and the briefing,
# the end of a level leading to the statistics, Kurt's death leading to the menu with "Continue",
# and a saved game written and loaded again. Settings and saves go to a temporary user folder.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/flow_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
MDK=${MDK:-build/linux-x64/mdk}
OUT_DIR=${TMPDIR:-/tmp}
SHOT=$OUT_DIR/mdk_flow.bmp
MDK_USER_DIR=$OUT_DIR/mdk_flow_user
export MDK_USER_DIR
rm -rf "$MDK_USER_DIR"
FAILED=0

run() {
	timeout 120 "$MDK" --mute --screenshot="$SHOT" "$@" 2>&1
}

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

OUT=$(run --menu --wait=1)
echo "$OUT" | grep "^Menu"
echo "$OUT" | grep -q "^Menu: New Game | Level: 1 | Saved Game | Options | Quit$"; check "main menu" $?
[ -f "$SHOT" ]; check "menu screenshot" $?

# The frame a page opens draws its items in their rows, not piled at the view's top left: the
# hidden 1280 x 960 canvas letterboxes the 600 x 360 view, its top 96 rows stay black.
TOP_BAND=$((1280 * 4 * 96))
BLACK=ff000000
run --menu --press=Menu.Down@0.5,Menu.Down@0.6,Menu.Down@0.7,Menu.Accept@1 --wait=1 > /dev/null
LIT=$(tail -c +55 "$SHOT" | head -c $TOP_BAND | od -An -v -tx4 | tr -s ' ' '\n' | grep -v "^$\|^$BLACK$" | wc -l)
[ "$LIT" = 0 ]; check "page drawn laid out on its first frame" $?

OUT=$(run --menu --options --wait=1)
echo "$OUT" | grep -q "^Menu: Display | Graphics | 3D Stereo | Audio | Keyboard & mouse | Gamepad | Game | Mods | Back$"; check "options page" $?

# The options' pages; Esc goes up a page (Audio, Options, the main menu).
OUT=$(run --menu --options --press=Menu.Down@0.3,Menu.Down@0.4,Menu.Down@0.5,Menu.Accept@0.6,Menu.Back@1,Menu.Back@1.5 --wait=2)
echo "$OUT" | grep -q "^Menu: Master volume: 80 | Music volume: 50 | Effects volume: 70 | Music filter: On | Back$"; check "audio page" $?
[ "$(echo "$OUT" | grep "^Menu" | tail -n 2 | head -n 1)" = "Menu: Display | Graphics | 3D Stereo | Audio | Keyboard & mouse | Gamepad | Game | Mods | Back" ]; check "Esc: up to the options" $?
echo "$OUT" | grep "^Menu" | tail -n 1 | grep -q "^Menu: New Game"; check "Esc: up to the main menu" $?
OUT=$(run --menu --options --press=Menu.Down@0.3,Menu.Down@0.4,Menu.Down@0.5,Menu.Down@0.6,Menu.Down@0.7,Menu.Down@0.8,Menu.Accept@0.9 --wait=1)
echo "$OUT" | grep -q "^Menu: Difficulty: Normal | FPS overlay: Off | Console | Back$"; check "game page" $?
# The FPS overlay without the console (phones): on, then the page again shows it on.
OUT=$(run --menu --options --press=Menu.Down@0.3,Menu.Down@0.4,Menu.Down@0.5,Menu.Down@0.6,Menu.Down@0.7,Menu.Down@0.8,Menu.Accept@0.9,Menu.Down@1.1,Menu.Accept@1.2,Menu.Back@1.4,Menu.Accept@1.6 --wait=2)
echo "$OUT" | grep "^Menu" | tail -n 1 | grep -q "^Menu: Difficulty: Normal | FPS overlay: On | Console | Back$"; check "FPS overlay from the game page" $?
# The console without the Grave key (phones): it has the keys, so Down no longer moves the menu.
OUT=$(run --menu --options --press=Menu.Down@0.3,Menu.Down@0.4,Menu.Down@0.5,Menu.Down@0.6,Menu.Down@0.7,Menu.Down@0.8,Menu.Accept@0.9,Menu.Down@1.1,Menu.Down@1.2,Menu.Accept@1.3,Menu.Down@1.6,Menu.Accept@1.7 --wait=2)
echo "$OUT" | grep "^Menu" | tail -n 1 | grep -q "^Menu: Difficulty"; check "console from the game page" $?
OUT=$(run --menu --options --press=Menu.Down@0.3,Menu.Down@0.4,Menu.Accept@0.5 --wait=1)
echo "$OUT" | grep -q "^Menu: Mode: Off | Separation: 0.25 | Convergence: 10 | Head tracking: Off | Back$"; check "3D stereo page" $?
OUT=$(run --menu --controls --wait=1)
echo "$OUT" | grep -q "^Menu: Mouse sensitivity: 1.00 | Invert mouse: Off | Forward: W | .* | Always run: CapsLock | .* | Quick load: F9 | Default keys | Back$"; check "controls page" $?
OUT=$(run --menu --gamepad --wait=1)
echo "$OUT" | grep -q "^Menu: Look sensitivity: 1.00 | Invert look: Off | Jump: A | Run: B | Always run: L3 | Fire: RT | Sniper mode: LT | .* | Quick load: - | Default buttons | Back$"; check "gamepad page" $?

# -nobloodno / -bloodyes (not saved) set gore (Options, Graphics).
OUT=$(run --menu --options --nobloodno --press=Menu.Down@0.3,Menu.Accept@0.5 --wait=1)
echo "$OUT" | grep -q "^Menu: Graphics: Original | Anti-aliasing: Off | Gore: Off | Back$"; check "--nobloodno" $?
OUT=$(run --menu --options --bloodyes --press=Menu.Down@0.3,Menu.Accept@0.5 --wait=1)
echo "$OUT" | grep -q "| Gore: On | Back$"; check "--bloodyes" $?

OUT=$(run --stats=7 --counts=120,60,10,5,8,20,3 --phase=1 --wait=2)
echo "$OUT" | grep "^Score"
echo "$OUT" | grep -q "^Score-O-matic: 120/120, 50/100, 10/10, 50/100, 8/20, heads 3$"; check "Score-O-matic counts" $?

OUT=$(run --briefing=6 --wait=2)
echo "$OUT" | grep -q "^Statistics: Briefing$"; check "briefing" $?

# special_event 1 ends level 7: the tornado (about 7 s), then the stream (tests/stream_test.sh
# follows it to the statistics).
OUT=$(run --level=7 --event=1 --wait=12)
echo "$OUT" | grep "^Stream"
echo "$OUT" | grep -q "^Stream after LEVEL7: Normal,"; check "level end leads to the stream" $?

# Kurt dies: LASTGAME is written and the menu offers "Continue".
OUT=$(run --level=3 --die --wait=10)
echo "$OUT" | grep "^Kurt died\|^Menu"
echo "$OUT" | grep -q "^Menu: Continue | New Game | Level: 3 |"; check "death leads to the menu with Continue" $?

OUT=$(run --level=6 --save=FLOW1 --wait=0.5)
echo "$OUT" | grep "^Saved game"
grep -q '"type":3,"level":6' "$MDK_USER_DIR/saves/FLOW1.sav"; check "game saved" $?
OUT=$(run --load=FLOW1 --wait=0.5)
echo "$OUT" | grep "^Loaded\|^Level"
echo "$OUT" | grep -q "^Level 6:"; check "saved game loads level 6" $?

rm -rf "$MDK_USER_DIR"
[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
