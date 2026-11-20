#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <doctest.h>
#include <miniz.h>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Files.h"
#include "Mdk.Core/ZipFile.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/Png.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Game/Flow/Settings.h"
#include "Mdk.Game/Mods/ModCatalog.h"
#include "Mdk.Game/Mods/ModFiles.h"
#include "Mdk.Game/Mods/ModImages.h"
#include "Mdk.Game/Mods/ModInfo.h"

using namespace Mdk;
using namespace Mdk::Formats;
using namespace Mdk::Game::Flow;
using namespace Mdk::Game::Mods;

// Mods packed as zips: read like folders (root or one top folder), never outside the
// mod, with entries too large or too compressed (zip bombs) refused.
namespace
{
    constexpr int Rgba = 4;
    // The central directory's file header and its uncompressed size's offset.
    constexpr uint CentralHeader = 0x02014b50;
    constexpr int CentralSize = 24;

    using Bytes = std::vector<byte>;
    using Entry = std::pair<std::string, Bytes>;

    // A zip writer storing its entries uncompressed and their names as given (miniz's writer
    // refuses "/a", "C:/a" and "a\b"; ZipArchive with CompressionLevel.NoCompression doesn't).
    //   local header + name + data ... central headers ... end of central directory
    class StoredZip
    {
        static constexpr uint LocalHeader = 0x04034b50;
        static constexpr uint EndHeader = 0x06054b50;
        static constexpr ushort Version = 20;
        // 1980-01-01, the DOS epoch.
        static constexpr ushort Date = 0x21;
        // 12:00:00 (hour << 11). Like .NET's entry time, never 0: miniz reads method and time as
        // one 32-bit value and, when both are 0, refuses the whole zip over one stored entry whose
        // sizes differ, where .NET opens it and refuses that entry alone.
        static constexpr ushort Time = 12 << 11;

        Bytes _bytes;
        Bytes _central;
        ushort _count = 0;

        static void U16(Bytes& to, ushort value)
        {
            to.push_back(static_cast<byte>(value));
            to.push_back(static_cast<byte>(value >> 8));
        }

        static void U32(Bytes& to, uint value)
        {
            U16(to, static_cast<ushort>(value));
            U16(to, static_cast<ushort>(value >> 16));
        }

        static void Append(Bytes& to, const void* data, size_t size)
        {
            auto from = static_cast<const byte*>(data);
            to.insert(to.end(), from, from + size);
        }

    public:
        void Add(const std::string& name, const Bytes& data)
        {
            auto crc = static_cast<uint>(mz_crc32(MZ_CRC32_INIT, data.data(), data.size()));
            auto size = static_cast<uint>(data.size());
            auto nameSize = static_cast<ushort>(name.size());
            auto offset = static_cast<uint>(_bytes.size());

            // Local header: signature, version, flags, method (stored), time, date, crc, sizes, name, extra.
            U32(_bytes, LocalHeader);
            U16(_bytes, Version);
            U16(_bytes, 0);
            U16(_bytes, 0);
            U16(_bytes, Time);
            U16(_bytes, Date);
            U32(_bytes, crc);
            U32(_bytes, size);
            U32(_bytes, size);
            U16(_bytes, nameSize);
            U16(_bytes, 0);
            Append(_bytes, name.data(), name.size());
            Append(_bytes, data.data(), data.size());

            // Central header: as above after "version made by", then comment, disk, attributes, offset.
            U32(_central, CentralHeader);
            U16(_central, Version);
            U16(_central, Version);
            U16(_central, 0);
            U16(_central, 0);
            U16(_central, Time);
            U16(_central, Date);
            U32(_central, crc);
            U32(_central, size);
            U32(_central, size);
            U16(_central, nameSize);
            U16(_central, 0);
            U16(_central, 0);
            U16(_central, 0);
            U16(_central, 0);
            U32(_central, 0);
            U32(_central, offset);
            Append(_central, name.data(), name.size());
            _count++;
        }

        Bytes Build() const
        {
            auto bytes = _bytes;
            auto offset = static_cast<uint>(bytes.size());
            bytes.insert(bytes.end(), _central.begin(), _central.end());
            U32(bytes, EndHeader);
            U16(bytes, 0);
            U16(bytes, 0);
            U16(bytes, _count);
            U16(bytes, _count);
            U32(bytes, static_cast<uint>(_central.size()));
            U32(bytes, offset);
            U16(bytes, 0);
            return bytes;
        }
    };

