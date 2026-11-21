# Porting rules (C# mdk-sdl → C++ mdk-cpp)

Source of truth: `/work/MDK/mdk-sdl` (C#, version 1.2.6). Goal: a 1:1 port — same behaviour,
same structure, same names — so the two can be compared file by file. Not a redesign.

## Layout

- `src/Mdk.Formats/Arena.cs` → `src/formats/Arena.h` + `Arena.cpp`. Folders lower case (`Mdk.Game/HdTextures` →
  `game/hd_textures`), file names = the C# file names.
- Partial classes (`Kurt.cs`, `Kurt.Combat.cs`): one header `Kurt.h` declaring the whole class,
  one `.cpp` per C# file (`Kurt.cpp`, `Kurt.Combat.cpp`).
- Several small types in one `.cs` stay together in one header.
- A `.cs` holding only types (enums, records) → header only.
- Namespaces mirror C#: `Mdk.Game.Objects` → `namespace Mdk::Game::Objects`.
- Includes are rooted at `src/`: `#include "formats/Arena.h"`.
- CMake globs `src/<Layer>/**/*.cpp`, so new files need no CMake edits.
- Tests: `tests/formats/ArenaTests.cpp` (doctest, `#include <doctest.h>`). One file
  per C# test file; `[Fact]` → `TEST_CASE("Class.Method")`, `[Theory]` + `InlineData` →
  one `TEST_CASE` looping over the data (or `SUBCASE`). `[DataFact]` (needs the game data) →
  skip at run time when the data isn't found (see `tests/Shared`).

## Core (`src/core`, namespace `Mdk`) — use it, don't reinvent

- `Numerics.h`: `Vector2/3/4`, `Matrix4x4`, `Matrix3x2`, `Quaternion` with .NET's formulas and
  row-vector convention. Static members are functions: `Vector3.Zero` → `Vector3::Zero()`,
  `Vector3.UnitZ` → `Vector3::UnitZ()`, `Matrix4x4.Identity` → `Matrix4x4::Identity()`.
  `m.Translation` → `m.Translation()` / `m.SetTranslation(v)`. `Matrix4x4.Invert(m, out r)` →
  `Matrix4x4::Invert(m, r)`.
- `DotNet.h`: `MathF::`, `Math::` (C# semantics: `Round` is banker's — **never** `std::round`
  for `MathF.Round`; `Max/Min` NaN rules; `Sign` throws on NaN), `Mdk::Random` (seeded
  sequences identical to .NET), `Str::` string helpers, exception types
  (`InvalidDataException`…), `byte/ushort/uint` aliases, `TickCount64`, `GetEnv`.
- Other Core helpers (read the header before use): `Dictionary.h` (**`Mdk::Dictionary` — .NET
  Dictionary with its exact iteration order; use it for every C# `Dictionary` that is iterated**),
  `Files.h` (File/Directory/Path), `Console.h`, `Drawing.h` (RectangleF…), `TimeSpan.h`,
  `Cancellation.h`, `Uri.h`, `FileStream.h`, `Sha256.h` (+ `Convert` hex), `ZipFile.h`,
  `Process.h` (+ `AppDomain` exit hooks), `Flags.h`, `Embedded.h`.
- Need a missing BCL helper? Add it to a **new** file in `src/core/` named after the
  feature (e.g. `Sha256.h`), never edit `Numerics.*`/`DotNet.*` (other agents work in parallel).
  If you must, append only, and say so in your report.

## Semantics that differ — get these right

| C# | C++ |
|---|---|
| `(int)someFloat`, `(uint)f`, `(short)f` | `ToInt(f)` / `SatCast<uint>(f)` (C# saturates, C++ is UB) |
| `MathF.Round(x)` | `MathF::Round(x)` (to even) |
| `MathF.Round(x, MidpointRounding.AwayFromZero)` | `MathF::RoundAwayFromZero(x)` |
| `x % y` on floats | `std::fmod(x, y)` |
| `a << n` with n ≥ 32 | C# masks: `a << (n & 31)` |
| int overflow (unchecked) | fine: built with `-fwrapv` |
| `uint`/`int` mixed arithmetic | C# promotes `int op uint` to `long`! Cast explicitly to keep values |
| `byte + byte` | C# gives `int`; same in C++ but watch `auto` |
| `char` (UTF-16) | `char` in UTF-8 `std::string` (game text is ASCII) |
| `string` | `std::string` (value). `string?` → `std::optional<std::string>` or empty when the code treats null and "" alike |
| `List<T>` | `std::vector<T>` |
| `Dictionary<K,V>` | `Mdk::Dictionary<K,V>` (exact .NET order) when iterated; `std::unordered_map` if only looked up |
| `HashSet<T>` | `std::unordered_set` (same caveat) |
| `SortedDictionary` | `std::map` |
| `Span<T>`, `ReadOnlySpan<T>`, `T[]` params | `std::span<T>` / `std::span<const T>` |
| `byte[]` owned | `std::vector<byte>` |
| `stackalloc` | `std::array` or small `std::vector` |
| `IEnumerable<T>` + `yield return` | return `std::vector<T>` (or a callback when lazy order matters) |
| LINQ | plain loops |
| `foreach` over a collection being modified | copy first, as C# would throw |
| `event`/`Action`/`Func` | `std::function` |
| `interface` | abstract class with pure virtuals, virtual dtor |
| `x is Foo f` on classes | `dynamic_cast<Foo*>` |
| `switch` expression | `switch` statement or lambda |
| `record`/`record struct` | `struct` with `bool operator==(const X&) const = default;` — `with { A = 1 }` → copy then assign |
| `class` objects shared between owners | `std::shared_ptr<T>` when ownership is truly shared/unclear; `std::unique_ptr<T>` for a single owner; raw `T*` for non-owning back-references. Value types (`struct`) by value |
| `T?` on class refs | `T*` (non-owning) / `std::shared_ptr<T>` (nullable owned) |
| `T?` on value types | `std::optional<T>` |
| `out`/`ref` params | references |
| properties | `T Name() const` getters, `void SetName(T)` setters; trivial auto-properties `{ get; set; }` → public fields with the C# name |
| `readonly`/`const` fields | `const`/`constexpr`/`static constexpr` |
| `static class` | `namespace`-scoped free functions inside a `struct`/`class` with static members (keep `Class::Method` call shape) |
| `IDisposable`/`using` | RAII destructor |
| `lock` | `std::mutex` + `std::lock_guard` |
| `Task`/threads | `std::thread` / `std::async` |
| exceptions | C++ exceptions (same types from `DotNet.h`, `std::exception` for `Exception`) |
| `string.Format`/interpolation | `std::format` (invariant culture: `{:.2f}` for `F2`/`0.00`; plain float `{}` → use `Str::FromFloat` when the text is visible or saved) |
| `File.ReadAllBytes`, `Path.Combine`, `Directory.*` | `std::filesystem` + fstream (small helpers in `src/core/Files.h`, add if missing) |
| `Console.WriteLine`/logging | whatever the C# logs through (port the C# log type) |
| `Debug.Assert` | `assert` |
| `nameof(X)` | `"X"` |
| `default` | value-init `{}` |

## Style (the user's rules)

- Mdk.Engine (already ported, `src/engine`) conventions to match: destructors replace
  `Dispose`; settable properties are public fields, read-only computed ones getters (`Size()`).
- Mdk.Formats (ported) conventions: C# classes returned/held as `std::shared_ptr<T>` (null =
  C# null); records are structs by value (nullable → `std::optional`). File-backed objects hold
  `SharedBytes` (`BinReader.h`); `bni.Bytes` → `*bni->Bytes()`. Script operands are
  `Scripts::Operand` (variant). Use the same shapes in Game.
- Portability: no floating-point `std::from_chars` (missing on Apple) — use `Str::TryParse*`.
  Code must compile for linux-x64, win-x64 and osx-arm64.
- Keep C# names for types, methods, fields, constants, enums (PascalCase methods as in C#).
  Private fields: `_name` → `_name` (keep the underscore, identical spelling).
- Keep C# visibility (`public`/`private`/`internal`→`public`). Don't widen access.
- Always braces, even one-line `if`. Early return/continue, low indentation.
- Empty lines between logical blocks, as in the C#.
- Port comments with the code (`/// <summary>` → `//` above the declaration). Don't add
  commentary about the port itself except where C++ forces a different shape (one line, why).
- Constants instead of magic numbers — the C# already has them; keep them.
- Enums: `enum class` with the C# member names. `[Flags]` enums → `enum class` + the bit
  operators (`MDK_FLAGS(EnumName)` macro in `src/core/Flags.h`).
- Headers: `#pragma once`, minimal includes, forward-declare where it breaks cycles.
- No `using namespace` in headers.

## Process

- Translate whole files faithfully; don't skip "unimportant" code paths, don't stub, don't
  leave TODOs unless the dependency is in a later layer (then a `// TODO(port): <what>` and
  report it).
- Build after each few files: `./build.sh linux-x64 <target>` (targets: `mdk_formats`,
  `mdk_engine`, `mdk_game`, `mdk_formats_tests`, `mdk_game_tests`, `mdk`). Fix all errors.
- Run the tests you ported. The game data is at `/work/MDK` (`MDK_DATA_DIR=/work/MDK`).
- Don't touch other agents' folders. Questions you can't resolve: decide sensibly, note them
  in your report (one line each) — never stop to ask.
