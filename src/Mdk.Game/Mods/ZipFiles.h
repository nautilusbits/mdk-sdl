#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Mdk.Game/Mods/ModCatalog.h"
#include "Mdk.Game/Mods/ModFiles.h"

namespace Mdk::Game::Mods
{
    // A mod that is a zip (mods/walls.zip): mod.txt at its root, or in its one top folder (a
    // zipped folder: that folder is the mod's top). Untrusted: entries are read in memory, never
    // extracted; those leaving the mod (.., absolute, drive letters, backslashes) are ignored; entries
    // too large or inflating far beyond their packed size (zip bombs) are refused, a zip too large as
    // a whole too. Paths ignore case. The zip stays open after the first read, until Close.
    class ZipFiles : public ModFiles
    {
    public:
        static constexpr const char* Extension = ".zip";

        ~ZipFiles() override;

        static std::shared_ptr<ZipFiles> Open(const std::string& path);

        bool Has(const std::string& path) override;

        std::vector<std::string> List(const std::string& folder) override;

        // An entry's bytes, never more than it declares (checked again: the zip may have changed).
        std::vector<byte> Read(const std::string& path) override;

        std::string Where(const std::string& path) override { return _path + ":" + path; }

        void Close() override;

    private:
        // An entry of the zip's directory (System.IO.Compression's ZipArchiveEntry).
        struct ZipEntry
        {
            std::string FullName;
            long long Length = 0;
            long long CompressedLength = 0;
        };

        // The open zip (miniz's reader; defined in ZipFiles.cpp, miniz's type can't be declared here).
        struct Archive;

        // The mod's paths to the entries' names, the paths in the zip's order (a Dictionary's).
        struct EntryNames
        {
            std::map<std::string, std::string, FolderLess> Names;
            std::vector<std::string> Order;
        };

        // Any one entry, unpacked (an 8K × 8K RGBA image is 256 MB unpacked, less as PNG).
        static constexpr long long MaxEntry = 256LL << 20;
        // All entries, unpacked.
        static constexpr long long MaxTotal = 2LL << 30;
        static constexpr int MaxEntries = 100'000;
        // Unpacked to packed, above RatioFrom (PNGs and models barely pack).
        static constexpr long long MaxRatio = 100;
        static constexpr long long RatioFrom = 1LL << 20;
        static constexpr char Separator = '/';

        std::string _path;
        // The mod's paths to the entries' names.
        EntryNames _entries;
        std::mutex _lock;
        std::unique_ptr<Archive> _archive;

        ZipFiles(const std::string& path, EntryNames entries);

        // The mod's paths to the entries' names, or null (refused).
        static std::optional<EntryNames> Index(const std::string& path, const std::vector<ZipEntry>& archive);

        // The mod's top in the zip: "" (mod.txt at the root, or none), else "walls/" (the
        // one top folder holding mod.txt).
        static std::string Top(const std::vector<ZipEntry>& entries);

        // A relative path that stays in the mod: "textures/WALL.png", not "../x", "/x",
        // "C:/x", "a\b", "a/./b" or "a//b".
        static bool Safe(const std::string& name);

        // Why an entry is refused (too large, a bomb), or null.
        static std::optional<std::string> Refusal(const ZipEntry& entry);

        // The zip again (after Close); no access is an IOException, as a folder's.
        std::unique_ptr<Archive> Reopen();

        // Open for reading; the user may still delete or replace the file.
        static std::unique_ptr<Archive> OpenRead(const std::string& path);
    };
}
