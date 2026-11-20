#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Game/Menu/Ui.h"

namespace Mdk::Formats
{
    class Flc;
    class Mve;
}

namespace Mdk::Game::Menu
{
    // Plays an FLC on the 600 x 360 view (video.gd): one frame per game frame of the original
    // (34 ms; the file's own speed is ignored), the palette per frame; the last frame stays. Still
    // images use the same view. An MVE plays at its own rate with its sound, centred on the 640 x 480
    // screen.
    //   FLC ──34 ms──► frame ──► image (indices + palette, brightened) ──► view
    //   MVE ──frame time, or sooner when the sound runs low──► frame + sound ──► stream voice
    class VideoPlayer
    {
    public:
        // The image on the view (indices and palette), for the slideshow's return.
        struct StillImage
        {
            int Width = 0;
            int Height = 0;
            std::vector<byte> Indices;
            std::vector<byte> Palette;
        };

        explicit VideoPlayer(Ui& ui) : _ui(ui) {}
        ~VideoPlayer() { Stop(); }

        VideoPlayer(const VideoPlayer&) = delete;
        VideoPlayer& operator=(const VideoPlayer&) = delete;

        // A frame was shown: its number (from 1).
        std::function<void(int)> FrameShown;
        std::function<void()> Finished;

        bool Playing() const { return _flc != nullptr || _mve != nullptr; }

        // Read-only outside (C#: private set).
        const std::optional<StillImage>& Still() const { return _still; }

        // Starts an FLC (relative to the game's data). False when it can't be read.
        bool Play(const std::string& path);

        // Starts an MVE with its sound. False when it can't be read.
        bool PlayMovie(const std::string& path);

        // Stays on the current frame for `seconds`.
        void Hold(float seconds) { _hold = seconds; }

        // Raises every colour by `amount` (0-1, the end movie's flash).
        void Brighten(float amount);

        // Shows a still image of palette indices with its palette (768 RGB bytes).
        void ShowStill(int width, int height, std::span<const byte> indices, std::span<const byte> palette);

        void Stop();

        void Update(float delta);

        // The image fitted in its view (the 600 x 360 screen, or the movie's), centred.
        void Draw();

    private:
        // The original's game frame is at least 34 ms.
        static constexpr float FrameSeconds = 0.034f;
        // MVEs are centred on the whole screen.
        static constexpr Vector2 MovieScreen{640.f, 480.f};
        // The movie's sound is a bit shorter than its frames: with less queued, the next frame
        // comes at once.
        static constexpr float SoundLow = 0.25f;
        static constexpr float MicrosecondsPerSecond = 1e6f;
        static constexpr int PaletteBytes = 768;
        static constexpr int MaxColour = 255;

        Ui& _ui;
        std::shared_ptr<Formats::Flc> _flc;
        std::shared_ptr<Formats::Mve> _mve;
        std::shared_ptr<PalettedImage> _image;
        Vector2 _view = ScreenView::Size;
        float _time = 0;
        float _hold = 0;
        float _brighten = 0;
        int _soundVoice = 0;
        std::vector<byte> _palette = std::vector<byte>(PaletteBytes);
        std::optional<StillImage> _still;

        void ShowNext();

        void UpdateMovie(float delta);

        bool NextMovieFrame();

        void PushSound();

        std::vector<byte> Brightened(std::span<const byte> palette) const;

        void Finish();
    };
}
