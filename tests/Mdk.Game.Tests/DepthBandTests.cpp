#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Render/DepthBands.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/ArenaBand.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Shared/GameData.h"

using namespace Mdk;
using Mdk::Engine::Audio::AudioDevice;
using Mdk::Engine::Audio::Output;
using Mdk::Engine::Render::DepthBand;
using Mdk::Engine::Render::DepthBands;
using Mdk::Formats::Bni;
using Mdk::Formats::Cmi;
using Mdk::Game::Audio::SoundMixer;
using Mdk::Game::Collision::ArenaSpace;
using Mdk::Game::Level::ArenaBand;
using Mdk::Game::Level::LevelData;
using Mdk::Game::Level::TriangleGroups;
using Mdk::Game::Scripts::ScriptRuntime;
using Mdk::Tests::GameData;

// Found by playtest: at LEVEL4's snowboard ride end (CMEAT_1 by the tube to MEAT_3) Kurt
// looked half sunk: MEAT_3's flat shell lies above CMEAT_1's floor and won the depth test. The
// original has no depth buffer: it draws the other arena first and the camera's over it (0x41df35),
// so the camera's arena covers the other one.

namespace
{
    constexpr int Level = 4;
    constexpr const char* Tube = "CMEAT_1";
    constexpr const char* Shell = "MEAT_3";
    constexpr Vector3 Feet{62.35f, 2582.81f, -392.90f};
    // The follow camera behind Kurt there (--trace).
    constexpr Vector3 Camera{62.35f, 2574.84f, -387.84f};

    constexpr const char* Room = "MEAT_7";
    constexpr const char* Corridor = "CMEAT_7";
    constexpr float Doorway = 14822.f;
    constexpr float Floor = 21.f;

    // C#'s static AudioDevice: one muted device for the whole class.
    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime()
    {
        const auto& data = *GameData::Data();
        auto space = std::make_shared<ArenaSpace>();
        auto level = std::make_shared<LevelData>(data, Level);
        auto cmi = Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + std::to_string(Level) + "/LEVEL" + std::to_string(Level) + ".CMI"));
        auto sprites = Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        for (const auto& arena : level->Arenas)
        {
            if (!level->IsReachable(arena->Name))
            {
                continue;
            }

            space->Add(arena);
        }

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<Game::Kurt::Kurt>(*space, *mixer, [](Game::Kurt::Kurt::State) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }

    bool Contains(const std::vector<std::string>& names, const std::string& name)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }
}

TEST_CASE("DepthBandTests.FrontIsNearerThanBack")
{
    auto front = DepthBands::Of(DepthBand::Front);
    auto back = DepthBands::Of(DepthBand::Back);
    CHECK(0.f == front.Min);
    CHECK(front.Max <= back.Min);
    CHECK(1.f == back.Max);
}

TEST_CASE("DepthBandTests.CameraArenaIsInFront")
{
    CHECK(DepthBand::Front == ArenaBand::Of(Tube, Tube));
    CHECK(DepthBand::Back == ArenaBand::Of(Shell, Tube));
}

DATA_TEST_CASE("DepthBandTests.TubeCoversTheShell")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Tube, Feet, 90.f);
    runtime->Update(ScriptRuntime::Tick);

    CHECK(Contains(runtime->DrawnArenas(), Shell));
    CHECK(std::string(Tube) == runtime->CameraArena(Camera));
}

// Kurt just through a doorway, the camera still behind it: the arena left is the
// camera's (0x490db4), drawn over Kurt's.
DATA_TEST_CASE("DepthBandTests.CameraBehindTheDoorway")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Room, Vector3(0.f, Doorway - 100.f, Floor), 90.f);
    runtime->Update(ScriptRuntime::Tick);
    runtime->Kurt->Teleport(Vector3(0.f, Doorway + 3.f, Floor), runtime->Kurt->Yaw);
    runtime->Update(ScriptRuntime::Tick);
    CHECK(std::string(Corridor) == runtime->CurrentArena);

    CHECK(std::string(Room) == runtime->CameraArena(Vector3(0.f, Doorway - 5.f, Floor + 5.f)));
    CHECK(std::string(Corridor) == runtime->CameraArena(Vector3(0.f, Doorway + 1.f, Floor + 5.f)));
}