    // The C# test class's instance: a temp mods folder, deleted with what the test opened.
    class Fixture
    {
        // What the tests opened: closed before the folder goes (Windows keeps open files).
        std::vector<std::function<void()>> _close;

    public:
        std::string Folder;

        Fixture()
        {
            std::random_device random;
            auto name = "mdk-zip-mods-" + std::to_string(random()) + std::to_string(random());
            Folder = (std::filesystem::temp_directory_path() / name).string();
            std::filesystem::create_directories(Folder);
        }

        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;

        ~Fixture()
        {
            for (auto& close : _close)
            {
                close();
            }

            Directory::Delete(Folder, true);
        }

        std::shared_ptr<ModFiles> Open(const std::string& path)
        {
            auto files = ModFiles::Zip(path);
            _close.push_back([files] {
                if (files)
                {
                    files->Close();
                }
            });
            return files;
        }

        std::shared_ptr<ModCatalog> Scan()
        {
            auto catalog = ModCatalog::Scan(Folder);
            _close.push_back([catalog] { catalog->Close(); });
            return catalog;
        }

        // A zip in the mods folder of these entries (name, bytes).
        std::string Zip(const std::string& name, const std::vector<Entry>& entries)
        {
            auto path = Path::Combine(Folder, name);
            StoredZip zip;
            for (const auto& [entryName, bytes] : entries)
            {
                zip.Add(entryName, bytes);
            }

            File::WriteAllBytes(path, zip.Build());
            return path;
        }
    };

    Bytes Text(const std::string& text) { return Bytes(text.begin(), text.end()); }

    Bytes Image(byte red)
    {
        Bytes rgba(2 * 2 * Rgba);
        for (size_t i = 0; i < rgba.size(); i += Rgba)
        {
            rgba[i] = red;
            rgba[i + 3] = 0xFF;
        }

        return Png::Encode(2, 2, rgba, Png::Channels::Rgba);
    }

    Texture TextureOf(const std::string& name)
    {
        Texture texture;
        texture.Name = name;
        texture.Width = 2;
        texture.Height = 2;
        texture.FrameCount = 1;
        texture.Indices = {1, 1, 1, 1};
        return texture;
    }

    // Sets the first entry's uncompressed size in the central directory (a lying zip).
    void DeclareSize(const std::string& path, uint size)
    {
        auto bytes = File::ReadAllBytes(path);
        for (size_t i = 0; i + sizeof(uint) <= bytes.size(); i++)
        {
            uint value = 0;
            std::memcpy(&value, bytes.data() + i, sizeof(uint));
            if (value != CentralHeader)
            {
                continue;
            }

            std::memcpy(bytes.data() + i + CentralSize, &size, sizeof(uint));
            File::WriteAllBytes(path, bytes);
            return;
        }

        throw InvalidDataException("no central directory");
    }

    using Strings = std::vector<std::string>;
}

// mod.txt at the root: the mod is the zip's name, its files by path, case ignored.
TEST_CASE("ZipModsTests.ReadsTheRoot")
{
    Fixture fixture;
    fixture.Zip("walls.zip", {{"mod.txt", Text("name=Clean walls\npriority=3\n")}, {"textures/WALL.png", Image(7)}});

    auto catalog = fixture.Scan();

    REQUIRE(catalog->Mods.size() == 1);
    const auto& mod = catalog->Mods[0];
    CHECK(mod.Folder == "walls");
    CHECK(mod.Name == "Clean walls");
    CHECK(mod.Priority == 3);
    auto all = catalog->Files(Settings());
    REQUIRE(all.size() == 1);
    auto files = all[0];
    CHECK(files->Has("TEXTURES/wall.PNG"));
    CHECK(files->List("textures") == Strings{"textures/WALL.png"});
    auto image = ModImages::Open({files}, 3)->Texture("WALL", TextureOf("WALL"), Palette());
    REQUIRE(image.has_value());
    CHECK(image->Rgba[0] == 7);
}

// A zip of a folder (one top folder holding mod.txt) reads as that folder.
TEST_CASE("ZipModsTests.ReadsOneTopFolder")
{
    Fixture fixture;
    auto path = fixture.Zip("walls.zip", {{"walls/mod.txt", Text("name=Nested\n")}, {"walls/textures/LEVEL3/WALL.png", Image(1)}});

    auto files = fixture.Open(path);

    REQUIRE(files != nullptr);
    CHECK(files->Name == "walls");
    CHECK(files->Has("mod.txt"));
    CHECK(files->List("textures") == Strings{"textures/LEVEL3/WALL.png"});
    CHECK(ModInfo::Load(*files).Name == "Nested");
}

