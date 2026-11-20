#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "Asserts.h"
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

// The mixer's laws, on a muted device (nothing reaches the speakers).
namespace
{
    constexpr int Rate = 44100;
    // -25 dB, the floor of the volume law.
    constexpr float FloorGain = 0.05623f;

    std::shared_ptr<Sound> Tone(float seconds, Looping looping)
    {
        return std::make_shared<Sound>(std::vector<float>(static_cast<int>(Rate * seconds)), 1, Rate, looping);
    }

    // A mixer whose every name plays entry.
    std::shared_ptr<SoundMixer> MixerOf(AudioDevice& device, SoundMixer::Entry entry)
    {
        return std::make_shared<SoundMixer>(device, [entry](const std::string&) { return std::optional<SoundMixer::Entry>(entry); });
    }
}

TEST_CASE("SoundTests.VolumeIsLinearInDecibelsAndNeverSilent")
{
    CHECK_PRECISION(1.0f, SoundMixer::Gain(SoundMixer::FullVolume), 4);
    CHECK_PRECISION(FloorGain, SoundMixer::Gain(0), 4);

    // Half volume: -12.5 dB.
    CHECK_PRECISION(std::pow(10.0f, -12.5f / 20.0f), SoundMixer::Gain(SoundMixer::FullVolume / 2.0f), 3);
}

TEST_CASE("SoundTests.OneShotEndsAndLoopKeepsPlaying")
{
    AudioDevice device(Output::Muted);
    auto once = device.Play(Tone(0.1f, Looping::Once), 1.0f);
    auto loop = device.Play(Tone(0.1f, Looping::Forever), 1.0f);
    device.Update(0.2f);
    CHECK_FALSE(device.IsPlaying(once));
    CHECK(device.IsPlaying(loop));
}

TEST_CASE("SoundTests.OncePlaysOnlyWhenSilent")
{
    AudioDevice device(Output::Muted);
    auto mixer = MixerOf(device, SoundMixer::Entry{Tone(1.0f, Looping::Once), SoundMixer::FullVolume});
    CHECK(mixer->Play("A") != 0);
    CHECK(mixer->Play("A", SoundMixer::Start::Once) == 0);
    CHECK(mixer->Play("A", SoundMixer::Start::New) != 0);
}

TEST_CASE("SoundTests.FarSoundsFallToTheFloor")
{
    AudioDevice device(Output::Muted);
    auto mixer = MixerOf(device, SoundMixer::Entry{Tone(1.0f, Looping::Forever), SoundMixer::FullVolume});
    mixer->ListenerPosition = Vector3::Zero();
    auto near = mixer->PlayAt("A", Vector3(10.0f, 0.0f, 0.0f));
    auto far = mixer->PlayAt("A", Vector3(300.0f, 0.0f, 0.0f));
    CHECK_PRECISION(1.0f, device.GainOf(near), 3);
    CHECK_PRECISION(FloorGain, device.GainOf(far), 3);
}

// A sound following an object (play_sound flag 0x10) stops when the object is removed
// (sound_mixer.gd: its player goes with the object).
DATA_TEST_CASE("SoundTests.FollowingSoundStopsWithItsObject")
{
    constexpr int Level = 3;
    constexpr int Following = 0x10;
    auto& data = *GameData::Data();
    AudioDevice device(Output::Muted);
    auto mixer = MixerOf(device, SoundMixer::Entry{Tone(1.0f, Looping::Forever), SoundMixer::FullVolume});
    auto space = std::make_shared<ArenaSpace>();
    auto number = std::to_string(Level);
    auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
    auto runtime = std::make_shared<ScriptRuntime>(std::make_shared<LevelData>(data, Level),
        Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI")),
        Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI")), space, std::make_shared<TriangleGroups>(), mixer, kurt);
    auto obj = std::make_shared<MdkObject>();
    runtime->PlaySound(*obj, "A", Following, Formats::Scripts::Operand{});
    CHECK(mixer->IsPlaying("A"));

    runtime->Remove(*obj);

    CHECK_FALSE(mixer->IsPlaying("A"));
}
