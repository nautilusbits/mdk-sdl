#!/bin/sh
# The touch layout editor (Options, Touch; --touch-layout opens it without a touch screen): keys
# pick a control and grow it, Enter (Done) saves its place, Delete resets, Back cancels. The
# desktop's options have no Touch page. Settings go to a temporary user folder.
# Run from the project folder after `dotnet build`: sh tests/touch_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
MDK=src/Mdk.App/bin/Debug/net10.0/mdk_sdl
OUT_DIR=${TMPDIR:-/tmp}
SHOT=$OUT_DIR/mdk_touch.bmp
MDK_USER_DIR=$OUT_DIR/mdk_touch_user
export MDK_USER_DIR
CFG=$MDK_USER_DIR/settings.cfg
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

rm -rf "$MDK_USER_DIR"
OUT=$(run --menu --gamepad --wait=1)
! echo "$OUT" | grep -q "Edit touch layout"; check "desktop gamepad page without the editor" $?
OUT=$(run --menu --options --wait=1)
! echo "$OUT" | grep -q "| Touch |"; check "desktop options without the Touch page" $?

rm -f "$SHOT"
OUT=$(run --menu --touch-layout --wait=1)
echo "$OUT" | grep -q "^Touch layout: || | FIRE | JUMP | USE | SNIPE | ITEM | + | - | MOVE$"; check "editor opens" $?
VALUES=$(tail -c +55 "$SHOT" 2>/dev/null | od -An -v -tu1 | tr -s ' ' '\n' | sort -u | wc -l)
[ "$VALUES" -ge 16 ]; check "editor drawn" $?

# Down twice picks Fire (pause is first), Right twice grows it, Enter saves; back on the Touch page.
OUT=$(run --menu --touch-layout --press=Menu.Down@0.3,Menu.Down@0.35,Menu.Right@0.4,Menu.Right@0.5,Menu.Accept@0.8 --wait=1.2)
echo "$OUT" | grep -q "^Touch layout saved: Fire$"; check "done saves" $?
grep -q "^touch\.Fire=Right,[0-9.]*,Bottom,[0-9.]*,[0-9.]*,[0-9.]*" "$CFG"; check "place in settings.cfg" $?
[ "$(grep -c "^touch\." "$CFG")" = 1 ]; check "only the edited control saved" $?
echo "$OUT" | grep "^Menu" | tail -n 1 | grep -q "^Menu: Touch opacity: 100% | Edit touch layout | Back$"; check "back to the touch page" $?
echo "$OUT" | grep -q "^Menu: Display | Graphics | 3D Stereo | Audio | Keyboard & mouse | Gamepad | Touch | Game | Mods | Back$"; check "options list the Touch page" $?

# Back cancels: the saved place stays.
BEFORE=$(grep "^touch\." "$CFG")
run --menu --touch-layout --press=Menu.Down@0.3,Menu.Down@0.35,Menu.Down@0.38,Menu.Right@0.4,Menu.Back@0.8 --wait=1.2 > /dev/null
[ "$(grep "^touch\." "$CFG")" = "$BEFORE" ]; check "back cancels" $?

# Delete resets, Enter saves the defaults: no places left.
OUT=$(run --menu --touch-layout --press=Menu.Delete@0.3,Menu.Accept@0.5 --wait=1)
! grep -q "^touch\." "$CFG"; check "reset to the defaults" $?

rm -rf "$MDK_USER_DIR"
[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
