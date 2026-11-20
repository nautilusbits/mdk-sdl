#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Game/Angles.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Mdk.Game/Scripts/SniperRounds.h"
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

// Level 8's start ship (GUNT_1 XBSHIP_0): indestructible (65000), but a sniper round on
// one of its 7 blue turrets T1-T7 blows it off; without them it falls and explodes. Rounds hit
// the parts' faces (0x414668), not their boxes: the hull's box holds the turrets.

namespace
{
    constexpr int Level = 8;
    constexpr float Tick = 1.f / 30.f;
    constexpr const char* Arena = "GUNT_1";
    constexpr const char* Ship = "XBSHIP";
    constexpr int BulletRound = 0;
    constexpr int Turrets = 0x7F;
    // On the floor, every turret in sight once the ship hovers at (62, 680, 207).
    const Vector3 Stand(0.f, 160.f, 10.f);
    constexpr float EyeHeight = 6.f;
    // The ship reaches its hovering spot after about 20 s.
    constexpr int ArriveTicks = 25 * 30;
    constexpr int ShootTicks = 30 * 30;
    constexpr int CrashTicks = 20 * 30;

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
        for (const auto& arena : level->Arenas)
        {
            if (level->IsReachable(arena->Name))
            {
                space->Add(arena);
            }
        }

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }

    // The centres of the turrets still on the ship, as drawn.
    std::vector<Vector3> TurretCentres(MdkObject& ship)
    {
        const auto& pose = ship.PoseParts();
        std::vector<Vector3> centres;
        for (int p = 0; p < static_cast<int>(pose.size()); p++)
        {
            if (!ScriptRuntime::IsWeakPart(ship, p) || pose[p].empty() || (ship.HiddenParts & (1 << p)) != 0)
            {
                continue;
            }

            auto sum = Vector3::Zero();
            for (const auto& v : pose[p])
            {
                sum = sum + v;
            }

            centres.push_back(Vector3::Transform(sum / static_cast<float>(pose[p].size()), ship.Transform()));
        }

        return centres;
    }
}

DATA_TEST_CASE("BigShipTests.SnipedTurretsBringTheShipDown")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Arena, Stand, 90.f);
    for (int i = 0; i < ArriveTicks; i++)
    {
        runtime->Update(Tick);
    }

    std::shared_ptr<MdkObject> found;
    for (const auto& o : runtime->Objects)
    {
        if (o->TypeName == Ship)
        {
            found = o;
            break;
        }
    }

    REQUIRE(found != nullptr);
    auto& ship = *found;
    auto eye = runtime->KurtPosition + Vector3(0.f, 0.f, EyeHeight);

    // Bullets at each remaining turret in turn.
    for (int i = 0; i < ShootTicks && (ship.HiddenParts & Turrets) != Turrets; i++)
    {
        auto targets = TurretCentres(ship);
        auto to = targets[i % targets.size()] - eye;
        auto yaw = Mdk::Game::RadiansToDegrees(MathF::Atan2(to.Y, to.X));
        auto pitch = -Mdk::Game::RadiansToDegrees(MathF::Atan2(to.Z, Vector2(to.X, to.Y).Length()));
        runtime->SniperRounds->Fire(BulletRound, eye, yaw, pitch, nullptr);
        runtime->Update(Tick);
    }

    CHECK(Turrets == (ship.HiddenParts & Turrets));

    for (int i = 0; i < CrashTicks && !ship.Dead && ship.Health > 0; i++)
    {
        runtime->Update(Tick);
    }

    CHECK((ship.Dead || ship.Health == 0));
}
