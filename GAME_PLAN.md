# Mdk.Game port plan (3 agents, 2 rounds)

Groups (C# folders under /work/MDK/mdk-sdl/src/Mdk.Game):
- G1 Scripts: Scripts/ (25 files, ~12.6k lines; ScriptRuntime*, ScriptVm, ObjectMotion, Items…)
- G2 World:   Kurt/, Objects/, Level/, Collision/, Audio/, Stream/, Fall/ (60 files, ~13.5k)
- G3 Shell:   Flow/, Menu/, Hud/, DevTools/, Mods/, HdTextures/, root files incl. Viewer.cs (84 files, ~14k)

Round 1 — headers: every .cs of the group → .h with the complete declaration (all members,
constants, nested types, inline trivial bodies only), 1:1 per PORTING.md. Other groups' types:
`#include "Mdk.Game/<Folder>/<File>.h"` by the C# file that declares them (the path is
deterministic) or forward-declare. Don't write other groups' headers.
Round 2 — .cpp files, then the group's tests.

Shared decisions (so headers from different agents fit):
- Every non-static C# `class` instance (Game types) held in fields/collections:
  `std::shared_ptr<T>`; parameters `T&` (non-null) or `T*` (nullable) or the shared_ptr when the
  callee stores it. Back-references to an owner (object → viewer/runtime/level) are raw `T*`.
- Records / record structs / structs: by value; nullable → `std::optional<T>`.
- Auto-properties with get+set (any setter visibility) → public field, C# name.
  Get-only auto-property initialised in ctor → public field too (const-ness not enforced).
  Computed property (`=>` or get body) → `Name()` const method; setter with body → `SetName(v)`.
- Events / delegates → `std::function` fields with the C# name.
- Static members → static members; static classes → struct with static members.
- Interfaces (ISoundSource, ICommandTarget, Solids.ISurface, Screen…) → abstract classes,
  virtual dtor. A class implementing an interface inherits it publicly.
- Enums: `enum class` in the header where C# declares them.
