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

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

// Found by playtest: LEVEL8's GUNT_5 missile aliens XT (set_height_offset 8, a model centred on
// its origin) sank halfway into the floor after their move_to down to the floor. The original
// sweeps an object's vertical move with its box lowered to z - height offset (0x45e810), so it
// stops with its origin that high above the floor.
namespace
{
    constexpr int Level = 8;
    const std::string Arena = "GUNT_5";
    const std::string Alien = "XT";
    constexpr float Tick = 1.f / 30.f;
    constexpr float Tolerance = 0.5f;
    constexpr float RayReach = 10.f;
    constexpr int FallTicks = 90;
    constexpr int MoveTicks = 25 * 30;

    // Inside if_kurt_in_rect (-260, 1636)-(-252, 1773): spawns the XT pair.
    const Vector3 Trigger(-256.f, 1700.f, -226.f);
    // On GUNT_5's lower floor, where the first XT sees Kurt and moves to (-220, 1640, -226).
    const Vector3 InSight(-240.f, 1580.f, -226.f);
    // Above the lower floor (z -226).
    const Vector3 AboveFloor(-220.f, 1640.f, -200.f);

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime(const std::shared_ptr<ArenaSpace>& space)
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, Level);
        auto number = std::to_string(Level);
        auto cmi = Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
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

    // The model's lowest point and the floor under the object.
    std::pair<float, float> Footing(ArenaSpace& space, MdkObject& obj)
    {
        auto lowest = std::numeric_limits<float>::infinity();
        auto transform = obj.Transform();
        for (auto& part : obj.PoseParts())
        {
            for (auto v : part)
            {
                lowest = MathF::Min(lowest, Vector3::Transform(v, transform).Z);
            }
        }

        Vector3 up(0.f, 0.f, RayReach);
        Vector3 floor;
        CHECK(space.Floor(obj.Position + up, obj.Position - up, floor));
        return {lowest, floor.Z};
    }
}

DATA_TEST_CASE("HeightOffsetTests.FallStopsAtTheHeightOffset")
{
    auto space = std::make_shared<ArenaSpace>();
    auto runtime = CreateRuntime(space);
    auto obj = runtime->Spawn(*runtime->GetArenaState(Arena)->Controller, Alien, AboveFloor, 0.f, -1, 0, ScriptRuntime::Spawning::Plain);
    REQUIRE(obj != nullptr);
    obj->HeightOffset = 8.f;
    obj->Flags = MdkObject::FlagGravity | MdkObject::FlagCollides;
    for (int i = 0; i < FallTicks; i++)
    {
        runtime->Motion->Update(*obj);
    }

    auto [lowest, floor] = Footing(*space, *obj);
    CHECK(obj->Position.Z >= floor + obj->HeightOffset - Tolerance);
    CHECK(obj->Position.Z <= floor + obj->HeightOffset + Tolerance);
}

DATA_TEST_CASE("HeightOffsetTests.MovedAlienStandsOnTheFloor")
{
    auto space = std::make_shared<ArenaSpace>();
    auto runtime = CreateRuntime(space);
    runtime->TeleportKurt(Arena, Trigger, 0.f);
    runtime->Update(Tick);
    runtime->TeleportKurt(Arena, InSight, 0.f);
    std::vector<std::shared_ptr<MdkObject>> aliens;
    for (auto& o : runtime->Objects)
    {
        if (o->TypeName == Alien)
        {
            aliens.push_back(o);
        }
    }

    REQUIRE_FALSE(aliens.empty());
    for (int i = 0; i < MoveTicks; i++)
    {
        runtime->Update(Tick);
    }

    for (auto& alien : aliens)
    {
        auto [lowest, floor] = Footing(*space, *alien);
        CHECK(lowest >= floor - Tolerance);
        CHECK(lowest <= floor + Tolerance);
    }
}
