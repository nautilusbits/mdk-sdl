#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mdk.Game/Flow/Screen.h"

namespace Mdk::Formats
{
    class Bni;
}

namespace Mdk::Game::Menu
{
    class Ui;
    class VideoPlayer;

    // The end of the game (game state 8, 0x47727c; end_movie.gd): MISC/FLIC/MDKEND.FLC
    // with its sounds from MISC/FINISH.BNI and a white flash (0x477604), then
    // MISC/FLIC/MDKBZK.MVE (0x477870) and the main menu, after the splash. Esc gives up.
    //   frame 1 DOGSHIP (loop) ... 129 DROP, 133 FLYBY, 186 EXPLODE1, 188 DOGSHIP stops, 194 ENDEXP,
    //   196 EXPLODE1, 210-232 whiter, 233 white for 1 s, 233-260 back
    class EndMovie : public Flow::IScreen
    {
    public:
        explicit EndMovie(Ui& ui);

        Flow::Event Frame(float elapsed, const std::optional<std::string>& screenshot) override;

        std::vector<std::string> Status() const override;

        // How much whiter a frame is (0-1).
        static float Flash(int frame);

        void Dispose() override;

    private:
        static constexpr const char* Video = "MISC/FLIC/MDKEND.FLC";
        static constexpr const char* Movie = "MISC/FLIC/MDKBZK.MVE";
        static constexpr const char* Sounds = "MISC/FINISH.BNI";
        static constexpr const char* Looped = "DOGSHIP";
        static constexpr int LoopEnd = 188;
        static constexpr int FlashStart = 210;
        static constexpr int FlashPeak = 233;
        static constexpr int FlashEnd = 260;
        static constexpr float FlashHold = 1.f;
        static constexpr float MaxColour = 255.f;

        // Sounds by frame (the frame counter before decoding a frame).
        static const std::unordered_map<int, std::string>& FrameSounds();

        Ui& _ui;
        std::shared_ptr<VideoPlayer> _video;
        std::shared_ptr<Formats::Bni> _sounds;
        std::unordered_map<std::string, int> _voices;
        Flow::Event _next = Flow::Event::None;

        // A frame was shown: the extras of the next one (0x477604).
        void OnFrame(int frame);

        // After the FLC: the MVE, then the menu.
        void PlayMovie();

        void Play(const std::string& name);
    };
}
