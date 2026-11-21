#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include <doctest.h>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "formats/Model.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/Kurt.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/ScriptRuntime.h"
#include "game/scripts/SniperRounds.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Level 8's forklift puzzle (GUNT_2): the turret garage XPER's death brings a forklift
// with a driver (XFORK script 0x4bc6). Hits past 110 blow the driver and the canopy off; then each
// hit pushes it along the shot (push_hit_dir), its script giving it its health back. On the yellow
// pad (-435..-414, 547..568) it hides the glass (group 1) over the way down.

namespace
{
    // The scripts' random draws (the forklift's path, hits) repeat from run to run.
    constexpr int Seed = 1;
    constexpr int Level = 8;
    constexpr float Tick = 1.f / 30.f;
    const std::string Arena = "GUNT_2";
    const std::string Forklift = "XFORK";
    const std::string Driver = "XFK_HEAD";
    constexpr int Glass = 1;
    constexpr int BulletRound = 0;
    constexpr int BulletDamage = 8;
    // West of the garage, south of the pad: the forklift drives at him.
    constexpr Vector3 Stand(-440.f, 548.f, 8.f);
    constexpr Vector3 PadCenter(-424.5f, 557.5f, 8.f);
    const Formats::Box Pad{Vector3(-435.f, 547.f, 7.f), Vector3(-414.f, 568.f, 9.f)};
    constexpr int ArriveTicks = 60;
    constexpr int ShootTicks = 20 * 30;
    constexpr int FireEvery = 3;
    // Shots come from this far behind the forklift, aimed at its middle.
    constexpr float ShotRange = 20.f;
    constexpr float AimHeight = 4.f;
    constexpr float Still = 0.5f;
    // Shots from in front of a wall behind the forklift.
    constexpr float WallGap = 1.f;
    constexpr float DegreesPerRadian = 180.f / MathF::PI;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::tuple<std::shared_ptr<ScriptRuntime>, std::shared_ptr<TriangleGroups>> CreateRuntime()
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
        return {std::make_shared<ScriptRuntime>(level, cmi, sprites, space, groups, mixer, kurt, Seed), groups};
    }

    void Run(ScriptRuntime& runtime, int ticks)
    {
        for (int i = 0; i < ticks; i++)
        {
            runtime.Update(Tick);
        }
    }

    // Kurt by the pad, the garage killed: the forklift with the driver.
    std::shared_ptr<MdkObject> DrivenForklift(ScriptRuntime& runtime)
    {
        runtime.TeleportKurt(Arena, Stand, 0.f);
        Run(runtime, ArriveTicks);
        runtime.Kill(*runtime.FindObjectNamed("XPER"));
        Run(runtime, ArriveTicks);

        // Objects.Last(o => o.TypeName == Forklift && !o.Dead).
        for (auto it = runtime.Objects.rbegin(); it != runtime.Objects.rend(); ++it)
        {
            if ((*it)->TypeName == Forklift && !(*it)->Dead)
            {
                return *it;
            }
        }

        FAIL("no forklift");
        return nullptr;
    }

    // A bullet from eye at the forklift's middle.
    void Shoot(ScriptRuntime& runtime, Vector3 eye, const MdkObject& forklift)
    {
        auto to = forklift.Position + Vector3(0.f, 0.f, AimHeight) - eye;
        auto yaw = MathF::Atan2(to.Y, to.X) * DegreesPerRadian;
        auto pitch = -MathF::Atan2(to.Z, Vector2(to.X, to.Y).Length()) * DegreesPerRadian;
        runtime.SniperRounds->Fire(BulletRound, eye, yaw, pitch, nullptr);
    }

    // Shoots the forklift from Kurt until the driver is gone.
    void ShootDriver(ScriptRuntime& runtime, const MdkObject& forklift)
    {
        auto driver = 1 << forklift.FindPart(Driver);
        for (int i = 0; i < ShootTicks && !forklift.Dead && (forklift.HiddenParts & driver) == 0; i++)
        {
            if (i % FireEvery == 0)
            {
                Shoot(runtime, runtime.KurtPosition + Vector3(0.f, 0.f, AimHeight), forklift);
            }

            runtime.Update(Tick);
        }
    }

    bool OnPad(const MdkObject& forklift) { return Vector3::Clamp(forklift.Position, Pad.Min, Pad.Max) == forklift.Position; }
}

DATA_TEST_CASE("ForkliftTests.ShotsPushTheForkliftOntoThePad")
{
    auto [runtime, groups] = CreateRuntime();
    auto forklift = DrivenForklift(*runtime);
    ShootDriver(*runtime, *forklift);

    // One shot at a time from the far side, each once it stands still.
    for (int i = 0; i < ShootTicks && !OnPad(*forklift); i++)
    {
        if (Vector2(forklift->Velocity.X, forklift->Velocity.Y).Length() < Still)
        {
            auto flat = forklift->Position - PadCenter;
            flat.Z = 0.f;
            auto away = Vector3::Normalize(flat);
            auto middle = forklift->Position + Vector3(0.f, 0.f, AimHeight);
            auto eye = middle + away * ShotRange;
            if (auto wall = runtime->Raycast(middle, eye))
            {
                eye = middle + away * (Vector3::Distance(middle, wall->Point) - WallGap);
            }

            Shoot(*runtime, eye, *forklift);
        }

        runtime->Update(Tick);
    }

    runtime->Update(Tick);

    CHECK_FALSE(forklift->Dead);
    CHECK(OnPad(*forklift));
    auto glass = groups->Get(Arena, Glass);
    REQUIRE(glass != nullptr);
    CHECK((glass->State & TriangleGroups::State::Hidden) == TriangleGroups::State::Hidden);
}

DATA_TEST_CASE("ForkliftTests.OneHitHurtsTheForkliftAsUsual")
{
    auto [runtime, groups] = CreateRuntime();
    auto forklift = DrivenForklift(*runtime);
    auto health = forklift->Health;
    runtime->Lethality = Lethality::OneHit;

    // One bullet: its 8 hit points, far from the 110 that blow the driver off.
    Shoot(*runtime, runtime->KurtPosition + Vector3(0.f, 0.f, AimHeight), *forklift);
    Run(*runtime, ArriveTicks);

    CHECK(forklift->Health >= health - BulletDamage);
    CHECK(forklift->Health <= health - 1);
    CHECK((forklift->HiddenParts & (1 << forklift->FindPart(Driver))) == 0);
}

DATA_TEST_CASE("ForkliftTests.OneHitBlowsOnlyTheDriverOff")
{
    auto [runtime, groups] = CreateRuntime();
    auto forklift = DrivenForklift(*runtime);
    runtime->Lethality = Lethality::OneHit;

    ShootDriver(*runtime, *forklift);
    Run(*runtime, ArriveTicks);

    CHECK_FALSE(forklift->Dead);
    CHECK((forklift->HiddenParts & (1 << forklift->FindPart(Driver))) != 0);
}
