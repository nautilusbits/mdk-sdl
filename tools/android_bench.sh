#!/bin/sh
# Benchmarks the C++ port against the C# original (mdk-sdl) on an Android phone over adb.
#
# Usage: tools/android_bench.sh <MDK folder> [reps (3)] [C++ APK] [C# APK]
#   MDK folder: the installation (GOG, Steam, CD) with TRAVERSE, MISC, FALL3D and STREAM, on
#   this computer (pushed), or on the device as device:/sdcard/Download/MDK (copied there, only
#   those four folders; the source is only read).
#   APKs: build/publish/mdk-bench-cpp.apk and build/publish/mdk-bench-cs.apk by default
#   (packages io.github.nemo22.mdk.bench.cpp / .cs: beside the real MDK, which stays untouched).
# Environment: ADB (adb; ANDROID_SERIAL picks the device), TIMEOUT (seconds a run may take past its --wait, 120), COOLDOWN
# (seconds between runs, 5), OUT (the CSV, build/android_bench.csv).
#
# Once per package: install (-r), the game folders pushed to its files/mdk with the import stamp
# (as SetupActivity's copy; skipped when there) and settings.cfg with vsync=Off. Then each
# scenario × rep, the apps alternating (order swapped every rep):
#   force-stop ─► logcat -c ─► am start -W --es options "..." ─► poll pidof + dumpsys meminfo
#     ─► process gone (--wait quit the game) ─► "Perf:" lines (logcat tag MDK) ─► CSV row
# Then the medians per scenario and app. Times: frame (whole frame, GPU wait included), CPU
# (the frame's sections less the GPU wait), load (the level), wall (am start to exit, device clock).
# Needs only sh, awk, sort and adb (Linux, macOS, WSL).
#
# The APKs: C++, in android/: ./gradlew assembleStandardRelease -PbenchId=io.github.nemo22.mdk.bench.cpp
# C#: a copy of mdk-sdl 1.2.6 (no .git) with tools/android_bench_cs.patch (git apply --ignore-whitespace),
# then dotnet publish src/Mdk.Android -c Release -p:ApplicationId=io.github.nemo22.mdk.bench.cs
# (-p:DxcSpirv=... -p:RequiredShaders=spirv; NDK r27); the APK is publish/*-Signed.apk (debug key).

set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MDK_DIR=${1:?usage: android_bench.sh <MDK folder> [reps] [C++ APK] [C# APK]}
REPS=${2:-3}
CPP_APK=${3:-$ROOT/build/publish/mdk-bench-cpp.apk}
CS_APK=${4:-$ROOT/build/publish/mdk-bench-cs.apk}
ADB=${ADB:-adb}
TIMEOUT=${TIMEOUT:-120}
COOLDOWN=${COOLDOWN:-5}
OUT=${OUT:-build/android_bench.csv}
mkdir -p "$(dirname "$OUT")"

CPP_PKG=io.github.nemo22.mdk.bench.cpp
CS_PKG=io.github.nemo22.mdk.bench.cs
GAME_FOLDERS="TRAVERSE FALL3D STREAM MISC"
# The import's stamp (formats/MdkData ImportStamp): the copy is complete.
STAMP=.imported

# name|options: as tests/alloc_test.sh on the desktop (--perf=0 on startup: its load time).
SCENARIOS='startup|--level=3 --perf=0 --wait=0.1 --mute
meat5|--level=4 --delay=0.5 --teleport=MEAT_5,-288,13559,-1989 --original --perf=5 --wait=25 --mute
walkfire|--level=7 --walk=4 --fire --perf=10 --wait=40 --mute
sniper|--level=7 --sniper=0.5,5 --perf=5 --wait=20 --mute'

die() {
	echo "android_bench: $*" >&2
	exit 1
}

sh_adb() {
	"$ADB" shell "$@" < /dev/null | tr -d '\r'
}

# The device's clock in seconds (2 decimals): wall times without host clock resolution issues.
uptime_s() {
	sh_adb cat /proc/uptime | awk '{ print $1 }'
}

# launcher <package>: its launcher activity (C#'s has a generated name); am start -p doesn't
# resolve it on some systems (HyperOS).
launcher() {
	sh_adb cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER "$1" | tail -n 1
}

# setup <package> <apk>: install, then the game files and settings once.
setup() {
	PKG=$1
	[ -f "$2" ] || die "no APK $2"
	echo "Installing $2"
	"$ADB" install -r "$2" < /dev/null > /dev/null || die "install of $2 failed"

	FILES=/sdcard/Android/data/$PKG/files
	if [ "$(sh_adb "[ -f $FILES/mdk/$STAMP ] && echo yes")" = yes ]; then
		echo "  game files already in $FILES/mdk"
		return
	fi

	# The app makes its files folder (owned by it) on its first start; then it shows the setup
	# screen (no game files yet), which the force-stop closes.
	sh_adb am start -W -n "$(launcher "$PKG")" > /dev/null
	i=0
	until [ "$(sh_adb "[ -d $FILES ] && echo yes")" = yes ]; do
		i=$((i + 1))
		[ $i -le 20 ] || die "$FILES not made by $PKG"
		sleep 1
	done

	sh_adb am force-stop "$PKG"
	sh_adb mkdir -p "$FILES/mdk"
	case $MDK_DIR in
		device:*) copy_on_device "${MDK_DIR#device:}" "$FILES/mdk" ;;
		*) push_folders "$FILES/mdk" ;;
	esac

	[ "$(sh_adb "[ -f $FILES/mdk/TRAVERSE/TRAVSPRT.BNI ] || [ -f $FILES/mdk/traverse/travsprt.bni ] && echo yes")" = yes ] \
		|| echo "  warning: no TRAVERSE/TRAVSPRT.BNI copied: is $MDK_DIR the MDK folder?"
	sh_adb touch "$FILES/mdk/$STAMP"
	# Frames not held to the display's rate where the GPU allows (else FIFO: see the CPU column).
	sh_adb "echo vsync=Off > $FILES/settings.cfg"
}

