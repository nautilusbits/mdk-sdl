#include "Mdk.Game/Flow/TreePaths.h"

#include <algorithm>
#include <cstring>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Game::Flow
{
    std::optional<std::string> TreePaths::Contained(const std::optional<std::string>& path)
    {
        if (!path.has_value() || !Str::StartsWith(*path, std::string(Volumes) + Separator))
        {
            return std::nullopt;
        }

        auto parts = Str::Split(*path, Separator);
        auto climbs = std::any_of(parts.begin(), parts.end(), [](const std::string& part) { return part == Up || part == Here; });
        return climbs ? std::nullopt : path;
    }

    std::optional<std::string> TreePaths::PathOf(const std::string& authority, const std::string& documentId,
        const std::string& primaryRoot)
    {
        if (authority == Downloads && Str::StartsWith(documentId, Raw))
        {
            return documentId.substr(std::strlen(Raw));
        }

        auto colon = Str::IndexOf(documentId, VolumeSeparator);
        if (authority != ExternalStorage || colon < 0)
        {
            return std::nullopt;
        }

        auto volume = documentId.substr(0, static_cast<size_t>(colon));
        std::string root;
        if (volume == PrimaryVolume)
        {
            root = primaryRoot;
        }
        else if (volume == HomeVolume)
        {
            root = primaryRoot + Separator + HomeFolder;
        }
        else
        {
            root = std::string(Volumes) + Separator + volume;
        }

        auto folder = Str::Trim(std::string_view(documentId).substr(static_cast<size_t>(colon) + 1), std::string_view(&Separator, 1));
        return folder.empty() ? root : root + Separator + folder;
    }

    std::optional<std::string> TreePaths::PrimaryRoot(const std::string& appFiles)
    {
        auto at = Str::IndexOf(appFiles, AppData);
        return at < 0 ? std::nullopt : std::optional<std::string>(appFiles.substr(0, static_cast<size_t>(at)));
    }
}