// Without mod.txt in it, a lone top folder is the mod's own (textures/).
TEST_CASE("ZipModsTests.KeepsATopFolderWithoutModInfo")
{
    Fixture fixture;
    auto path = fixture.Zip("walls.zip", {{"textures/WALL.png", Image(1)}});

    auto files = fixture.Open(path);

    REQUIRE(files != nullptr);
    CHECK(files->List("textures") == Strings{"textures/WALL.png"});
}

// Entries that would leave the mod are ignored.
TEST_CASE("ZipModsTests.IgnoresPathsOutside")
{
    Fixture fixture;
    auto path = fixture.Zip("evil.zip",
        {{"../textures/A.png", Image(1)}, {"/textures/B.png", Image(1)}, {"C:/textures/C.png", Image(1)},
            {"textures\\D.png", Image(1)}, {"textures/../../E.png", Image(1)}, {"textures/./F.png", Image(1)},
            {"textures//G.png", Image(1)}, {"textures/OK.png", Image(1)}});

    auto files = fixture.Open(path);

    REQUIRE(files != nullptr);
    CHECK(files->List("textures") == Strings{"textures/OK.png"});
    CHECK_FALSE(files->Has("../textures/A.png"));
    CHECK_THROWS_AS(files->Read("textures/../../E.png"), FileNotFoundException);
}

// An entry larger than the limit, or one inflating far beyond its packed size (a zip
// bomb), is refused; the others stay.
TEST_CASE("ZipModsTests.RefusesOversizedEntries")
{
    Fixture fixture;
    auto big = fixture.Zip("big.zip", {{"textures/BIG.png", Image(1)}, {"textures/OK.png", Image(2)}});
    DeclareSize(big, static_cast<uint>(std::numeric_limits<int>::max()));
    auto bigFiles = fixture.Open(big);
    REQUIRE(bigFiles != nullptr);
    CHECK(bigFiles->List("textures") == Strings{"textures/OK.png"});

    // Deflated (CompressionLevel.Optimal).
    auto bomb = Path::Combine(fixture.Folder, "bomb.zip");
    const Entry entries[] = {{"textures/BOMB.png", Bytes(8 << 20)}};
    ZipFile::Create(bomb, entries);

    auto bombFiles = fixture.Open(bomb);
    REQUIRE(bombFiles != nullptr);
    CHECK(bombFiles->List("textures").empty());
}

// An entry inflating beyond the size it declares is refused when read.
TEST_CASE("ZipModsTests.RefusesEntriesLargerThanDeclared")
{
    Fixture fixture;
    auto path = fixture.Zip("liar.zip", {{"textures/WALL.png", Image(1)}});
    DeclareSize(path, 4);

    auto files = fixture.Open(path);

    REQUIRE(files != nullptr);
    CHECK_THROWS(files->Read("textures/WALL.png"));
    CHECK_FALSE(ModImages::Open({files}, 3)->Texture("WALL", TextureOf("WALL"), Palette()).has_value());
}

// A broken zip is no mod (and no crash).
TEST_CASE("ZipModsTests.SkipsABrokenZip")
{
    Fixture fixture;
    File::WriteAllText(Path::Combine(fixture.Folder, "broken.zip"), "not a zip");

    CHECK(fixture.Scan()->Mods.empty());
}

// A folder and a zip of one name: the folder wins; the switch is the name's.
TEST_CASE("ZipModsTests.FolderWinsOverZip")
{
    Fixture fixture;
    fixture.Zip("walls.zip", {{"mod.txt", Text("name=Zipped\n")}});
    fixture.Zip("other.ZIP", {{"mod.txt", Text("name=Other\n")}});
    fixture.Zip(".hidden.zip", {{"mod.txt", Text("name=Hidden\n")}});
    auto folder = Path::Combine(fixture.Folder, "walls");
    Directory::CreateDirectory(folder);
    File::WriteAllText(Path::Combine(folder, ModInfo::FileName), "name=Folder\n");
    Settings settings;
    settings.Mods["other"] = ModState::Off;

    auto catalog = fixture.Scan();

    Strings names;
    for (const auto& mod : catalog->Mods)
    {
        names.push_back(mod.Name);
    }

    Strings enabled;
    for (const auto& mod : catalog->Enabled(settings))
    {
        enabled.push_back(mod.Folder);
    }

    CHECK(names == Strings{"Other", "Folder"});
    CHECK(enabled == Strings{"walls"});
}
