#!/bin/sh
# Sniper mode on level 3: Kurt lands facing the grunt XG_1006 (19, 181, 127), enters sniper mode
# looking 9° down and fires one round once the clip is loaded: a bullet takes 8 of its 60 health, a
# sniper grenade blows it up. The screenshot shows the scope: the dark SNIPERS1 frame around a lit
# view. The Godot port, from the same spot (--level=3 --at=0,120,138.2,75.5 --delay=0.3
# --sniper=1,9 --fire --profile=2): XG_1006 at 36 health after three bullets, as here with --fire.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/sniper_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
# Default settings, not the player's (their difficulty changes damage).
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk-sdl}
SHOT=${TMPDIR:-/tmp}/mdk_sniper.bmp
SPOT=--at=0,120,150,75.5
# Godot's comparison starts just above the floor.
LOW_SPOT=--at=0,120,138.2,75.5
FAILED=0

# run <spot> <options>
run() {
	timeout 60 "$MDK" --level=3 --mute --profile --screenshot="$SHOT" --sniper=1,9 "$@" 2>&1
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

OUT=$(run "$SPOT" --sniper-fire --wait=2.5)
profile "$OUT" 1 XG_1006
profile "$OUT" 2 XG_1006
profile "$OUT" 1 XG_1006 | grep -q "health 60 "; check "grunt at full health" $?
profile "$OUT" 2 XG_1006 | grep -q "health 52 "; check "a bullet takes 8" $?

# The scope's screen: the frame's left edge is darker than the view in the scope.
python - "$SHOT" <<'EOF'
import sys
from PIL import Image, ImageStat
image = Image.open(sys.argv[1]).convert("L")
w, h = image.size
frame = ImageStat.Stat(image.crop((0, 0, w // 40, h))).mean[0]
scope = ImageStat.Stat(image.crop((w * 3 // 8, h * 3 // 8, w * 5 // 8, h * 5 // 8))).mean[0]
print("  frame %.0f, scope %.0f" % (frame, scope))
sys.exit(0 if frame < 40 and scope > 2 * frame else 1)
EOF
check "scope overlay" $?

OUT=$(run "$SPOT" --sniper-fire --give=SW_SGREN --wait=3.5)
profile "$OUT" 3 "EXPLODE_[0-9]*" | head -1
[ -z "$(profile "$OUT" 3 XG_1006)" ]; check "a sniper grenade kills the grunt" $?

OUT=$(run "$LOW_SPOT" --fire --wait=2.5)
profile "$OUT" 2 XG_1006
profile "$OUT" 2 XG_1006 | grep -q "health 36 "; check "held fire: three hits (Godot: 36)" $?

# LEVEL6 OLYM_3: three grunts taunt behind a glass wall. A mortar round lobbed through its small
# opening passes the face behind it (seen from its back, as bsp_sweep_box does), lands by XG_1001
# and goes off at 7 s: the grunts fall with their floor (gone at 11 s) and SW_KEY flies out.
OUT=$(timeout 60 "$MDK" --level=6 --at=-1636.9,-76.5,-647,241 --mute --profile --screenshot="$SHOT" \
	--give=SW_LGREN --sniper=1,-32 --sniper-fire --wait=11 2>&1)
profile "$OUT" 11 "SW_KEY_[0-9]*"
[ "$(profile "$OUT" 1 "XG_[0-9]*" | wc -l)" = 3 ] && [ -z "$(profile "$OUT" 11 "XG_[0-9]*")" ]
check "a mortar through the opening drops the grunts" $?

# The enhanced look's filtered frame and mask meet at the view's edge (20, 60, 600x360 of 640x480)
# without a seam: the line there is no lighter than the frame beside it (a seam let the scene through).
run "$SPOT" --enhanced --wait=1 > /dev/null
python - "$SHOT" <<'EOF'
import sys
from PIL import Image
image = Image.open(sys.argv[1]).convert("L")
w, h = image.size
scale = h / 480
left = (w - 640 * scale) / 2
x0, x1 = round(left + 20 * scale), round(left + 620 * scale) - 1
y0, y1 = round(60 * scale), round(420 * scale) - 1
# The edge's lines, each pixel's lightness less the lighter of its neighbours 3 pixels across.
lines = [((x, y), (0, 3)) for y in (y0, y1) for x in range(x0 + 4, x1 - 4)]
lines += [((x, y), (3, 0)) for x in (x0, x1) for y in range(y0 + 4, y1 - 4)]
def across(p, d):
    return image.getpixel(p) - max(image.getpixel((p[0] - d[0], p[1] - d[1])), image.getpixel((p[0] + d[0], p[1] + d[1])))
mean = sum(across(p, d) for p, d in lines) / len(lines)
# Seamless about -8 (as the original look), with the seam about -1.
print("  edge lighter than beside it by %.1f" % mean)
sys.exit(0 if mean < -4 else 1)
EOF
check "no seam around the enhanced view" $?

# LEVEL4's start through the scope (about 600 draws with the insets) runs past Direct3D 12's sampler
# heap: the draws go on in a new command buffer, and the swapchain's, which presents when submitted,
# waits for the frame's end (sniper mode crashed on D3D12 in 1.2.2). --swapchain: the hidden window's
# frames through its swapchain, as a shown one's.
case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*)
	OUT=$(timeout 60 "$MDK" --level=4 --mute --gpu=d3d12 --swapchain --screenshot="$SHOT" --sniper=1,0 \
		--sniper-fire --wait=3 2>&1)
	echo "$OUT" | grep -q "^Saved "; check "D3D12 presents the scope's frame whole" $?
	;;
esac

[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
