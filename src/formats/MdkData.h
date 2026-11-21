#pragma once

#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/DotNet.h"

namespace Mdk::Formats
{
    enum class EntryKind { File, Folder };

    // A file or folder of a tree; a file's size in bytes, or UnknownSize.
    struct DataEntry
    {
        static constexpr long long UnknownSize = -1;

        std::string Name;
        EntryKind Kind = EntryKind::File;
        long long Size = UnknownSize;

        bool operator==(const DataEntry&) const = default;
    };

    // A folder tree an installation is copied from (Android: the folder the user picked,
    // read through its content URIs). Paths are relative, '/'-separated: "" is the root.
    class IDataTree
    {
    public:
        virtual ~IDataTree() = default;

        // The files and folders of a folder.
        virtual std::vector<DataEntry> List(const std::string& folder) = 0;

        virtual std::unique_ptr<std::istream> Open(const std::string& file) = 0;
    };

    // Locates the original MDK installation and reads its files.
    //
    // Importing: an installation somewhere the game can't read files by path (Android's
    // shared storage) is copied once into a folder of the program's own.
    //   picked folder ─FindIn─► "" or "MDK" ─Import─► app folder/TRAVERSE, MISC... + stamp ─Imported─► MdkData
    class MdkData
    {
    public:
        const std::string& Dir() const { return _dir; }

        // The installation, or null: MDK_DATA_DIR, mdk in LocalPaths,
        // the folders above the program, GOG and Steam folders.
        static std::shared_ptr<MdkData> Find();

        // An installation read in place (Android: the folder the user chose): `folder`
        // or a folder of it (the user picked the folder above, e.g. "MDK"); null when there's none
        // or it can't be read.
        static std::shared_ptr<MdkData> At(const std::string& folder);

        // Absolute path of a data file, e.g. TRAVERSE/LEVEL3/LEVEL3O.MTO, named as on disk
        // (MISC/MDKFONT.FTI may be MISC/mdkfont.fti).
        std::string PathOf(const std::string& relative) const;

        std::vector<byte> Read(const std::string& relative) const;

        // The installation in a tree: its root (""), a folder of the root (the user picked
        // the folder above, e.g. "MDK"), or empty.
        static std::optional<std::string> FindIn(IDataTree& tree);

        // Copies the game's folders of `root` in the tree into `dir`, reporting (files done,
        // files) after each file. A file already there with the same size is kept (an import
        // again copies what changed).
        static std::shared_ptr<MdkData> Import(IDataTree& tree, const std::string& root, const std::string& dir, const std::function<void(int, int)>& progress);

        // Copies the folder `name` (any case) of `root` in the tree into `target`, e.g. HD
        // textures made on a PC; returns the files copied (0 without that folder). Unchanged
        // files are kept, as in Import.
        static int CopyFolder(IDataTree& tree, const std::string& root, const std::string& name, const std::string& target, const std::function<void(int, int)>& progress);

        // A finished import in `dir`, or null.
        static std::shared_ptr<MdkData> Imported(const std::string& dir);

    private:
        // A file every installation has.
        static constexpr const char* Marker = "TRAVERSE/TRAVSPRT.BNI";
        static constexpr const char* EnvironmentVariable = "MDK_DATA_DIR";
        // Written last: a copy without it was interrupted.
        static constexpr const char* ImportStamp = ".imported";
        static constexpr char Separator = '/';

        std::string _dir;

        explicit MdkData(std::string dir) : _dir(std::move(dir)) {}

        static bool HasMarker(const std::string& folder);

        // Copies files of the tree under `from` into `dir`, skipping those already there with
        // the same size; returns the files copied.
        static int CopyFiles(IDataTree& tree, const std::vector<std::pair<std::string, long long>>& files, const std::string& from, const std::string& dir, const std::function<void(int, int)>& progress);

        // The tree has Marker under `folder`, in any case.
        static bool HasMarker(IDataTree& tree, const std::string& folder);

        // The files under a folder of the tree, at any depth, with their sizes.
        static void FilesOf(IDataTree& tree, const std::string& folder, std::vector<std::pair<std::string, long long>>& files);

        static std::string Join(const std::string& folder, const std::string& name);
    };
}
