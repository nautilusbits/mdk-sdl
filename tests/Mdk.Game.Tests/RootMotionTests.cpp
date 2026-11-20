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
#include "Mdk.Game/Scripts/ObjectMotion.h"
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

// Found by playtest: LEVEL4's MEAT_5 never dropped its key. An alien XC ending its charge
// (XC_STOP) sank through the ice and lived on below it; the arena brings the tank, which carries
// the key, only once every XC is dead. The original adds an animation's root motion to the push
// (anim_step_frames 0x43ab70), moved with collisions on the next tick, not to the position.
namespace
{
    constexpr int Level = 4;
    constexpr const char* Arena = "MEAT_5";
    constexpr const char* Alien = "XC";
    constexpr const char* ChargeEnd = "XC_STOP";
    // Where the charge ended, on the ice (z −1989).
    const Vector3 OnTheIce{-4.3f, 13181.5f, -1989.05f};
    constexpr float Yaw = 315.0f;
    constexpr float Tolerance = 0.5f;
    constexpr int Ticks = 120;

    // Found by playtest: LEVEL5 MUSE_4's Gunter (XGUNTAM, flag 0x80000000) sank into his pillar
    // a little with each XGU_BLDM loop, out of reach. Flag 0x80000000 (obj+0x14b bit 7) skips
    // root motion; his death script clears it so that his fall moves him.
    constexpr int GunterLevel = 5;
    constexpr const char* GunterArena = "MUSE_4";
    constexpr const char* Gunter = "XGUNTAM";
    constexpr const char* Taunt = "XGU_BLDM";
    const Vector3 OnThePillar{387.0f, 121.0f, -1563.0f};
    constexpr float GunterYaw = 90.0f;
    // A minute: about six taunts, each sank him about 1.5.
    constexpr int GunterTicks = 1800;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime(int levelNumber = Level)
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, levelNumber);
        auto number = std::to_string(levelNumber);
        auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto space = std::make_shared<ArenaSpace>();
        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }
}

DATA_TEST_CASE("RootMotionTests.RootMotionStopsAtTheFloor")
{
    auto runtime = CreateRuntime();
    auto obj = runtime->Spawn(*runtime->GetArenaState(Arena)->Controller, Alien, OnTheIce, Yaw, -1, 0, ScriptRuntime::Spawning::Plain);
    REQUIRE(obj != nullptr);
    obj->Flags = MdkObject::FlagGravity | MdkObject::FlagCollides;
    obj->RestartAnimation(runtime->FindArenaAnimation(Arena, ChargeEnd), MdkObject::Looping::Once);

    for (int i = 0; i < Ticks; i++)
    {
        runtime->Motion->Update(*obj);
    }

    CHECK(obj->Position.Z >= OnTheIce.Z - Tolerance);
    CHECK(obj->Position.Z <= OnTheIce.Z + Tolerance);
}

DATA_TEST_CASE("RootMotionTests.NoRootMotionKeepsGunterUp")
{
    auto runtime = CreateRuntime(GunterLevel);
    auto obj = runtime->Spawn(*runtime->GetArenaState(GunterArena)->Controller, Gunter, OnThePillar, GunterYaw, -1, 0,
        ScriptRuntime::Spawning::Plain);
    REQUIRE(obj != nullptr);
    obj->Flags = MdkObject::FlagNoRootMotion;
    auto taunt = runtime->FindArenaAnimation(GunterArena, Taunt);

    // His script restarts the taunt once it's done (anim_once, if_anim_done).
    for (int i = 0; i < GunterTicks; i++)
    {
        if (obj->IsAnimationDone())
        {
            obj->RestartAnimation(taunt, MdkObject::Looping::Once);
        }

        runtime->Motion->Update(*obj);
    }

    CHECK(obj->Position == OnThePillar);
}
