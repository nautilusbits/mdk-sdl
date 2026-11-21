#pragma once

#include <string>

namespace Mdk::Game::Flow
{
    // Where settings, saves and mods live: next to the executable (a portable install), or, when
    // that folder can't be written (Program Files, a protected Steam or GOG folder), the user's own
    // data folder, e.g. %APPDATA%\mdk-sdl or ~/.local/share/mdk-sdl.
    struct UserFolders
    {
        // The user's data folder of this program (not created).
        static std::string Personal();

        // `beside` when a file can be written there, else `personal` (created).
        static std::string Choose(const std::string& beside, const std::string& personal);

    private:
        static constexpr const char* Name = "mdk-sdl";
        static constexpr const char* Probe = ".write-test";

        // Writes and deletes a probe file: folder rights, read-only media and the like all count.
        static bool IsWritable(const std::string& folder);
    };
}
