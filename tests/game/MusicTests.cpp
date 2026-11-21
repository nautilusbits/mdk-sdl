#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "Asserts.h"
#include "core/Dictionary.h"
#include "engine/audio/AudioDevice.h"
#include "formats/Cmi.h"
#include "game/audio/LevelMusic.h"
#include "game/audio/SoundMixer.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game::Audio;
using Mdk::Tests::GameData;

// The arena music and its fades (level_audio.gd), on a muted device.
namespace
{
    constexpr int Rate = 44100;
    constexpr const char* None = "NONE";
    // The fades: 256 frames in, 128 out, at 34 ms a frame.
    constexpr float FadeInSeconds = 256 * 0.034f;
    constexpr float FadeOutSeconds = 128 * 0.034f;

    const Dictionary<std::string, std::string> Tracks = {
        {"HMO_1", "H1"},
        {"HMO_2", "H2"},
        {"CHMO_1", ""},
        {"HMO_3", None},
    };

    // Device, mixer and music, torn down in reverse (the C# disposes the device).
    struct Setup
    {
        std::unique_ptr<AudioDevice> Device;
        std::unique_ptr<SoundMixer> Mixer;
        std::unique_ptr<LevelMusic> Music;
    };

    // Every name but NONE is a looping sound.
    Setup Create()
    {
        Setup setup;
        setup.Device = std::make_unique<AudioDevice>(Output::Muted);
        SoundMixer::Entry entry{std::make_shared<Sound>(std::vector<float>(Rate), 1, Rate, Looping::Forever), 0};
        setup.Mixer = std::make_unique<SoundMixer>(*setup.Device, [entry](const std::string& name)
        {
            return name == None ? std::optional<SoundMixer::Entry>() : std::optional<SoundMixer::Entry>(entry);
        });
        setup.Music = std::make_unique<LevelMusic>(*setup.Mixer, Tracks);
        return setup;
    }

    void Run(LevelMusic& music, float seconds)
    {
        constexpr float Frame = 1.f / 60.f;
        for (float t = 0.f; t < seconds; t += Frame)
        {
            music.Update(Frame);
        }
    }
}

TEST_CASE("MusicTests.CorridorsPlayTheLevelsCorridorTrack")
{
    auto setup = Create();
    auto& music = *setup.Music;
    CHECK(music.TrackOf("HMO_1") == "H1");
    CHECK(music.TrackOf("CHMO_1") == LevelMusic::Corridor);
    CHECK(music.TrackOf("UNKNOWN") == LevelMusic::Corridor);
    CHECK(music.TrackOf("HMO_3") == None);
}

TEST_CASE("MusicTests.NewTrackFadesInFromSilence")
{
    auto setup = Create();
    auto& music = *setup.Music;
    music.Enter("HMO_1");
    CHECK(music.Playing() == "H1");
    CHECK(music.Volume() == 0.f);

    Run(music, FadeInSeconds / 2.f);
    CHECK_NEAR(SoundMixer::FullVolume / 2.f, music.Volume(), SoundMixer::FullVolume * 0.01f);
    Run(music, FadeInSeconds / 2.f + 0.1f);
    CHECK(music.Volume() == static_cast<float>(SoundMixer::FullVolume));
}

TEST_CASE("MusicTests.OldTrackFadesOutAndStops")
{
    auto setup = Create();
    auto& music = *setup.Music;
    music.Enter("HMO_1");
    Run(music, FadeInSeconds + 0.1f);
    music.Enter("CHMO_1");
    CHECK(music.Playing() == LevelMusic::Corridor);
    CHECK(music.Fading() == "H1");

    Run(music, FadeOutSeconds - 0.1f);
    CHECK(music.Fading() == "H1");
    Run(music, 0.2f);
    CHECK_FALSE(music.Fading().has_value());
}

TEST_CASE("MusicTests.GoingBackSwapsWithTheFadingTrack")
{
    auto setup = Create();
    auto& music = *setup.Music;
    music.Enter("HMO_1");
    Run(music, FadeInSeconds + 0.1f);
    music.Enter("CHMO_1");
    Run(music, 1.f);
    float left = music.FadingVolume();
    music.Enter("HMO_1");
    CHECK(music.Playing() == "H1");
    CHECK(music.Volume() == left);
    CHECK(music.Fading() == LevelMusic::Corridor);
}

TEST_CASE("MusicTests.NoneIsSilence")
{
    auto setup = Create();
    auto& music = *setup.Music;
    music.Enter("HMO_1");
    music.Enter("HMO_3");
    CHECK(music.Playing() == None);
    CHECK(music.Fading() == "H1");
    // Still in the same arena: nothing changes.
    music.Enter("HMO_3");
    CHECK(music.Fading() == "H1");
}

DATA_TEST_CASE("MusicTests.Level3CorridorsHaveNoTrack")
{
    auto data = GameData::Data();
    auto cmi = Formats::Cmi::Load(data->PathOf("TRAVERSE/LEVEL3/LEVEL3.CMI"));
    bool empty = false;
    for (const auto& [arena, track] : cmi->ArenaMusic)
    {
        empty = empty || track.empty();
    }

    CHECK(empty);
    CHECK(cmi->ArenaMusic["HMO_1"] == "H1");
}
