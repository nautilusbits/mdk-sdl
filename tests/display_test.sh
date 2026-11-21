#!/bin/sh
# The display options and the GPU backends: --gpu picks SDL_GPU's driver (the log names it), each
# backend draws the same frame, a hidden window ignores the display settings (its frame stays
# 1280 x 960) and older settings files load. Linux and macOS have one backend each: only Vulkan
# (SPIR-V builds) or Metal is tried there.
# Run from the project folder after `./build.sh linux-x64 mdk`: sh tests/display_test.sh
# No window: the game draws off screen and never takes the focus.
export MDK_HIDDEN=1
MDK=${MDK:-build/linux-x64/mdk-sdl}
OUT_DIR=${TMPDIR:-/tmp}
MDK_USER_DIR=$OUT_DIR/mdk_display_user
export MDK_USER_DIR
FAILED=0
# The backends' frames may differ by a few edge pixels: at most this share of bytes (%).
MAX_DIFFERENT=5

check() {
	if [ "$2" = 0 ]; then
		echo "  $1: ok"
	else
		echo "  $1: FAILED"
		FAILED=1
	fi
}

# run <screenshot> <options>
run() {
	SHOT=$1
	shift
	rm -f "$SHOT"
	timeout 60 "$MDK" --mute --screenshot="$SHOT" "$@" 2>&1
}

# backend <--gpu name> <name in the log>: the level's frame on that backend.
backend() {
	rm -rf "$MDK_USER_DIR"
	OUT=$(run "$OUT_DIR/mdk_gpu_$1.bmp" --gpu="$1" --level=3 --wait=2)
	echo "$OUT" | grep "^GPU"
	if echo "$OUT" | grep -q "no .* shaders in this build"; then
		echo "  $1: skipped (not in this build)"
		return 1
	fi

	echo "$OUT" | grep -q "^GPU: .* ($2 via SDL_GPU)"; check "--gpu=$1 names $2" $?
	[ -s "$OUT_DIR/mdk_gpu_$1.bmp" ]; check "--gpu=$1 screenshot" $?
}

case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*)
	backend d3d12 "Direct3D 12" && backend vulkan Vulkan && {
		SIZE=$(wc -c < "$OUT_DIR/mdk_gpu_d3d12.bmp")
		DIFFERENT=$(cmp -l "$OUT_DIR/mdk_gpu_d3d12.bmp" "$OUT_DIR/mdk_gpu_vulkan.bmp" | wc -l)
		echo "  bytes different: $DIFFERENT of $SIZE"
		[ $((DIFFERENT * 100)) -le $((SIZE * MAX_DIFFERENT)) ]; check "Direct3D 12 and Vulkan draw the same frame" $?
	}
	;;
Darwin)
	backend metal Metal
	;;
*)
	backend vulkan Vulkan
	;;
esac

# An unknown backend: a warning, then the settings' (Auto).
rm -rf "$MDK_USER_DIR"
OUT=$(run "$OUT_DIR/mdk_display.bmp" --gpu=glide --menu --wait=0.5)
echo "$OUT" | grep -q "^--gpu=glide: d3d12, vulkan, metal or auto"; check "--gpu=glide warns" $?
echo "$OUT" | grep -q "^GPU: "; check "--gpu=glide starts" $?

# The program's, the look's and the display's lines at start.
# The C++ build names its compiler where .NET names its runtime version (".NET clang 21.1.0").
echo "$OUT" | grep -q "^MDK SDL [0-9].* | \.NET \(clang \)\{0,1\}[0-9].* | SDL 3\."; check "build line" $?
echo "$OUT" | grep -q "^Look: Original, mods: none$"; check "look line" $?
echo "$OUT" | grep -q "^Display: [0-9]*x[0-9]* @ .* Hz, window 1280x960 (hidden), render target 1280x960, swapchain .*, present mode "; check "display line" $?

# A hidden window ignores the window's settings: no fullscreen, no mode change, no render scale.
mkdir -p "$MDK_USER_DIR"
printf 'fullscreen=Exclusive\nwindow_size=1600x1200\nrender_scale=200\nvsync=Adaptive\nframe_limit=60\ngpu_backend=Auto\n' > "$MDK_USER_DIR/settings.cfg"
OUT=$(run "$OUT_DIR/mdk_display.bmp" --menu --options --press=Menu.Accept@0.3 --wait=1)
echo "$OUT" | grep -q "^Display: .*, window 1280x960 (hidden), render target 1280x960,"; check "hidden: 1280 x 960" $?
echo "$OUT" | grep -q "^Menu: Display mode: Exclusive | Resolution: [0-9]*x[0-9]* | Render scale: 200% | VSync: Adaptive.* | Frame limit: 60 | GPU: Auto (.*) | Gamma: 1.0 | Back$"; check "display page" $?

# Older settings: fullscreen=True is the desktop's fullscreen.
printf 'fullscreen=True\nmaster_volume=30\n' > "$MDK_USER_DIR/settings.cfg"
OUT=$(run "$OUT_DIR/mdk_display.bmp" --menu --options --press=Menu.Accept@0.3 --wait=1)
echo "$OUT" | grep -q "^Menu: Display mode: Fullscreen | Resolution: [0-9]*x[0-9]* (desktop) | Render scale: 100% | VSync: On | Frame limit: Off |"; check "older settings" $?

rm -rf "$MDK_USER_DIR"
[ $FAILED = 0 ] && echo PASSED && exit 0
echo FAILED
exit 1