# game_folder <name>: true for TRAVERSE, FALL3D, STREAM, MISC in any case.
game_folder() {
	UPPER=$(echo "$1" | tr '[:lower:]' '[:upper:]')
	case " $GAME_FOLDERS " in
		*" $UPPER "*) return 0 ;;
	esac

	return 1
}

# push_folders <target>: the game folders of the local MDK folder, over adb.
push_folders() {
	for F in "$MDK_DIR"/*; do
		[ -d "$F" ] || continue
		NAME=$(basename "$F")
		game_folder "$NAME" || continue
		echo "  pushing $NAME"
		"$ADB" push "$F" "$1/" < /dev/null > /dev/null || die "push of $F failed"
	done
}

# copy_on_device <source> <target>: the game folders of an MDK folder on the device (read only).
copy_on_device() {
	for NAME in $(sh_adb ls "$1"); do
		game_folder "$NAME" || continue
		echo "  copying $1/$NAME"
		[ "$(sh_adb "cp -r '$1/$NAME' '$2/' && echo yes")" = yes ] || die "copy of $1/$NAME failed"
	done
}

# Max of TOTAL PSS / TOTAL RSS in kB of dumpsys meminfo (Android 10+: "TOTAL PSS: n ... TOTAL RSS: n";
# older: the "TOTAL n" row, no RSS).
meminfo() {
	sh_adb dumpsys meminfo "$1" | awk '
		/TOTAL PSS:/ { for (i = 1; i < NF; i++) { if ($(i - 1) == "TOTAL" && $i == "PSS:") pss = $(i + 1); if ($(i - 1) == "TOTAL" && $i == "RSS:") rss = $(i + 1) } }
		/^ *TOTAL +[0-9]/ && pss == "" { pss = $2 }
		END { print (pss == "" ? 0 : pss) " " (rss == "" ? 0 : rss) }'
}

# run <app> <package> <scenario> <options> <rep>: one run, one CSV row.
run() {
	APP=$1 PKG=$2 SCEN=$3 OPTS=$4 REP=$5
	WAIT=$(echo "$OPTS" | sed -n 's/.*--wait=\([0-9.]*\).*/\1/p')
	LIMIT=$(awk -v w="${WAIT:-0}" -v t="$TIMEOUT" 'BEGIN { printf "%d", w + t }')
	sh_adb am force-stop "$PKG"
	"$ADB" logcat -c < /dev/null
	START=$(uptime_s)
	sh_adb "am start -W -n $(launcher "$PKG") --es options '$OPTS'" > /dev/null

	# Running: the memory's peak, sampled each second, until the process ends.
	PEAK_PSS=0 PEAK_RSS=0 STATUS=ok
	while :; do
		PID=$(sh_adb pidof "$PKG")
		[ -n "$PID" ] || break
		set -- $(meminfo "$PKG")
		[ "$1" -gt "$PEAK_PSS" ] 2>/dev/null && PEAK_PSS=$1
		[ "$2" -gt "$PEAK_RSS" ] 2>/dev/null && PEAK_RSS=$2
		NOW=$(uptime_s)
		if awk -v s="$START" -v n="$NOW" -v l="$LIMIT" 'BEGIN { exit !(n - s > l) }'; then
			STATUS=timeout
			sh_adb am force-stop "$PKG"
			break
		fi

		sleep 1
	done

	END=$(uptime_s)
	LOG=$("$ADB" logcat -d -s MDK:I < /dev/null | tr -d '\r')
	PERF=$(echo "$LOG" | sed -n 's/.*\(Perf: .*\)/\1/p')
	echo "$LOG" | grep -q "MDK stopped" && STATUS=stopped
	[ -n "$PERF" ] || [ "$STATUS" != ok ] || STATUS=noperf
	echo "$PERF" | sed 's/^/    /'

	# Perf: N frames, F ms (X FPS): scene a render b ... ms (GPU wait g)
	# Perf: load L ms, run R s, GC heap H MB (live V MB), working set W MB
	echo "$PERF" | awk -v app="$APP" -v scen="$SCEN" -v rep="$REP" -v s="$START" -v e="$END" \
		-v pss="$PEAK_PSS" -v rss="$PEAK_RSS" -v status="$STATUS" '
		/ frames, / {
			for (i = 1; i <= NF; i++) { if ($i == "frames,") frame = $(i + 1) }
			line = $0
			sub(/.*FPS\): /, "", line); sub(/ ms \(GPU wait.*/, "", line)
			n = split(line, f, " "); sum = 0
			for (i = 2; i <= n; i += 2) sum += f[i]
			gpu = $0; sub(/.*GPU wait /, "", gpu); sub(/\).*/, "", gpu)
			cpu = sum - gpu
		}
		/Perf: load / {
			for (i = 1; i <= NF; i++) {
				if ($i == "load") load = $(i + 1)
				if ($i == "heap") heap = $(i + 1)
				if ($i == "set") ws = $(i + 1)
			}
		}
		END {
			printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%.1f,%.1f,%.2f,%s\n", app, scen, rep, frame, (frame == "" ? "" : sprintf("%.2f", cpu)),
				gpu, load, heap, ws, pss / 1024, rss / 1024, e - s, status
		}' >> "$OUT"
	tail -n 1 "$OUT" | awk -F, '{ printf "  %-8s %-3s rep %s: frame %s ms, CPU %s ms, load %s ms, peak PSS %s MB, wall %s s (%s)\n", $2, $1, $3, $4, $5, $7, $10, $12, $13 }'
	sleep "$COOLDOWN"
}

