#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "Asserts.h"
#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "formats/MdkData.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/CutsceneCamera.h"
#include "game/kurt/Kurt.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/scripts/ScriptRuntime.h"
#include "shared/GameData.h"

using namespace Mdk;
using Mdk::Engine::Audio::AudioDevice;
using Mdk::Engine::Audio::Output;
using Mdk::Formats::Bni;
using Mdk::Formats::Cmi;
using Mdk::Formats::MdkData;
using Mdk::Game::CutsceneCamera;
using Mdk::Game::Audio::SoundMixer;
using Mdk::Game::Collision::ArenaSpace;
using Mdk::Game::Level::LevelData;
using Mdk::Game::Level::TriangleGroups;
using Mdk::Game::Scripts::ScriptRuntime;

// The cutscene camera (0x477d94, get_cutscene_camera) and camera_track's pitch.

namespace
{
    constexpr int Level = 7;
    constexpr const char* Arena = "DANT_1";
    constexpr const char* Model = "SW_H150";
    // special_event 93: the boss shot, a fixed camera looking 3 units above the target.
    constexpr int BossEvent = 93;
    constexpr Vector3 BossCamera{1158.f, 5006.f, 315.f};
    constexpr float LookAbove = 3.f;
    constexpr float Precision = 1e-3f;
}

TEST_CASE("CutsceneCameraTests.YawIsNinetyMinusHeadingAndPitchLooksDown")
{
    struct Row
    {
        float Yaw, Pitch, X, Y, Z;
    };

    const Row rows[] = {
        {0.f, 0.f, 0.f, 1.f, 0.f},
        {90.f, 0.f, 1.f, 0.f, 0.f},
        {0.f, 90.f, 0.f, 0.f, -1.f},
    };

    for (const auto& row : rows)
    {
        CAPTURE(row.Yaw);
        CAPTURE(row.Pitch);
        auto forward = CutsceneCamera::ForwardOf(row.Yaw, row.Pitch);
        CHECK_NEAR(row.X, forward.X, Precision);
        CHECK_NEAR(row.Y, forward.Y, Precision);
        CHECK_NEAR(row.Z, forward.Z, Precision);
    }
}

DATA_TEST_CASE("CutsceneCameraTests.BossShotLooksAtTheTarget")
{
    auto data = MdkData::Find();
    auto level = std::make_shared<LevelData>(*data, Level);
    auto cmi = Cmi::Load(data->PathOf("TRAVERSE/LEVEL" + std::to_string(Level) + "/LEVEL" + std::to_string(Level) + ".CMI"));
    auto sprites = Bni::Load(data->PathOf("TRAVERSE/TRAVSPRT.BNI"));
    auto space = std::make_shared<ArenaSpace>();
    AudioDevice device(Output::Muted);
    auto mixer = std::make_shared<SoundMixer>(device, [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
    auto kurt = std::make_shared<Game::Kurt::Kurt>(*space, *mixer, [](Game::Kurt::Kurt::State) { return 1; });
    ScriptRuntime runtime(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    auto target = runtime.Spawn(*runtime.GetArenaState(Arena)->Controller, Model, Vector3(1100.f, 4900.f, 300.f), 0.f, -1, 0,
        ScriptRuntime::Spawning::Plain);
    REQUIRE(target != nullptr);

    runtime.SpecialEvent(*target, BossEvent);
    CHECK_FALSE(CutsceneCamera::Active(runtime));
    runtime.Update(ScriptRuntime::Tick);
    CHECK(CutsceneCamera::Active(runtime));

    CutsceneCamera camera;
    camera.Update(runtime);
    auto expected = Vector3::Normalize(target->Position + Vector3::UnitZ() * LookAbove - BossCamera);
    CHECK(BossCamera == camera.Position);
    CHECK_NEAR(expected.X, camera.Forward.X, Precision);
    CHECK_NEAR(expected.Y, camera.Forward.Y, Precision);
    CHECK_NEAR(expected.Z, camera.Forward.Z, Precision);
}
