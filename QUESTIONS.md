# Open questions (collected while porting; work continues with the default noted)

1. Git: local repo `mdk-cpp` with local commits only. Push where? (Gitea `Emlynix/mdk-cpp`?
   GitHub?) — default: nothing pushed.
2. SDL3 binaries: taken from ppy.SDL3-CS 2026.722.0 (SDL 3.5.0 a8591d9), same as C#. Build SDL
   from source instead? — default: prebuilt.
3. Matrix4x4.Invert etc.: .NET uses SIMD paths on x64, so results differ in the last bits. Bit
   exact parity with C# isn't a goal; behaviour parity is. OK?
4. macOS minimum raised from 11.0 to 13.3 (libc++'s std::format needs it). .NET 10 needs 12+
   anyway. OK, or replace std::format to support older macOS?
5. Linux build needs glibc 2.34+ (the bundled libSDL3.so requires it: Ubuntu 22.04+, Debian 12+).
6. Saves (JSON): C++ reads C# saves; C++ writes keys in another order and floats as doubles
   (0.10000000149011612). Readable both ways. Byte-identical save files wanted? — default: no,
   may switch to ordered_json + float formatting later.
7. Mod zips: a stored entry with DOS time 00:00:00 and lying sizes makes miniz refuse the whole
   zip (C# refuses only that entry). Malformed zips only; left as is.
8. Upstream bug: C# test SnowboardRunTests.SecondRunEndsInMeat5 fails on mdk-sdl 1.2.6 itself
   (since e1fde55, platform footing on triangles). The C++ matches the C# exactly; the test is
   marked should_fail. Fix upstream (C#) and port the fix?
