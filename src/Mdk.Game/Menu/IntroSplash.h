#pragma once

#include <array>
#include <memory>
#include <span>
#include <vector>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Engine::Platform
{
    class Input;
}

namespace Mdk::Formats
{
    class Bni;
}

namespace Mdk::Game::Menu
{
    class PalettedImage;
    class Ui;

    // The INTRO1A splash (0x426edc) before the main menu: at start, after Kurt dies and
    // after the end movies. A 600 x 360 still (MISC/OPTIONS.BNI: two 768-byte palettes, then
    // run-length rows, 0x426e04) whose palette fades; any key skips it.
    //   black ──1 s──► palette 1 ──3 s──► ──2 s──► palette 2 ──3 s──► ──1 s──► black
    class IntroSplash
    {
    public:
        IntroSplash(Ui& ui, const Formats::Bni& options);

        // Read-only outside (C#: private set).
        bool Done() const { return _done; }

        void Update(Engine::Platform::Input& input, float delta);

        void Draw();

        // The palette `t` of the way between two (0x417040, 0x4170ec).
        static std::vector<byte> Blend(std::span<const byte> from, std::span<const byte> to, float t);

        // Runs: a count above 127 copies 256 - count bytes, a smaller one repeats the next byte,
        // 0 ends.
        static std::vector<byte> Unpack(std::span<const byte> bytes, int p, int size);

    private:
        // Each step: seconds, palette at its start, palette at its end (0 black, 1, 2).
        struct Step
        {
            float Seconds = 0;
            int From = 0;
            int To = 0;
        };

        static constexpr const char* Entry = "INTRO1A";
        static constexpr int Width = 600;
        static constexpr int Height = 360;
        static constexpr int PaletteSize = 768;
        static constexpr int FullWeight = 256;
        static constexpr int WeightShift = 8;
        static constexpr int CopyLimit = 127;
        static constexpr int ByteRange = 256;
        static constexpr std::array<Step, 5> Steps{{{1.f, 0, 1}, {3.f, 1, 1}, {2.f, 1, 2}, {3.f, 2, 2}, {1.f, 2, 0}}};

        Ui& _ui;
        std::vector<std::vector<byte>> _palettes;
        std::shared_ptr<PalettedImage> _image;
        int _step = 0;
        float _time = 0;
        bool _done = false;
    };
}
