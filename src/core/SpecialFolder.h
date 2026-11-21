#pragma once

// Environment.GetFolderPath for the user's data folders, as .NET resolves them:
//   ApplicationData       %APPDATA%       $XDG_CONFIG_HOME or ~/.config
//   LocalApplicationData  %LOCALAPPDATA%  $XDG_DATA_HOME or ~/.local/share (macOS: ~/Library/Application Support)

#include <string>

#include "core/DotNet.h"
#include "core/Files.h"

namespace Mdk::Environment
{
    enum class SpecialFolder { ApplicationData, LocalApplicationData };

    // The folder's path; empty when it can't be told (no home).
    inline std::string GetFolderPath(SpecialFolder folder)
    {
#if defined(_WIN32)
        auto path = GetEnv(folder == SpecialFolder::ApplicationData ? "APPDATA" : "LOCALAPPDATA");
        return path.value_or("");
#else
        auto xdg = GetEnv(folder == SpecialFolder::ApplicationData ? "XDG_CONFIG_HOME" : "XDG_DATA_HOME");
#if defined(__APPLE__)
        if (folder == SpecialFolder::LocalApplicationData)
        {
            xdg.reset();
        }
#endif
        if (xdg.has_value() && Path::IsPathRooted(*xdg))
        {
            return *xdg;
        }

        auto home = GetEnv("HOME");
        if (!home.has_value() || home->empty())
        {
            return "";
        }

#if defined(__APPLE__)
        if (folder == SpecialFolder::LocalApplicationData)
        {
            return Path::Combine(*home, "Library/Application Support");
        }
#endif
        return folder == SpecialFolder::ApplicationData ? Path::Combine(*home, ".config") : Path::Combine(*home, ".local/share");
#endif
    }

    // OperatingSystem.IsWindows().
    constexpr bool IsWindows()
    {
#if defined(_WIN32)
        return true;
#else
        return false;
#endif
    }
}
