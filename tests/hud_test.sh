#!/bin/sh
# The inventory stays on screen outside sniper mode (0x46cce4 resets its timer 0x574328 to 60
# every frame): 3 seconds after the start, with Kurt still and firing, the super chain gun's
# slot and its ticks are still drawn at the bottom left. Each run is compared with the same run
# without the pickup.
# The mission timer (0x420830): with 3/4 of the time gone, SC_BSTAT's black covers the panel's
# ring counter-clockwise from the right (0x420d88), all but its bottom right quarter; also over
# a mod's panel (a plain green image twice the size), in the enhanced look.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/hud_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk}
DIR=${TMPDIR:-/tmp}
FAILED=0

# shot <file> <options>
shot() {
	FILE="$DIR/$1"
	shift
	rm -f "$FILE"
	timeout 60 "$MDK" --level=3 --mute --wait=3 --screenshot="$FILE" "$@" > /dev/null 2>&1
}

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

# inventory_shown <with> <without>: the bottom left (the first two slots) differs.
inventory_shown() {
	python - "$DIR/$1" "$DIR/$2" <<'EOF'
import sys
from PIL import Image, ImageChops, ImageStat
with_item = Image.open(sys.argv[1]).convert("L")
without = Image.open(sys.argv[2]).convert("L")
w, h = with_item.size
box = (0, h * 300 // 360, w * 80 // 600, h)
diff = ImageStat.Stat(ImageChops.difference(with_item.crop(box), without.crop(box))).mean[0]
print("  bottom left difference %.1f" % diff)
sys.exit(0 if diff > 5 else 1)
EOF
}

shot mdk_hud_gatt.bmp --give=SW_GATT
shot mdk_hud_none.bmp
inventory_shown mdk_hud_gatt.bmp mdk_hud_none.bmp; check "inventory shown while still" $?

shot mdk_hud_gatt_fire.bmp --give=SW_GATT --fire
shot mdk_hud_none_fire.bmp --fire
inventory_shown mdk_hud_gatt_fire.bmp mdk_hud_none_fire.bmp; check "inventory shown while firing" $?

# timer_shot <file> <ticks left> <options>: a save of the start with that time left, loaded.
timer_shot() {
	timeout 60 "$MDK" --level=3 --mute --wait=1 --snapshot=TIMER --screenshot="$DIR/$1" > /dev/null 2>&1
	sed -i "s/\"town_ticks\":[0-9]*/\"town_ticks\":$2/" "$MDK_USER_DIR/saves/TIMER.sav"
	FILE=$1
	shift 2
	rm -f "$DIR/$FILE"
	timeout 60 "$MDK" --load=TIMER --mute --wait=1 --screenshot="$DIR/$FILE" "$@" > /dev/null 2>&1
}

# timer_emptied <late> <early>: the panel's top left quarter changed, not its bottom right.
timer_emptied() {
	python - "$DIR/$1" "$DIR/$2" <<'EOF'
import sys
from PIL import Image, ImageChops, ImageStat
late = Image.open(sys.argv[1]).convert("L")
early = Image.open(sys.argv[2]).convert("L")
w, h = late.size
s = h / 360
left, top, right, bottom = w - 100 * s, h - 73 * s, w - 16 * s, h - 10 * s
mid_x, mid_y = (left + right) / 2, (top + bottom) / 2
def diff(box):
	box = tuple(int(v) for v in box)
	return ImageStat.Stat(ImageChops.difference(late.crop(box), early.crop(box))).mean[0]
emptied = diff((left, top, mid_x, mid_y))
kept = diff((mid_x, mid_y, right, bottom))
print("  timer: top left difference %.1f, bottom right %.1f" % (emptied, kept))
sys.exit(0 if emptied > 5 and kept < 1 else 1)
EOF
}

timer_shot mdk_hud_timer_early.bmp 53900
timer_shot mdk_hud_timer_late.bmp 13500
timer_emptied mdk_hud_timer_late.bmp mdk_hud_timer_early.bmp; check "timer ring emptied" $?

# A save keeps its own total (saves have no difficulty): an Easy save with 1/4 of its 81000
# ticks gone, loaded on Hard (36000), shows the right quarter gone, not a full ring.
echo "difficulty=Easy" > "$MDK_USER_DIR/settings.cfg"
timer_shot mdk_hud_timer_full.bmp 80990
timer_shot mdk_hud_timer_quarter.bmp 60750
echo "difficulty=Hard" > "$MDK_USER_DIR/settings.cfg"
rm -f "$DIR/mdk_hud_timer_quarter.bmp"
timeout 60 "$MDK" --load=TIMER --mute --wait=1 --screenshot="$DIR/mdk_hud_timer_quarter.bmp" > /dev/null 2>&1
rm -f "$MDK_USER_DIR/settings.cfg"
python - "$DIR/mdk_hud_timer_quarter.bmp" "$DIR/mdk_hud_timer_full.bmp" <<'EOF'
import sys
from PIL import Image, ImageChops, ImageStat
late = Image.open(sys.argv[1]).convert("L")
early = Image.open(sys.argv[2]).convert("L")
w, h = late.size
s = h / 360
left, top, right, bottom = w - 100 * s, h - 73 * s, w - 16 * s, h - 10 * s
mid_x, mid_y = (left + right) / 2, (top + bottom) / 2
def diff(box):
	box = tuple(int(v) for v in box)
	return ImageStat.Stat(ImageChops.difference(late.crop(box), early.crop(box))).mean[0]
emptied = diff((mid_x, top, right, mid_y))
kept = diff((left, mid_y, mid_x, bottom))
print("  timer: top right difference %.1f, bottom left %.1f" % (emptied, kept))
sys.exit(0 if emptied > 5 and kept < 1 else 1)
EOF
check "timer ring of a save made on another difficulty" $?

MOD="$MDK_USER_DIR/mods/panel"
mkdir -p "$MOD/images"
printf 'name=Panel\npriority=0\n' > "$MOD/mod.txt"
python -c "import sys; from PIL import Image; Image.new('RGBA', (168, 126), (0, 200, 0, 255)).save(sys.argv[1])" "$MOD/images/SC_STAT.png"
timer_shot mdk_hud_timer_mod_early.bmp 53900 --enhanced
timer_shot mdk_hud_timer_mod_late.bmp 13500 --enhanced
timer_emptied mdk_hud_timer_mod_late.bmp mdk_hud_timer_mod_early.bmp; check "timer ring emptied over a mod's panel" $?

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
