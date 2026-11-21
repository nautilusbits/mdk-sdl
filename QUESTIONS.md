# Open questions (collected while porting; work continues with the default noted)

1. ~~Git~~ — answered: at final, C++ replaces C# on master of the mdk-sdl repo.
2. ~~SDL3 binaries~~ — resolved: SDL 3.5.0 vendored in third_party/SDL, built from source and
   linked statically.
3. ~~Matrix4x4 last-bit parity~~ — answered: behaviour parity is enough.
4. ~~macOS minimum 13.3~~ — answered: keep.
5. ~~glibc 2.34+~~ — answered: keep.
6. ~~Byte-identical saves~~ — answered: not needed.
7. ~~Malformed mod zips~~ — answered: leave as is.
8. ~~Snowboard run ends in MEAT_4~~ — fixed in C++: the stuck-board nudge collides now
   (the C# still has the bug).
9. ~~Android release key~~ — answered: user's mdk-release.keystore; gradle signs with
   ANDROID_KEYSTORE_FILE / ANDROID_KEYSTORE_PASS / ANDROID_KEY_ALIAS.
10. ~~Android not run~~ — resolved: benchmarked on a Xiaomi Pad 6 (tools/android_bench.sh).
11. ~~Executable name~~ — answered: `mdk-sdl` (`mdk-sdl.exe`).
