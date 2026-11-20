#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Formats/Mto.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Running down a steep slope (LEVEL6's OLYM_8, in-game level 2: 42°, the floor's normal z
// 0.74): Kurt's feet must follow it, not leave it every other step and flick his animation
// between the run and the fall.
namespace
{
    constexpr int Level = 6;
    constexpr float Step = 1.0f / 60.0f;
    constexpr int Frames = 5;
    constexpr float Seconds = 1.5f;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }
}

DATA_TEST_CASE("SteepSlopeTests.RunningDownhillNeverFalls")
{
    for (float yaw : {178.0f, 160.0f, -160.0f})
    {
        CAPTURE(yaw);
        auto& data = *GameData::Data();
        auto number = std::to_string(Level);
        auto mto = Formats::Mto::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + "O.MTO"));
        ArenaSpace space;
        space.Add(mto->GetArena("OLYM_8"));
        SoundMixer mixer(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        KurtPlayer kurt(space, mixer, [](auto) { return Frames; });
        kurt.Feet = Vector3(2307.0f, 2469.0f, -1164.0f);
        kurt.Yaw = yaw;

        Input input;
        for (float t = 0.0f; t < 0.5f; t += Step)
        {
            kurt.Update(input, Step);
        }

        // Run forward, counting the falls started.
        input.Hold(Key::Forward, Input::State::Down);
        int falls = 0;
        int airSteps = 0;
        auto previous = kurt.Current;
        for (float t = 0.0f; t < Seconds; t += Step)
        {
            kurt.Update(input, Step);
            airSteps += kurt.OnFloor ? 0 : 1;
            if (kurt.Current == KurtPlayer::State::Fall && previous != KurtPlayer::State::Fall)
            {
                falls++;
            }

            previous = kurt.Current;
        }

        CAPTURE(airSteps);
        CAPTURE(kurt.Feet.X);
        CAPTURE(kurt.Feet.Y);
        CAPTURE(kurt.Feet.Z);
        CHECK(falls == 0);
    }
}
