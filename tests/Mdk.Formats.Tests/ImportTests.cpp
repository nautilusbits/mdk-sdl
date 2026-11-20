#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#include "Mdk.Core/Files.h"
#include "Mdk.Formats/MdkData.h"
#include "Shared/TempDir.h"

using namespace Mdk;
using namespace Mdk::Formats;

// Copying an installation the user put somewhere (Android: a folder picked on the phone)
// into a folder of the program's own.

namespace
{
    // A tree of the file system, as Android's picked folder would be.
    class FolderTree : public IDataTree
    {
    public:
        explicit FolderTree(std::string root) : _root(std::move(root)) {}

        std::vector<DataEntry> List(const std::string& folder) override
        {
            auto dir = Path::Combine(_root, folder);
            std::vector<DataEntry> entries;
            for (auto& d : Directory::GetDirectories(dir))
            {
                entries.push_back(DataEntry{Path::GetFileName(d), EntryKind::Folder});
            }

            for (auto& f : Directory::GetFiles(dir))
            {
                entries.push_back(DataEntry{Path::GetFileName(f), EntryKind::File, File::Length(f)});
            }

            return entries;
        }

        std::unique_ptr<std::istream> Open(const std::string& file) override
        {
            auto path = Path::Combine(_root, file);
            return std::make_unique<std::ifstream>(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
        }

    private:
        std::string _root;
    };

    class Fixture
    {
    public:
        Mdk::Tests::TempDir Temp{"mdk-import-"};

        // A file of `relative` under the temp folder, its text its path.
        std::string Put(const std::string& relative)
        {
            auto path = Path::Combine(Temp.Path(), relative);
            Directory::CreateDirectory(Path::GetDirectoryName(path));
            File::WriteAllText(path, relative);
            return path;
        }