# med <column>: the median of a CSV column of $ROWS.
med() {
	echo "$ROWS" | cut -d, -f"$1" | grep . | sort -n | awk '{ v[NR] = $1 } END { if (NR == 0) print "-"; else if (NR % 2) print v[(NR + 1) / 2]; else printf "%.2f\n", (v[NR / 2] + v[NR / 2 + 1]) / 2 }'
}

# The medians per scenario and app (status ok only).
report() {
	echo
	printf "%-9s %-4s %9s %9s %9s %9s %10s %8s %4s\n" scenario app "frame ms" "CPU ms" "load ms" "GC MB" "peak PSS" "wall s" runs
	for SCEN in $(echo "$SCENARIOS" | cut -d'|' -f1); do
		for APP in cpp cs; do
			ROWS=$(awk -F, -v a="$APP" -v s="$SCEN" '$1 == a && $2 == s && $13 == "ok"' "$OUT")
			[ -n "$ROWS" ] || { printf "%-9s %-4s %s\n" "$SCEN" "$APP" "no successful run"; continue; }
			printf "%-9s %-4s %9s %9s %9s %9s %10s %8s %4s\n" "$SCEN" "$APP" "$(med 4)" "$(med 5)" "$(med 7)" "$(med 8)" "$(med 10)" "$(med 12)" \
				"$(echo "$ROWS" | wc -l | tr -d ' ')"
		done
	done
	echo
	echo "CSV: $OUT (peak PSS/RSS in MB; frame includes the GPU wait, CPU excludes it)"
}

case $MDK_DIR in
	device:*) ;;
	*) [ -d "$MDK_DIR" ] || die "no folder $MDK_DIR" ;;
esac
[ "$("$ADB" get-state 2>/dev/null)" = device ] || die "no device: connect one with USB debugging on ($ADB devices)"
echo "Device: $(sh_adb getprop ro.product.manufacturer) $(sh_adb getprop ro.product.model), Android $(sh_adb getprop ro.build.version.release)"

setup $CPP_PKG "$CPP_APK"
setup $CS_PKG "$CS_APK"

# The screen on and unlocked during the runs; the user's stay-on setting restored however the script ends.
STAY_ON=$(sh_adb settings get global stay_on_while_plugged_in)
sh_adb svc power stayon true
sh_adb input keyevent KEYCODE_WAKEUP
sh_adb wm dismiss-keyguard > /dev/null 2>&1
trap 'sh_adb settings put global stay_on_while_plugged_in "${STAY_ON:-0}"; sh_adb am force-stop $CPP_PKG; sh_adb am force-stop $CS_PKG' EXIT
trap 'exit 130' INT TERM

echo "app,scenario,rep,frame_ms,cpu_ms,gpu_wait_ms,load_ms,gc_heap_mb,working_set_mb,peak_pss_mb,peak_rss_mb,wall_s,status" > "$OUT"
echo "$SCENARIOS" | while IFS='|' read -r SCEN OPTS; do
	echo "$SCEN: $OPTS"
	REP=1
	while [ $REP -le "$REPS" ]; do
		if [ $((REP % 2)) -eq 1 ]; then
			run cpp $CPP_PKG "$SCEN" "$OPTS" $REP
			run cs $CS_PKG "$SCEN" "$OPTS" $REP
		else
			run cs $CS_PKG "$SCEN" "$OPTS" $REP
			run cpp $CPP_PKG "$SCEN" "$OPTS" $REP
		fi
		REP=$((REP + 1))
	done
done

report
