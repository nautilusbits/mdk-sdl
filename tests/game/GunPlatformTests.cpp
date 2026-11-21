#include <cmath>
#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "Asserts.h"
#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "engine/platform/Input.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/Kurt.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/ScriptRuntime.h"
#include "game/Viewer.h"
#include "shared/GameData.h"

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

// Level 6's OLYM_4 gun platforms (XBGUN): one keeps Kurt (set_targetable 2: flag 0x800000);
// fired from, it flies him into COLYM_4. Kept, his XY is the platform's each step, in the air
// too (0x46634e), so a jump on it doesn't leave him behind (issue #40).
namespace
{
    constexpr int Level = 6;
    constexpr const char* Arena = "OLYM_4";
    constexpr const char* Gun = "XBGUN";
    // The keeping gun of the row (x -1732).
    constexpr float KeeperX = -1732.f;
    constexpr float KeeperReach = 10.f;
    const Vector3 Above(0.f, 0.f, 12.f);
    constexpr int SettleSteps = 60;
    // Firing this long, the gun is on its way.
    constexpr int FireSteps = 150;
    constexpr int JumpSteps = 60;
    constexpr float Tolerance = 0.01f;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<SoundMixer> Mixer()
    {
        return std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
    }
}

DATA_TEST_CASE("GunPlatformTests.AJumpOnTheFlyingGunStaysOverIt")
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

    auto kurtMixer = Mixer();
    auto kurt = std::make_shared<KurtPlayer>(*space, *kurtMixer, [](auto) { return 1; });
    auto runtime = std::make_shared<ScriptRuntime>(level, cmi, sprites, space, groups, Mixer(), kurt, 1);
    Input input;
    auto step = [&](int steps)
    {
        for (int i = 0; i < steps; i++)
        {
            kurt->Update(input, Viewer::Step);
            runtime->Update(Viewer::Step);
            space->SetSolid(runtime->SolidArenas());
        }
    };

    runtime->TeleportKurt(Arena, Vector3::Zero(), 0.f);
    step(2);
    std::shared_ptr<MdkObject> gun;
    for (const auto& o : runtime->Objects)
    {
        if (o->TypeName == Gun && std::fabs(o->Position.X - KeeperX) < KeeperReach)
        {
            gun = o;
            break;
        }
    }

    REQUIRE(gun != nullptr);
    runtime->TeleportKurt(Arena, gun->Position + Above, 0.f);
    step(SettleSteps);
    input.Hold(Key::Fire, Input::State::Down);
    step(FireSteps);

    // A jump while it flies: Kurt is over the gun every step.
    input.Hold(Key::Jump, Input::State::Down);
    int airborne = 0;
    for (int i = 0; i < JumpSteps && (gun->Flags & MdkObject::FlagStandable) != 0; i++)
    {
        step(1);
        input.Hold(Key::Jump, Input::State::Up);
        airborne += kurt->OnFloor ? 0 : 1;
        CHECK_NEAR(gun->Position.X, kurt->Feet.X, Tolerance);
        CHECK_NEAR(gun->Position.Y, kurt->Feet.Y, Tolerance);
    }

    CHECK(airborne > 0);
}
