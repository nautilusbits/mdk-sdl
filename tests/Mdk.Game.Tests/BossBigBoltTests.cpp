#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// LEVEL7's boss (DANT_10, XB1) fires BIGBOLTs from the GUN part in his hands (fire, opcode 61):
// they start where the animation holds the gun, not at the rest pose's gun by his feet (which put
// them in the floor, dead at once), and cross the arena to Kurt.

namespace
{
    constexpr float Tick = 1.f / 30.f;
    // The far side of DANT_10, where Kurt first sees the tower (about 650 away).
    const Vector3 FarSide(545.89f, 5023.39f, 184.54f);
    // XB1's script: the firing animation, then fire [1, "GUN"], "BIGBOLT".
    constexpr int Firing = 0x20baa;

    std::shared_ptr<MdkObject> FirstOf(const ScriptRuntime& runtime, const std::string& type, bool alive)
    {
        for (const auto& o : runtime.Objects)
        {
            if (o->TypeName == type && (!alive || !o->Dead))
            {
                return o;
            }
        }

        return nullptr;
    }
}

DATA_TEST_CASE("BossBigBoltTests.BigBoltsLeaveTheGunAndReachKurt")
{
    auto& data = *GameData::Data();
    auto level = std::make_shared<LevelData>(data, 7);
    auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL7/LEVEL7.CMI"));
    auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
    auto space = std::make_shared<ArenaSpace>();
    for (const auto& arena : level->Arenas)
    {
        if (level->IsReachable(arena->Name))
        {
            space->Add(arena);
        }
    }

    AudioDevice device(Output::Muted);
    auto mixer = std::make_shared<SoundMixer>(device, [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
    auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
    auto runtime = std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    runtime->TeleportKurt("DANT_10", FarSide, 0.f);
    for (int i = 0; i < 30; i++)
    {
        runtime->Update(Tick);
    }

    auto boss = FirstOf(*runtime, "XB1", true);
    REQUIRE(boss != nullptr);
    boss->Restart = Firing;
    std::shared_ptr<MdkObject> bolt;
    for (int i = 0; i < 30 && bolt == nullptr; i++)
    {
        runtime->Update(Tick);
        bolt = FirstOf(*runtime, "BIGBOLT", false);
    }

    REQUIRE(bolt != nullptr);

    // Above the floor he stands on, in his hands.
    CAPTURE(bolt->Position.Z);
    CAPTURE(boss->Position.Z);
    CHECK(bolt->Position.Z > boss->Position.Z + 3.f);

    // It flies across (125 a second) and ends next to Kurt, not by the tower.
    auto health = runtime->Kurt->Health;
    for (int i = 0; i < 8 * 30 && !bolt->Dead && bolt->Health > 0; i++)
    {
        runtime->Update(Tick);
    }

    CHECK(Vector3::Distance(bolt->Position, runtime->KurtPosition) < 20.f);
    CHECK((runtime->Kurt->Health < health || bolt->Health == 0));
}
