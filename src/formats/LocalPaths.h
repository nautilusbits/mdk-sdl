#pragma once

#include <string>

namespace Mdk::Formats
{
    // mdk_paths.cfg, a file of your own next to the program (the Godot port's), naming
    // the game's folder (mdk) and the 1996 demo's (beta), so no environment variable is
    // needed:
    //   [paths]
    //   mdk="C:/GOG Games/MDK"
    //   beta="C:/Games/MDK (1996-08-06) (beta demo)"
    struct LocalPaths
    {
        static constexpr const char* FileName = "mdk_paths.cfg";
        static constexpr const char* Game = "mdk";
        static constexpr const char* Beta = "beta";

        // The folder named by `key` in the file next to the program, or "".
        static std::string Get(const std::string& key);

        // The value of `key` in the [paths] section of a config text
        // (Godot's ConfigFile: quoted strings, ; comments), or "".
        static std::string Parse(const std::string& text, const std::string& key);

    private:
        static constexpr const char* Section = "paths";
        static constexpr char Quote = '"';

        // "C:/Games/MDK" → C:/Games/MDK; escaped quotes and backslashes kept as one.
        static std::string Unquote(const std::string& value);
    };
}
