# MDK in C++ and SDL3

A C++20 port of [mdk-sdl](https://github.com/nemo22/mdk-sdl) 1.2.6, the C#/SDL3 port of
[MDK](https://en.wikipedia.org/wiki/MDK_(video_game)) (Shiny Entertainment, 1997). It is a 1:1
translation: same structure, file names and behaviour, so both can be compared file by file
(rules in [PORTING.md](PORTING.md)). Same features as mdk-sdl: the whole game, original and
enhanced look, stereo 3D, gamepads, HD textures mod, modding, quick save, console, cheats.

> **You need the original game to play.** This port contains no data from MDK. It reads the
> levels, textures, sounds and videos at run time from your own installed copy (GOG, Steam or CD).

## Running

One executable per platform, `mdk-sdl` (`mdk-sdl.exe` on Windows), with SDL3's library next to it.
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
Android SDK, dxc and spirv-cross for the shaders, all under `/work/.toolchain`):

```
./build.sh linux-x64 [targets...]    # also win-x64, osx-arm64, osx-x64; output in build/<platform>
tools/publish.sh [platforms...]       # release packages into build/publish/
```

Targets: `mdk` (the game), `Mdk.Core`, `Mdk.Formats`, `Mdk.Engine`, `Mdk.Game` and the test
programs. `BUILD_TYPE=Debug` and `MDK_BUILD_DIR` change the configuration and folder. Release
builds are stripped. Android: the Gradle project in `android/`.

Layers (each uses only the one below): `Mdk.App` → `Mdk.Game` → `Mdk.Engine` (SDL3) →
`Mdk.Formats` → `Mdk.Core` (.NET semantics: banker's rounding, saturating casts, .NET's
`Random`, dictionary order).

## Tests

- Unit tests (doctest, ported from mdk-sdl's xUnit tests): `Mdk.Core.Tests`, `Mdk.Formats.Tests`,
  `Mdk.Game.Tests`; run `MDK_DATA_DIR=/path/to/MDK build/linux-x64/Mdk.Game.Tests`. Tests that need
  the game data skip without it.
- Parity with the C# build: `tools/parity.sh` renders the same frames with both and compares them
  pixel for pixel (headless, lavapipe).
- Integration scripts: `tests/*.sh` (ported from mdk-sdl's).

## Status

Complete port of mdk-sdl 1.2.6; unit tests pass (one upstream failure kept as expected) and
level screenshots match the C# build. Open questions: [QUESTIONS.md](QUESTIONS.md).

## Legal

An unofficial fan project, not affiliated with Shiny Entertainment or the rights holders of MDK.
It contains only code; the game's data belongs to its owners. Licence: GPL-3.0, as mdk-sdl.
