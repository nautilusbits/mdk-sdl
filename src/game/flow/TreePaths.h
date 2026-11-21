#pragma once

#include <optional>
#include <string>

namespace Mdk::Game::Flow
{
    // Android: the file path of a folder picked with the system's folder picker (a tree URI's
    // authority and document id), for the game to read it in place with All files access.
    //   externalstorage  primary:MDK        ─► /storage/emulated/0/MDK
    //                    home:MDK           ─► /storage/emulated/0/Documents/MDK
    //                    1234-ABCD:MDK      ─► /storage/1234-ABCD/MDK (SD card)
    //   downloads        raw:/storage/...   ─► /storage/...
    //   anything else (cloud, msf:42)       ─► null
    struct TreePaths
    {
        static constexpr const char* ExternalStorage = "com.android.externalstorage.documents";
        static constexpr const char* Downloads = "com.android.providers.downloads.documents";

        // The picked folder's path; null when it has none (a cloud, a Downloads list entry).
        static std::optional<std::string> Resolve(const std::string& authority, const std::string& documentId,
            const std::string& primaryRoot)
        {
            return Contained(PathOf(authority, documentId, primaryRoot));
        }

        // The primary storage's root, from the app's external files folder; null when that
        // isn't on it.
        static std::optional<std::string> PrimaryRoot(const std::string& appFiles);

    private:
        static constexpr const char* PrimaryVolume = "primary";
        static constexpr const char* HomeVolume = "home";
        static constexpr const char* HomeFolder = "Documents";
        static constexpr const char* Raw = "raw:";
        static constexpr const char* Volumes = "/storage";
        // The app's external files folder: primary storage/Android/data/package/files.
        static constexpr const char* AppData = "/Android/data/";
        static constexpr char VolumeSeparator = ':';
        static constexpr char Separator = '/';
        static constexpr const char* Up = "..";
        static constexpr const char* Here = ".";

        // A path on the storage volumes only, with no .. or . part to climb out of them.
        static std::optional<std::string> Contained(const std::optional<std::string>& path);

        static std::optional<std::string> PathOf(const std::string& authority, const std::string& documentId,
            const std::string& primaryRoot);
    };
}
