#include "game/menu/IntroSplash.h"

#include <algorithm>
#include <stdexcept>

#include "engine/platform/Input.h"
#include "formats/Bni.h"
#include "game/menu/Ui.h"

namespace Mdk::Game::Menu
{
    IntroSplash::IntroSplash(Ui& ui, const Formats::Bni& options) : _ui(ui)
    {
        if (!options.Has(Entry))
        {
            _done = true;
            return;
        }

        // Some copies' entry is too short for the two palettes (#22): no splash, as without one.
        auto bytes = Ui::BytesOf(options, Entry);
        if (bytes.size() < PaletteSize * 2)
        {
            _done = true;
            return;
        }

        _palettes = {std::vector<byte>(PaletteSize), std::vector<byte>(bytes.begin(), bytes.begin() + PaletteSize),
            std::vector<byte>(bytes.begin() + PaletteSize, bytes.begin() + PaletteSize * 2)};
        _image = std::make_shared<PalettedImage>(ui.Renderer(), Width, Height, Unpack(bytes, PaletteSize * 2, Width * Height),
            _palettes[0]);
    }

    void IntroSplash::Update(Engine::Platform::Input& input, float delta)
    {
        if (_done)
        {
            return;
        }

        if (input.AnyPressed())
        {
            _done = true;
            return;
        }

        _time += delta;
        if (_time >= Steps[_step].Seconds)
        {
            _time = 0.f;
            _step++;
            _done = _step >= static_cast<int>(Steps.size());
        }
    }

    void IntroSplash::Draw()
    {
        if (_done || _image == nullptr)
        {
            return;
        }

        const auto& [seconds, from, to] = Steps[_step];
        _image->SetPalette(Blend(_palettes[from], _palettes[to], Math::Clamp(_time / seconds, 0.f, 1.f)));
        _image->Draw(*_ui.View, Vector2::Zero());
    }

    std::vector<byte> IntroSplash::Blend(std::span<const byte> from, std::span<const byte> to, float t)
    {
        auto weight = ToInt(MathF::Round(t * FullWeight));
        std::vector<byte> palette(from.size());
        for (size_t i = 0; i < palette.size(); i++)
        {
            palette[i] = static_cast<byte>((from[i] * (FullWeight - weight) + to[i] * weight) >> WeightShift);
        }

        return palette;
    }

    std::vector<byte> IntroSplash::Unpack(std::span<const byte> bytes, int p, int size)
    {
        std::vector<byte> output(static_cast<size_t>(size));
        auto n = 0;
        while (p < static_cast<int>(bytes.size()) && n < size)
        {
            int count = bytes[p++];
            if (count == 0)
            {
                break;
            }

            if (count > CopyLimit)
            {
                auto length = Math::Min(ByteRange - count, size - n);
                // C#'s span slice throws past the end.
                if (p + length > static_cast<int>(bytes.size()))
                {
                    throw std::out_of_range("IntroSplash.Unpack");
                }

                std::copy_n(bytes.begin() + p, length, output.begin() + n);
                p += ByteRange - count;
                n += length;
                continue;
            }

            if (p >= static_cast<int>(bytes.size()))
            {
                throw std::out_of_range("IntroSplash.Unpack");
            }

            std::fill_n(output.begin() + n, Math::Min(count, size - n), bytes[p++]);
            n += count;
        }

        return output;
    }
}
