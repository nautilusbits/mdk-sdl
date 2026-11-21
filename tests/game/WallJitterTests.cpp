#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "engine/platform/Input.h"
#include "formats/Dti.h"
#include "formats/Mto.h"
#include "game/Angles.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/FollowCamera.h"
#include "game/kurt/Kurt.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Running down level 3's ramp and into its canyon walls must not shake the view: Kurt
// loses the floor for single steps going downhill, but the camera only tilts once he really falls
// (air ticks 0x573a48 count from vz < -16).
namespace
{
    constexpr int Level = 3;
    constexpr float Step = 1.f / 60.f;
    constexpr int Frames = 5;
    constexpr float Seconds = 4.f;
    constexpr int MaxFlips = 4;
    // Pitch changes (degrees per step) below this don't count as reversals; no step may
    // turn the view more than MaxPitchStep (the climb term comes back by 0.8° a tick
    // when Kurt stops).
    constexpr float PitchTolerance = 0.05f;
    constexpr float MaxPitchStep = 1.f;
    // The slope's bumps turn the view a few times; the old air tilt did it 80-160 times.
    constexpr int MaxTiltFlips = 10;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    // How often a series of changes flips sign (changes below the tolerance ignored).
    int Reversals(const std::vector<float>& changes)
    {
        int flips = 0;
        float previous = 0.f;
        for (float d : changes)
        {
            if (!(MathF::Abs(d) > PitchTolerance))
            {
                continue;
            }

            if (previous != 0.f && MathF::Sign(d) != MathF::Sign(previous))
            {
                flips++;
            }

            previous = d;
        }

        return flips;
    }
}

DATA_TEST_CASE("WallJitterTests.RunningDownhillKeepsViewSteady")
{
    for (float yaw : {30.f, 60.f, 120.f, 150.f})
    {
        CAPTURE(yaw);
        auto& data = *GameData::Data();
        auto number = std::to_string(Level);
        auto dti = Formats::Dti::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".DTI"));
        auto mto = Formats::Mto::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + "O.MTO"));
        ArenaSpace space;
        space.Add(mto->GetArena(dti->Arenas[dti->StartArena].Name));
        SoundMixer mixer(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        KurtPlayer kurt(space, mixer, [](auto) { return Frames; });
        kurt.Feet = Vector3(-10.f, 60.f, 146.f);
        kurt.Yaw = yaw;
        Input input;
        for (float t = 0.f; t < 1.f; t += Step)
        {
            kurt.Update(input, Step);
        }

        input.Hold(Key::Forward, Input::State::Down);
        std::vector<float> heights;
        Mdk::Game::Kurt::FollowCamera camera;
        std::vector<float> eyes;
        for (float t = 0.f; t < Seconds; t += Step)
        {
            kurt.Update(input, Step);
            camera.Update(kurt, 4.f, input, Step);
            heights.push_back(kurt.Feet.Z);
            eyes.push_back(camera.Forward.Z);
        }

        // Up-down reversals: a steady walk goes one way or stays; shaking flips every few steps.
        int flips = 0;
        float previous = 0.f;
        for (size_t i = 1; i < heights.size(); i++)
        {
            float d = heights[i] - heights[i - 1];
            if (MathF::Abs(d) < 1e-3f)
            {
                continue;
            }

            if (previous != 0.f && MathF::Sign(d) != MathF::Sign(previous))
            {
                flips++;
            }

            previous = d;
        }

        INFO("height reversed ", flips, " times");
        CHECK(flips < MaxFlips);

        // The view's pitch may follow the descent smoothly (the climb term, −40 × the smoothed
        // vertical speed) but doesn't shake: no step turns it by more than a degree, and its
        // noticeable changes rarely reverse (the old air tilt toggled every few steps).
        std::vector<float> pitches;
        for (float z : eyes)
        {
            pitches.push_back(Game::RadiansToDegrees(MathF::Asin(z)));
        }

        std::vector<float> changes;
        float jump = 0.f;
        for (size_t i = 1; i < pitches.size(); i++)
        {
            float change = pitches[i] - pitches[i - 1];
            changes.push_back(change);
            jump = i == 1 ? MathF::Abs(change) : MathF::Max(jump, MathF::Abs(change));
        }

        INFO("view pitch jumped ", jump, "°");
        CHECK(jump < MaxPitchStep);
        int tilts = Reversals(changes);
        INFO("view pitch reversed ", tilts, " times");
        CHECK(tilts < MaxTiltFlips);
    }
}
