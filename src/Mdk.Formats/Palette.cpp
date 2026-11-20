#include "Mdk.Formats/Palette.h"

namespace Mdk::Formats
{
    std::shared_ptr<Palette> Palette::FromRgb(std::span<const byte> rgb)
    {
        auto palette = std::make_shared<Palette>();
        for (auto i = 0; i < Math::Min(Size, static_cast<int>(rgb.size()) / 3); i++)
        {
            palette->Rgba[i * 4] = rgb[i * 3];
            palette->Rgba[i * 4 + 1] = rgb[i * 3 + 1];
            palette->Rgba[i * 4 + 2] = rgb[i * 3 + 2];
            palette->Rgba[i * 4 + 3] = 255;
        }

        // The game forces index 0 to black; sprites treat it as transparent.
        palette->Rgba[0] = palette->Rgba[1] = palette->Rgba[2] = 0;
        return palette;
    }

    std::shared_ptr<Palette> Palette::WithArenaColors(std::span<const byte> arenaRgb) const
    {
        auto palette = std::make_shared<Palette>();
        palette->Rgba = Rgba;
        for (auto i = 0; i < static_cast<int>(arenaRgb.size()) / 3; i++)
        {
            auto index = ArenaFirstIndex + i;
            palette->Rgba[index * 4] = arenaRgb[i * 3];
            palette->Rgba[index * 4 + 1] = arenaRgb[i * 3 + 1];
            palette->Rgba[index * 4 + 2] = arenaRgb[i * 3 + 2];
        }

        return palette;
    }

    std::shared_ptr<Palette> Palette::WithSystemColors(std::span<const byte> systemRgb) const
    {
        auto palette = std::make_shared<Palette>();
        palette->Rgba = Rgba;
        auto count = Math::Min(ArenaFirstIndex, static_cast<int>(systemRgb.size()) / 3);
        for (auto i = 1; i < count; i++)
        {
            palette->Rgba[i * 4] = systemRgb[i * 3];
            palette->Rgba[i * 4 + 1] = systemRgb[i * 3 + 1];
            palette->Rgba[i * 4 + 2] = systemRgb[i * 3 + 2];
        }

        return palette;
    }
}
