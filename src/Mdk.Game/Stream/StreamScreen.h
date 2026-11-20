#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Game/Flow/Screen.h"
#include "Mdk.Game/Stream/Flight.h"
#include "Mdk.Game/Stream/StreamView.h"
#include "Mdk.Game/Stream/WatcomRandom.h"

namespace Mdk::Game::Menu
{
    class Ui;
}

namespace Mdk::Game::Stream
{
    // The stream after a level (game state 5: init 0x433b50, each frame 0x4352ac, cleanup
    // 0x435210; stream.gd). Kurt flies down the generated tube (Flight), drawn by
    // StreamView; the walls hurt. After about 30 seconds (or at 1 health) Bones picks him
    // up; after LEVEL8 (the Gunter variant) Kurt follows Gunter carrying Bones to a planet and can die.
    //   level ─► stream ─┬─ normal: StreamEnded ─► statistics
    //                    ├─ Gunter: StreamEnded ─► save prompt ─► LEVEL5
    //                    └─ died (Gunter tube): main menu
    class StreamScreen : public Flow::IScreen
    {
    public:
        StreamScreen(Menu::Ui& ui, int level, int health, std::shared_ptr<WatcomRandom> random);

        // Kurt's health in the stream (LEVEL5 starts with it).
        int Health() const { return _flight->Health; }

        Flow::Event Frame(float elapsed, const std::optional<std::string>& screenshot) override;

        // The flight's segment, time, Kurt's speed and health.
        std::vector<std::string> Status() const override;

        void Dispose() override;

    private:
        static constexpr const char* Archive = "STREAM/STREAM.BNI";
        static constexpr const char* Textures = "STREAM/STREAM.MTI";
        static constexpr const char* SystemPalette = "SYS_PAL";
        static constexpr const char* StreamPalette = "PAL";
        static constexpr int RgbSize = 3;
        // The last level index with statistics after the stream; the next one is Gunter's.
        static constexpr int LastStatisticsIndex = 3;
        static constexpr const char* KurtAnimation = "KURTANIM";
        static constexpr const char* RescueAnimation = "BONESANIM";
        static constexpr const char* HitSound = "HITSIDE";
        static constexpr const char* WindSound = "WIND";
        // Sounds playing at once at most (the stream's players).
        static constexpr int Players = 8;

        Menu::Ui& _ui;
        int _level;
        std::shared_ptr<Formats::Bni> _bni;
        std::unique_ptr<StreamView> _view;
        std::unique_ptr<Flight> _flight;
        std::string _otherModel;
        std::string _otherAnimation;
        std::unordered_map<std::string, std::shared_ptr<Engine::Audio::Sound>> _sounds;
        std::vector<int> _voices;
        int _hitVoice = 0;
        int _wallHits = 0;

        // Colours 0-63 are the system palette, 64-255 the stream's PAL.
        std::shared_ptr<Formats::Palette> LoadPalette();

        Clip ClipOf(const std::string& animation);

        // The turn keys or strafe keys steer left and right, forward and back up and down.
        static Steering SteeringOf(Engine::Platform::Input& input);

        // The flight's sounds; a wall hit's thud doesn't start again while it plays.
        void PlaySounds();

        std::shared_ptr<Engine::Audio::Sound> SoundOf(const std::string& name);

        // Prints the stream's moments (tests compare them).
        void Report();
    };
}
