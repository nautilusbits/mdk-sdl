#include <doctest.h>

#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Formats/Mto.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Shared/GameData.h"

#include <memory>
#include <string>

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using Mdk::Tests::GameData;

// Triangles flagged 0x2 (material NONE) are made not drawn and not solid when the original
// activates an arena (0x40d46c(a,1)). LEVEL8 GUNT_2's one cut the floor diagonally: an
// invisible wall Kurt couldn't pass.
namespace
{
    constexpr int Level = 8;
    const std::string Arena = "GUNT_2";
    constexpr float Step = 1.f / 60.f;
    constexpr int Frames = 5;
    constexpr float Seconds = 1.f;
    // A second of walking covers more than this.
    constexpr float MinWalk = 5.f;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }
}

DATA_TEST_CASE("HiddenTriangleTests.KurtWalksThroughHiddenWall")
{
    auto number = std::to_string(Level);
    auto mto = Formats::Mto::Load(GameData::Data()->PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + "O.MTO"));
    auto arena = mto->GetArena(Arena);
    TriangleGroups().Add(arena);
    ArenaSpace space;
    space.Add(arena);
    Vector3 start(-258.96f, 488.37f, 7.51f);
    SoundMixer mixer(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
    auto kurt = std::make_shared<Mdk::Game::Kurt::Kurt>(space, mixer, [](auto) { return Frames; });
    kurt->Feet = start;
    kurt->Yaw = 166.f;
    Input input;
    input.Hold(Key::Forward, Input::State::Down);
    for (float t = 0.f; t < Seconds; t += Step)
    {
        kurt->Update(input, Step);
    }

    CHECK_MESSAGE(Vector2::Distance(Vector2(start.X, start.Y), Vector2(kurt->Feet.X, kurt->Feet.Y)) > MinWalk,
        "stuck at ", kurt->Feet.X, " ", kurt->Feet.Y, " ", kurt->Feet.Z);
}
