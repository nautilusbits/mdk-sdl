#pragma once

#include <string>

namespace Mdk::Formats
{
    // Finds data files whatever their case: the game asks for MISC/MDKFONT.FTI, an
    // installation may have MISC/mdkfont.fti, which a case-sensitive file system (Linux) misses.
    struct CaseInsensitivePath
    {
        // The path of `relative` under `dir`, each part named as on disk; parts not found keep their name.
        static std::string Resolve(const std::string& dir, const std::string& relative);

    private:
        // The entry of a folder named `name`: the exact name first, then any case.
        static std::string Match(const std::string& dir, const std::string& name);
    };
}
