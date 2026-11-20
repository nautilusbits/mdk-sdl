#pragma once

#include <span>
#include <string>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Game::Mods
{
    class ModFiles;

    // A mod's mod.txt: name=value lines (# comments); all optional.
    // The name defaults to the folder's; a higher priority wins over lower ones (default 0).
    //   name=Clean walls
    //   author=Me
    //   version=1.0
    //   description=Cleaner walls for LEVEL3
    //   priority=10
    struct ModInfo
    {
        static constexpr const char* FileName = "mod.txt";

        std::string Folder;
        std::string Name;
        std::string Author;
        std::string Version;
        std::string Description;
        int Priority = 0;

        bool operator==(const ModInfo&) const = default;

        // The mod of a folder or zip (its mod.txt, if any).
        static ModInfo Load(ModFiles& files);

        static ModInfo Parse(const std::string& folder, const std::string& text);

        // The mod.txt text (a generated mod's).
        std::string Format() const;

    private:
        static constexpr char Comment = '#';

        // Text as File.ReadAllText(string) reads it (UTF-8, a BOM's encoding).
        static std::string Text(std::span<const byte> bytes);
    };
}
