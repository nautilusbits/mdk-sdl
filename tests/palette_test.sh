#!/bin/sh
# LEVEL8 keeps the system colours at palette 0-63 (0x41ba68 copies only the DTI's 64-255): its
# DTI's magenta and purple 10-12 never show. The grenade's inventory icon uses them; no pixel of
# either look may be magenta (R > 200, B > 200, G < 80).
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/palette_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
export MDK_USER_DIR="$(mktemp -d)"
MDK=${MDK:-build/linux-x64/mdk}
DIR=${TMPDIR:-/tmp}
FAILED=0

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

# no_magenta <file>
no_magenta() {
	python - "$1" <<'EOF'
import sys
from PIL import Image
pixels = Image.open(sys.argv[1]).convert("RGB").tobytes()
count = sum(1 for i in range(0, len(pixels), 3) if pixels[i] > 200 and pixels[i + 2] > 200 and pixels[i + 1] < 80)
print("  magenta pixels %d" % count)
sys.exit(0 if count == 0 else 1)
EOF
}

for LOOK in original enhanced; do
	SHOT="$DIR/mdk_palette_$LOOK.bmp"
	rm -f "$SHOT"
	timeout 60 "$MDK" --level=8 --mute --$LOOK --wait=3 --give=SW_HBOMB --screenshot="$SHOT" > /dev/null 2>&1
	[ -s "$SHOT" ]; check "$LOOK screenshot" $?
	no_magenta "$SHOT"; check "$LOOK grenade icon without magenta" $?
done

# The dog's icon (PICKUPS frame 8) uses arena colours (64-175), the DTI's magenta there: the HUD
# draws with the current arena's (0x4709d8 sets them in the table 0x581c28 its sprites use).
SHOT="$DIR/mdk_palette_dog.bmp"
rm -f "$SHOT"
timeout 60 "$MDK" --level=3 --mute --wait=3 --give=SW_SBONE --screenshot="$SHOT" > /dev/null 2>&1
[ -s "$SHOT" ]; check "dog screenshot" $?
no_magenta "$SHOT"; check "dog icon without magenta" $?

rm -rf "$MDK_USER_DIR"
[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
