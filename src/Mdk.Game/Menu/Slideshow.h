#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "Mdk.Game/Menu/VideoPlayer.h"

namespace Mdk::Formats
{
    class Gif;
}

namespace Mdk::Game::Menu
{
    class Ui;

    // The main menu's slideshow (0x4279a0): once MDK12.FLC ends, an idle menu shows
    // MISC/MDKS_001.GIF after 5 s (without the items) for 4 s, then the others 2 s each with the
    // items, then the video's last frame again, and so on. Any input starts the wait again; on the first
    // image it shows the next one at once.
    //   last frame ──5 s──► MDKS_001 (no items) ──4 s──► MDKS_002 ──2 s──► ... ──2 s──► last frame
    class Slideshow
    {
    public:
        Slideshow(Ui& ui, VideoPlayer& video) : _ui(ui), _video(video) {}

        // The image shown: 0 the video's last frame, 1... the GIFs (C#: private set).
        int Image() const { return _image; }

        // Whether the menu items show over the image.
        bool ItemsShown() const { return _image != Bare; }

        // Starts once the video has ended, keeping its last frame.
        void Start();

        void Update(float delta);

        // Menu input: the wait starts again, and the first image gives way at once.
        void Reset();

    private:
        // "MISC/MDKS_{0:000}.GIF".
        static constexpr const char* ImagePath = "MISC/MDKS_{:03}.GIF";
        static constexpr float FirstDelay = 5.f;
        static constexpr float FirstTime = 4.f;
        static constexpr float NextTime = 2.f;
        // The image without the menu items.
        static constexpr int Bare = 1;

        Ui& _ui;
        VideoPlayer& _video;
        std::vector<std::shared_ptr<Formats::Gif>> _images;
        std::optional<VideoPlayer::StillImage> _last;
        float _time = 0;
        bool _running = false;
        int _image = 0;

        float Duration() const;

        void Next();

        void Load();
    };
}
