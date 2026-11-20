#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Formats/ModelAnimation.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/FrameBlend.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Formats::ModelAnimation;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Issue #16: frames between ticks drew the objects' animations (monsters, items, doors)
// at 30 Hz. A frame draws the pose between the last tick's and this tick's animation time; a
// restart or the end of a non-looping animation snaps.

namespace
{
    constexpr int Level = 4;
    constexpr const char* Arena = "MEAT_5";
    constexpr const char* Alien = "XC";
    constexpr const char* Charge = "XC_STOP";
    const Vector3 Somewhere(-4.3f, 13181.5f, -1989.05f);
    constexpr float Half = 0.5f;
    constexpr float Precision = 1e-4f;

    using Parts = MdkObject::Parts;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<SoundMixer> Mixer()
    {
        return std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
    }

    struct Spawn
    {
        std::shared_ptr<ScriptRuntime> Runtime;
        std::shared_ptr<MdkObject> Obj;
        std::shared_ptr<ModelAnimation> Animation;
    };

    Spawn Spawned()
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, Level);
        auto number = std::to_string(Level);
        auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto space = std::make_shared<ArenaSpace>();
        auto mixer = Mixer();
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        auto runtime = std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
        auto obj = runtime->Spawn(*runtime->GetArenaState(Arena)->Controller, Alien, Somewhere, 0.f, -1, 0, ScriptRuntime::Spawning::Plain);
        auto animation = runtime->FindArenaAnimation(Arena, Charge);
        REQUIRE(obj != nullptr);
        REQUIRE(animation != nullptr);
        return {runtime, obj, animation};
    }

    FrameBlend BlendAt(float tick)
    {
        ArenaSpace space;
        auto mixer = Mixer();
        KurtPlayer kurt(space, *mixer, [](auto) { return 1; });
        FrameBlend blend(Blending::On);
        blend.Update(kurt, 1.f, tick);
        return blend;
    }

    // A tick of the animation, from a remembered start.
    void Tick(MdkObject& obj)
    {
        obj.Remember();
        obj.AdvanceAnimation(ScriptRuntime::Tick);
    }

    // Some vertex the tick moved: (part, vertex).
    std::pair<int, int> Moved(const Parts& from, const Parts& to)
    {
        for (int p = 0; p < static_cast<int>(to.size()); p++)
        {
            for (int v = 0; v < static_cast<int>(to[p].size()); v++)
            {
                if (from[p][v] != to[p][v])
                {
                    return {p, v};
                }
            }
        }

        throw std::runtime_error("nothing moved");
    }

    void Near(Vector3 expected, Vector3 actual)
    {
        CHECK(Vector3::Distance(expected, actual) < Precision);
    }
}

DATA_TEST_CASE("AnimationBlendTests.PosesAreDrawnBetweenFrames")
{
    auto [runtime, obj, animation] = Spawned();
    obj->RestartAnimation(animation, MdkObject::Looping::Once);
    Parts from = obj->PoseParts();
    Tick(*obj);
    Parts to = obj->PoseParts();

    auto [p, v] = Moved(from, to);
    Near(Vector3::Lerp(from[p][v], to[p][v], Half), BlendAt(Half).PoseOf(*obj)[p][v]);
    Near(to[p][v], BlendAt(1.f).PoseOf(*obj)[p][v]);
}

DATA_TEST_CASE("AnimationBlendTests.ARestartIsDrawnAtOnce")
{
    auto [runtime, obj, animation] = Spawned();
    obj->RestartAnimation(animation, MdkObject::Looping::Once);
    Tick(*obj);
    Tick(*obj);
    obj->Remember();
    obj->RestartAnimation(animation, MdkObject::Looping::Once);

    auto blend = BlendAt(Half);
    Parts pose = blend.PoseOf(*obj);
    Parts now = obj->PoseParts();
    for (size_t part = 0; part < now.size(); part++)
    {
        CHECK(now[part] == pose[part]);
    }
}

DATA_TEST_CASE("AnimationBlendTests.ALoopGoesOnToItsFirstFrame")
{
    auto [runtime, obj, animation] = Spawned();
    obj->RestartAnimation(animation, MdkObject::Looping::Loop);
    while (obj->AnimationFrame != animation->FrameCount - 1)
    {
        Tick(*obj);
    }

    Parts last = obj->PoseParts();
    Tick(*obj);
    Parts first = obj->PoseParts();

    auto [p, v] = Moved(last, first);
    Near(Vector3::Lerp(last[p][v], first[p][v], Half), BlendAt(Half).PoseOf(*obj)[p][v]);
}

DATA_TEST_CASE("AnimationBlendTests.TestsDrawTheFramesAsTheyAre")
{
    auto [runtime, obj, animation] = Spawned();
    obj->RestartAnimation(animation, MdkObject::Looping::Once);
    Tick(*obj);
    ArenaSpace space;
    auto mixer = Mixer();
    KurtPlayer kurt(space, *mixer, [](auto) { return 1; });
    FrameBlend blend(Blending::Off);
    blend.Update(kurt, Half, Half);
    CHECK(&obj->PoseParts() == &blend.PoseOf(*obj));
}
