# MDK in C++ and SDL3

A C++20 port of [mdk-sdl](https://github.com/nemo22/mdk-sdl) 1.2.6, the C#/SDL3 port of
[MDK](https://en.wikipedia.org/wiki/MDK_(video_game)) (Shiny Entertainment, 1997). It is a 1:1
translation: same structure, file names and behaviour, so both can be compared file by file
(rules in [PORTING.md](PORTING.md)). Same features as mdk-sdl: the whole game, original and
enhanced look, stereo 3D, gamepads, HD textures mod, modding, quick save, console, cheats.

> **You need the original game to play.** This port contains no data from MDK. It reads the
> levels, textures, sounds and videos at run time from your own installed copy (GOG, Steam or CD).

## Running

One executable per platform, `mdk-sdl` (`mdk-sdl.exe` on Windows), self-contained: SDL3 is linked in.
The game data is found like mdk-sdl's: the program inside the MDK installation folder, MDK in the
default GOG or Steam location, the `MDK_DATA_DIR` environment variable, or `mdk_paths.cfg` next
to the program:

```
[paths]
mdk="C:/GOG Games/MDK"
beta="C:/Games/MDK (1996-08-06) (beta demo)"
```

Options, keys, the console and the beta demo work as described in mdk-sdl's README.
Linux needs glibc 2.34+ (Ubuntu 22.04, Debian 12); macOS 13.3+.

## Building

On this box (zig as the cross compiler for every desktop platform, CMake + Ninja from the
Android SDK (3.31 for SDL on Apple), dxc and spirv-cross for the shaders, all under `/work/.toolchain`):

```
./build.sh linux-x64 [targets...]    # also win-x64, osx-arm64, osx-x64; output in build/<platform>
tools/publish.sh [platforms...]       # release packages into build/publish/
```

Targets: `mdk` (the game), `mdk_core`, `mdk_formats`, `mdk_engine`, `mdk_game` and the test
programs. `BUILD_TYPE=Debug` and `MDK_BUILD_DIR` change the configuration and folder. Release
builds are stripped. Android: the Gradle project in `android/`.

SDL 3.5.0 (a8591d9, zlib licence) is built from `third_party/SDL` and linked statically, SDL's
C code inside `libmain.so` on Android. On Linux it still loads X11, Wayland and the audio
libraries at run time; their headers come from Ubuntu packages in
`/work/.toolchain/sysroot-headers`. `-DMDK_STATIC_SDL=OFF` (Gradle: `-PsharedSdl=true`) uses
the prebuilt shared library in `third_party/sdl-bin` instead.

Layers (each uses only the one below): `src/app` → `src/game` → `src/engine` (SDL3) →
`src/formats` → `src/core` (.NET semantics: banker's rounding, saturating casts, .NET's
`Random`, dictionary order).

## Tests

- Unit tests (doctest, ported from mdk-sdl's xUnit tests): `mdk_core_tests`, `mdk_formats_tests`,
  `mdk_game_tests` (sources in `tests/core`, `tests/formats`, `tests/game`); run `MDK_DATA_DIR=/path/to/MDK build/linux-x64/mdk_game_tests`. Tests that need
  the game data skip without it.
- Parity with the C# build: `tools/parity.sh` renders the same frames with both and compares them
  pixel for pixel (headless, lavapipe).
- Integration scripts: `tests/*.sh` (ported from mdk-sdl's).
- Android against C# on a phone: `tools/android_bench.sh /path/to/MDK [reps]` (adb; `device:/sdcard/Download/MDK`
  copies the game folders from the device itself). It installs two
  bench APKs beside the real MDK (`io.github.nemo22.mdk.bench.cpp` / `.cs`, in `build/publish/` as
  `mdk-bench-cpp.apk`, `mdk-bench-cs.apk`), pushes the game files into each, runs the desktop's perf
  scenarios (intent extra `options`, e.g. `--level=7 --perf=10 --wait=40`; output to logcat, tag `MDK`)
  and prints the medians: frame, CPU, load, memory, wall time; CSV in `build/android_bench.csv`. C++ APK:
  `./gradlew assembleStandardRelease -PbenchId=io.github.nemo22.mdk.bench.cpp` in `android/`; C# APK: a copy
  of mdk-sdl with the same extra, `dotnet publish src/Mdk.Android -c Release -p:ApplicationId=io.github.nemo22.mdk.bench.cs`.

## Status

Version 2.0.0: complete port of mdk-sdl 1.2.6; unit tests pass (one upstream failure kept as expected) and
level screenshots match the C# build. Open questions: [QUESTIONS.md](QUESTIONS.md).

## Legal

An unofficial fan project, not affiliated with Shiny Entertainment or the rights holders of MDK.
It contains only code; the game's data belongs to its owners. Licence: GPL-3.0, as mdk-sdl.