        // An installation as copied from a CD or a store, in any case, with files the game doesn't read.
        FolderTree Installation(const std::string& under = "")
        {
            Put(Path::Combine(under, "TRAVERSE/travsprt.bni"));
            Put(Path::Combine(under, "TRAVERSE/LEVEL3/LEVEL3O.MTO"));
            Put(Path::Combine(under, "MISC/MDKFONT.FTI"));
            Put(Path::Combine(under, "FALL3D/FALL3D.BNI"));
            Put(Path::Combine(under, "STREAM/STREAM.MTO"));
            Put(Path::Combine(under, "MDK95.EXE"));
            Put(Path::Combine(under, "Saves/LASTGAME.SAV"));
            return FolderTree(Temp.Path());
        }
    };
}

TEST_CASE_FIXTURE(Fixture, "ImportTests.TheInstallationIsThePickedFolder")
{
    auto tree = Installation();
    CHECK(std::optional<std::string>("") == MdkData::FindIn(tree));
}

TEST_CASE_FIXTURE(Fixture, "ImportTests.TheInstallationIsAFolderOfThePickedOne")
{
    auto tree = Installation("MDK");
    CHECK(std::optional<std::string>("MDK") == MdkData::FindIn(tree));
}

TEST_CASE_FIXTURE(Fixture, "ImportTests.AFolderWithoutTheGameIsNoInstallation")
{
    Put("Music/song.mp3");
    Put("TRAVERSE/other.bni");
    FolderTree tree(Temp.Path());
    CHECK(!MdkData::FindIn(tree).has_value());
}

TEST_CASE_FIXTURE(Fixture, "ImportTests.TheGameFoldersAreCopied")
{
    auto tree = Installation("MDK");
    auto target = Path::Combine(Temp.Path(), "copy");
    std::vector<std::pair<int, int>> steps;

    auto data = MdkData::Import(tree, "MDK", target, [&](int done, int total) { steps.emplace_back(done, total); });

    CHECK(Str::EndsWith(File::ReadAllText(data->PathOf("TRAVERSE/LEVEL3/LEVEL3O.MTO")), "LEVEL3O.MTO"));
    CHECK(File::Exists(data->PathOf("TRAVERSE/TRAVSPRT.BNI")));
    CHECK(File::Exists(data->PathOf("MISC/MDKFONT.FTI")));
    CHECK(!File::Exists(Path::Combine(target, "MDK95.EXE")));
    CHECK(!Directory::Exists(Path::Combine(target, "Saves")));
    CHECK(std::pair(5, 5) == steps.back());
    CHECK(MdkData::Imported(target) != nullptr);
}

TEST_CASE_FIXTURE(Fixture, "ImportTests.AFolderBesideTheGameIsCopiedElsewhere")
{
    auto tree = Installation("MDK");
    Put("MDK/textures-hd/manifest.txt");
    Put("MDK/textures-hd/LEVEL3/WALL_1.png");
    auto target = Path::Combine(Temp.Path(), "user", "textures-hd");

    auto copied = MdkData::CopyFolder(tree, "MDK", "TEXTURES-HD", target, nullptr);

    CHECK(2 == copied);
    CHECK(Str::EndsWith(File::ReadAllText(Path::Combine(target, "manifest.txt")), "manifest.txt"));
    CHECK(File::Exists(Path::Combine(target, "LEVEL3", "WALL_1.png")));
}

// Mods come along as they are: folders and zips.
TEST_CASE_FIXTURE(Fixture, "ImportTests.ZippedModsAreCopied")
{
    auto tree = Installation("MDK");
    Put("MDK/mods/walls/mod.txt");
    Put("MDK/mods/hud.zip");
    auto target = Path::Combine(Temp.Path(), "user", "mods");

    CHECK(2 == MdkData::CopyFolder(tree, "MDK", "mods", target, nullptr));
    CHECK(Str::EndsWith(File::ReadAllText(Path::Combine(target, "hud.zip")), "hud.zip"));
    CHECK(File::Exists(Path::Combine(target, "walls", "mod.txt")));
}

TEST_CASE_FIXTURE(Fixture, "ImportTests.AMissingFolderCopiesNothing")
{
    auto tree = Installation("MDK");
    auto target = Path::Combine(Temp.Path(), "user", "textures-hd");

    CHECK(0 == MdkData::CopyFolder(tree, "MDK", "textures-hd", target, nullptr));
    CHECK(!Directory::Exists(target));
}

TEST_CASE_FIXTURE(Fixture, "ImportTests.AnImportAgainCopiesOnlyChangedFiles")
{
    auto tree = Installation("MDK");
    auto target = Path::Combine(Temp.Path(), "copy");
    auto data = MdkData::Import(tree, "MDK", target, nullptr);

    // Same size: kept; another size: copied again.
    auto kept = data->PathOf("MISC/MDKFONT.FTI");
    File::WriteAllText(kept, "MDK/MISC/MDKFONT.FTX");
    auto changed = data->PathOf("STREAM/STREAM.MTO");
    File::WriteAllText(changed, "old");
    std::vector<std::pair<int, int>> steps;

    MdkData::Import(tree, "MDK", target, [&](int done, int total) { steps.emplace_back(done, total); });

    CHECK("MDK/MISC/MDKFONT.FTX" == File::ReadAllText(kept));
    CHECK(Str::EndsWith(File::ReadAllText(changed), "STREAM.MTO"));
    CHECK(std::pair(5, 5) == steps.back());
}

TEST_CASE_FIXTURE(Fixture, "ImportTests.AnInterruptedCopyIsNoInstallation")
{
    Put("copy/TRAVERSE/TRAVSPRT.BNI");
    CHECK(MdkData::Imported(Path::Combine(Temp.Path(), "copy")) == nullptr);
}

TEST_CASE_FIXTURE(Fixture, "ImportTests.NoCopyIsNoInstallation")
{
    CHECK(MdkData::Imported(Path::Combine(Temp.Path(), "none")) == nullptr);
}

namespace
{
    // A stream that fails after its first bytes, as a content URI whose provider goes away.
    class FailingBuffer : public std::streambuf
    {
    protected:
        int_type underflow() override
        {
            if (_served)
            {
                throw IOException("read failed");
            }

            _served = true;
            setg(_bytes, _bytes, _bytes + sizeof(_bytes));
            return traits_type::to_int_type(_bytes[0]);
        }

    private:
        char _bytes[4] = {'M', 'D', 'K', '!'};
        bool _served = false;
    };

    class FailingStream : public std::istream
    {
    public:
        FailingStream() : std::istream(&_buffer) {}

    private:
        FailingBuffer _buffer;
    };

    // The installation's tree, its files failing while read.
    class FailingTree : public FolderTree
    {
    public:
        using FolderTree::FolderTree;

        std::unique_ptr<std::istream> Open(const std::string& file) override { return std::make_unique<FailingStream>(); }
    };
}

// C#'s Stream.CopyTo passes a read's failure on: the copy stops, unstamped.
TEST_CASE_FIXTURE(Fixture, "ImportTests.AFailedReadStopsTheImport")
{
    Installation();
    FailingTree tree(Temp.Path());
    auto dir = Path::Combine(Temp.Path(), "copy");

    CHECK_THROWS_AS(MdkData::Import(tree, "", dir, nullptr), IOException);
    CHECK(MdkData::Imported(dir) == nullptr);
}
