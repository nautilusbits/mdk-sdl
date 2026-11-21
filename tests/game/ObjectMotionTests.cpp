#include <doctest.h>

#include "engine/audio/AudioDevice.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/Kurt.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/ObjectMotion.h"
#include "game/scripts/ScriptRuntime.h"
#include "shared/GameData.h"
#include "Asserts.h"

#include <array>
#include <memory>
#include <string>

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using Mdk::Formats::Bni;
using Mdk::Formats::Cmi;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;

// Object moves through level 7's start arena (DANT_1, whose floor is at z 0).
namespace
{
    constexpr int Level = 7;
    const std::string Arena = "DANT_1";
    // A model of the level, on the floor where the runner pickup waits.
    const std::string Model = "SW_H150";
    const Vector3 OnTheFloor(-11.f, -13.f, 0.5f);
    constexpr int Ticks = 5;

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
        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<Mdk::Game::Kurt::Kurt>(*space, *mixer, [](auto) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }
}

// An object thrown along the floor slides on it and touches it (a grenade lands).
DATA_TEST_CASE("ObjectMotionTests.ObjectSlidingAlongTheFloorTouchesIt")
{
    auto runtime = CreateRuntime();
    auto obj = runtime->Spawn(*runtime->GetArenaState(Arena)->Controller, Model, OnTheFloor, 0.f, -1, 0, ScriptRuntime::Spawning::Plain);
    REQUIRE(obj != nullptr);
    obj->Flags = MdkObject::FlagGravity | MdkObject::FlagCollides;
    obj->Friction = 0.f;
    obj->Velocity = Vector3(20.f, 0.f, -5.f);

    // It lands on the third tick, then slides on.
    for (int i = 0; i < Ticks; i++)
    {
        runtime->Motion->Update(*obj);
    }

    CHECK(obj->Position.X > OnTheFloor.X);
    CHECK((obj->ContactFlags & MdkObject::ContactFloor) != 0);
}

// A rolling object's basis stays a rotation however long it rolls (object_motion.gd
// orthonormalises it each tick): no drift skews or scales the model.
DATA_TEST_CASE("ObjectMotionTests.RollingStaysOrthonormal")
{
    constexpr int RollTicks = 20000;
    constexpr float Tolerance = 1e-5f;
    auto runtime = CreateRuntime();
    auto obj = runtime->Spawn(*runtime->GetArenaState(Arena)->Controller, Model, OnTheFloor, 0.f, -1, 0, ScriptRuntime::Spawning::Plain);
    REQUIRE(obj != nullptr);
    obj->Flags = MdkObject::FlagRolling;
    obj->Friction = 0.f;
    obj->RollRadius = 0.37f;

    for (int i = 0; i < RollTicks; i++)
    {
        // Turning, so the axis changes.
        obj->Velocity = Vector3(MathF::Cos(i * 0.01f), MathF::Sin(i * 0.01f), 0.f) * 30.f;
        runtime->Motion->Update(*obj);
    }

    auto b = obj->RollingBasis;
    std::array<Vector3, 3> rows{Vector3(b.M11, b.M12, b.M13), Vector3(b.M21, b.M22, b.M23), Vector3(b.M31, b.M32, b.M33)};
    for (auto row : rows)
    {
        CHECK_NEAR(1.f, row.Length(), Tolerance);
    }

    CHECK_NEAR(0.f, Vector3::Dot(rows[0], rows[1]), Tolerance);
    CHECK_NEAR(0.f, Vector3::Dot(rows[1], rows[2]), Tolerance);
    CHECK_NEAR(0.f, Vector3::Dot(rows[0], rows[2]), Tolerance);
}
