#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Mdk.Game/Viewer.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Game::Viewer;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Level 3's HMO_9 hover ship (XGS): its pilot flies it down by the corridor; shot, an empty
// ship (set_targetable 2: flag 0x800000) waits for Kurt, then flies him through the low opening
// north (path 126468). Landed on it, Kurt stays on it (damp_gravity 0x469efc sets 0x573b8c;
// each step his XY is the ship's), so the opening's edge doesn't scrape him off.
namespace
{
    constexpr int Seed = 1;
    constexpr int Level = 3;
    constexpr float Step = Viewer::Step;
    constexpr const char* Arena = "HMO_9";
    constexpr const char* Ship = "XGS";
    // The pilot's ship lands with 30 hit points (its path done).
    constexpr int LandedHealth = 30;
    constexpr int LandSteps = 20 * 60;
    constexpr int WaitSteps = 2 * 60;
    constexpr int RideSteps = 4 * 60;
    // Kurt is put this far above the empty ship.
    constexpr float Drop = 3.f;
    // Past the opening (y 2680): the ship's way on north.
    constexpr float BeyondOpening = 2690.f;
    // The ship's deck above its origin (SURF's floor; its fins reach 3).
    constexpr float Deck = 0.35f;
    constexpr float DeckTolerance = 0.1f;
    const Vector3 Corridor(-178.15f, 2602.88f, -300.95f);

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    struct World
    {
        std::shared_ptr<ScriptRuntime> Runtime;
        std::shared_ptr<KurtPlayer> Kurt;
        std::shared_ptr<ArenaSpace> Space;
    };

    World Create()
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, Level);
        auto number = std::to_string(Level);
        auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto space = std::make_shared<ArenaSpace>();
        auto groups = std::make_shared<TriangleGroups>();
        for (const auto& arena : level->Arenas)
        {
            if (!level->IsReachable(arena->Name))
            {
                continue;
            }

            space->Add(arena);
            groups->Add(arena);
        }

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        return {std::make_shared<ScriptRuntime>(level, cmi, sprites, space, groups, mixer, kurt, Seed), kurt, space};
    }

    // Steps Kurt and the scripts as the viewer does, until done or steps run out.
    void Run(World& world, int steps, const std::function<bool()>& done = nullptr)
    {
        Input input;
        for (int i = 0; i < steps && !(done && done()); i++)
        {
            world.Kurt->Update(input, Step);
            world.Runtime->Update(Step);
            world.Space->SetSolid(world.Runtime->SolidArenas());
        }
    }
}

DATA_TEST_CASE("HoverShipTests.ShipFliesKurtThroughTheOpening")
{
    auto world = Create();
    auto& runtime = *world.Runtime;
    runtime.TeleportKurt(Arena, Corridor, 90.f);
    Run(world, 1);
    auto pilot = runtime.FindObjectNamed(Ship);
    REQUIRE(pilot != nullptr);
    Run(world, LandSteps, [&] { return pilot->Health == LandedHealth; });

    // The pilot's death leaves the empty ship; Kurt lands on it.
    runtime.Kill(*pilot);
    Run(world, WaitSteps);
    auto ship = runtime.FindObjectNamed(Ship);
    REQUIRE(ship != nullptr);
    CHECK(pilot != ship);
    runtime.TeleportKurt(Arena, ship->Position + Vector3(0.f, 0.f, Drop), 90.f);
    Run(world, RideSteps);

    CHECK(ship->Position.Y > BeyondOpening);
    CHECK(world.Kurt->Platform == ship.get());
    CHECK(Vector2(ship->Position.X, ship->Position.Y) == Vector2(world.Kurt->Feet.X, world.Kurt->Feet.Y));
}

// damp_platform_floor (0x41d2c4) casts its ray at the platform's triangles (0x414668), not its
// box: Kurt stands on the deck, not level with the fins.
DATA_TEST_CASE("HoverShipTests.KurtStandsOnTheShipsDeck")
{
    auto world = Create();
    auto& runtime = *world.Runtime;
    runtime.TeleportKurt(Arena, Corridor, 90.f);
    Run(world, 1);
    auto pilot = runtime.FindObjectNamed(Ship);
    REQUIRE(pilot != nullptr);
    Run(world, LandSteps, [&] { return pilot->Health == LandedHealth; });
    runtime.Kill(*pilot);
    Run(world, WaitSteps);
    auto ship = runtime.FindObjectNamed(Ship);
    REQUIRE(ship != nullptr);

    runtime.TeleportKurt(Arena, ship->Position + Vector3(0.f, 0.f, Drop), 90.f);
    Run(world, RideSteps / 2);

    CHECK(world.Kurt->Platform == ship.get());
    float height = world.Kurt->Feet.Z - ship->Position.Z;
    CHECK(height >= Deck - DeckTolerance);
    CHECK(height <= Deck + DeckTolerance);
}
