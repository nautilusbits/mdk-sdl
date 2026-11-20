#include <doctest.h>

#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/GameStats.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Shared/GameData.h"

#include <memory>
#include <stdexcept>
#include <string>

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Formats;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;

// The console's onehit: Kurt's hits kill enemies; damage to Kurt is unchanged.
namespace
{
    constexpr int Level = 3;
    constexpr float Tick = 1.f / 30.f;
    constexpr int SettleTicks = 30;
    // Kurt stands this far from the grunt, facing it.
    constexpr float Range = 25.f;
    // Among level 3's first grunts (tests/snapshot_test.sh).
    const std::string Arena = "HMO_1";
    const Vector3 Grunts(-2.f, 172.f, 135.f);

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
        auto cmi = Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto space = std::make_shared<ArenaSpace>();
        for (auto& arena : level->Arenas)
        {
            if (!level->IsReachable(arena->Name))
            {
                continue;
            }

            space->Add(arena);
        }

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<Mdk::Game::Kurt::Kurt>(*space, *mixer, [](auto) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }

    // Kurt next to a grunt of the start arena, aiming at it; the grunt.
    std::shared_ptr<MdkObject> FaceGrunt(ScriptRuntime& runtime)
    {
        runtime.TeleportKurt(Arena, Grunts, 0.f);
        for (int i = 0; i < SettleTicks; i++)
        {
            runtime.Update(Tick);
        }

        std::shared_ptr<MdkObject> grunt;
        for (auto& o : runtime.Objects)
        {
            if (!o->Dead && o->Health > 1 && o->Arena == runtime.CurrentArena
                && GameStats::IsEnemy(o->TypeName) && (o->Flags & MdkObject::FlagWeakParts) == 0)
            {
                grunt = o;
                break;
            }
        }

        if (grunt == nullptr)
        {
            throw std::runtime_error("No grunt");
        }

        auto box = runtime.GetWorldBounds(*grunt);
        auto center = (box.Min + box.Max) * 0.5f;
        Vector3 at(center.X - Range, center.Y, grunt->Position.Z);
        runtime.TeleportKurt(runtime.CurrentArena, at, 0.f);
        runtime.KurtPosition = at;
        runtime.TargetYaw = 0.f;
        return grunt;
    }
}

DATA_TEST_CASE("OneHitTests.OneChainGunHitKills")
{
    auto runtime = CreateRuntime();
    auto grunt = FaceGrunt(*runtime);
    runtime->Lethality = Lethality::OneHit;

    runtime->FireChainGun();
    runtime->Update(Tick);

    CHECK(grunt->Health == 0);
}

DATA_TEST_CASE("OneHitTests.ChainGunHitHurtsWithoutOneHit")
{
    auto runtime = CreateRuntime();
    auto grunt = FaceGrunt(*runtime);
    auto health = grunt->Health;

    runtime->FireChainGun();

    CHECK(grunt->Health >= 1);
    CHECK(grunt->Health <= health - 1);
}

DATA_TEST_CASE("OneHitTests.KurtIsHurtAsUsual")
{
    auto runtime = CreateRuntime();
    runtime->Lethality = Lethality::OneHit;
    auto health = runtime->Kurt->Health;

    runtime->HurtKurt(1);

    CHECK(runtime->Kurt->Health == health - 1);
}
