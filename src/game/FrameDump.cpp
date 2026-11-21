#include "game/FrameDump.h"

#include <format>

#include "core/DotNet.h"
#include "core/Files.h"

namespace Mdk::Game
{
    FrameDump FrameDump::Parse(const std::string& text)
    {
        auto parts = Str::Split(text, ',');
        auto every = parts.size() > 2 ? Str::ParseInt(parts[2]) : DefaultEvery;
        return FrameDump{ parts[0], Str::ParseInt(parts[1]), every };
    }

    std::optional<std::string> FrameDump::PathOf(int step) const
    {
        if (step % Every != 0 || step / Every >= Count)
        {
            return std::nullopt;
        }

        return Path::Combine(Folder, std::format("frame{:03}.bmp", step / Every));
    }
}
