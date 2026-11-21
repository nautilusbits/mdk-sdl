#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/Kurt.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/ScriptRuntime.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Issue #36: LEVEL7 DANT_6's cannon XD6GUN blew up as soon as Kurt came near. Kurt in
// (383..667, 3822..4215) makes the arena spawn four XG in the windows, each counting itself in
// var 1.0; the cannon dies once that count is 0. The original puts a new object at the head of
// its arena's list (0x45fd4c), so the XG count themselves before the cannon looks.
namespace
{
    constexpr int Level = 7;
    constexpr const char* Arena = "DANT_6";
    constexpr const char* Gun = "XD6GUN";
    constexpr const char* Alien = "XG";
    constexpr int Aliens = 4;
    constexpr float Tick = 1.0f / 30.0f;
    // In the arena's trigger rect, before the building.
    const Vector3 Stand{450.0f, 3900.0f, -89.0f};
    constexpr int Ticks = 90;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime()
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
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, groups, mixer, kurt);
    }
}

DATA_TEST_CASE("WindowGunTests.GunOutlivesTheAliensArrival")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Arena, Stand, 0.0f);
    for (int i = 0; i < Ticks; i++)
    {
        runtime->Update(Tick);
    }

    auto aliens = std::count_if(runtime->Objects.begin(), runtime->Objects.end(),
        [](const auto& o) { return o->TypeName == Alien && o->Arena == Arena && !o->Dead; });
    CHECK(aliens == Aliens);
    CHECK(runtime->FindObjectNamed(Gun) != nullptr);
}
