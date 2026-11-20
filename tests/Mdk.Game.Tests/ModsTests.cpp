#include <array>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/Files.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/Png.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Game/Flow/Settings.h"
#include "Mdk.Game/HdTextures/HdGenerator.h"
#include "Mdk.Game/HdTextures/HdImage.h"
#include "Mdk.Game/HdTextures/HdKey.h"
#include "Mdk.Game/Kurt/KurtSprite.h"
#include "Mdk.Game/Mods/ModCatalog.h"
#include "Mdk.Game/Mods/ModFiles.h"
#include "Mdk.Game/Mods/ModImages.h"
#include "Mdk.Game/Mods/ModInfo.h"
#include "Shared/TempDir.h"

using namespace Mdk;
using namespace Mdk::Engine::Render;
using namespace Mdk::Formats;
using namespace Mdk::Game::Flow;
using namespace Mdk::Game::HdTextures;
using namespace Mdk::Game::Kurt;
using namespace Mdk::Game::Mods;

// Mods: mod.txt, finding and ordering mods, switching them in the settings, and which mod's
// image replaces a texture (key, level, name; higher priority first).
namespace
{
    constexpr int Rgba = 4;
    constexpr byte Opaque = 255;

    // A PNG of one colour (red = red), alpha half where asked.
    void Image(const std::string& path, int width, int height, byte red, byte alpha = Opaque)
    {
        std::vector<byte> rgba(width * height * Rgba);
        for (int i = 0; i < width * height; i++)
        {
            rgba[i * Rgba] = red;
            rgba[i * Rgba + 3] = alpha;
        }

        Directory::CreateDirectory(Path::GetDirectoryName(path));
        File::WriteAllBytes(path, Png::Encode(width, height, rgba, Png::Channels::Rgba));
    }

    std::shared_ptr<Texture> MakeTexture(const std::string& name, int frames = 1)
    {
        auto texture = std::make_shared<Texture>();
        texture->Name = name;
        texture->Width = 2;
        texture->Height = 2;
        texture->FrameCount = frames;
        texture->Indices.assign(4 * frames, 1);
        return texture;
    }

    Palette Colours()
    {
        Palette palette;
        palette.Rgba.fill(100);
        return palette;
    }

    std::vector<std::string> Folders(const std::vector<ModInfo>& mods)
    {
        std::vector<std::string> folders;
        for (const auto& m : mods)
        {
            folders.push_back(m.Folder);
        }

        return folders;
    }

    std::optional<std::vector<byte>> RgbaOf(const std::optional<HdImage>& image)
    {
        return image ? std::optional<std::vector<byte>>(image->Rgba) : std::nullopt;
    }

    // The C# class's per-test temp folder (IDisposable).
    class ModsFixture
    {
    protected:
        Mdk::Tests::TempDir _temp{"mdk-mods-"};
        const std::string& _folder = _temp.Path();

        std::string Mod(const std::string& name, const std::optional<std::string>& info = std::nullopt)
        {
            auto path = Path::Combine(_folder, name);
            Directory::CreateDirectory(path);
            if (info)
            {
                File::WriteAllText(Path::Combine(path, ModInfo::FileName), *info);
            }

            return path;
        }

        std::shared_ptr<ModImages> Open(std::optional<int> level, std::initializer_list<const char*> mods)
        {
            std::vector<std::shared_ptr<ModFiles>> files;
            for (const char* m : mods)
            {
                files.push_back(ModFiles::Folder(Path::Combine(_folder, m)));
            }

            return ModImages::Open(files, level);
        }
    };
}

// --- mod.txt and the catalogue -----------------------------------------------------------

TEST_CASE("ModsTests.ParsesModInfo")
{
    auto info = ModInfo::Parse("walls", "# my mod\nname = Clean walls\nauthor=Me\nversion=1.2\ndescription=Walls, cleaner\npriority=5\nunknown=x\n");

    CHECK(info == ModInfo{"walls", "Clean walls", "Me", "1.2", "Walls, cleaner", 5});
    CHECK(ModInfo::Parse("walls", info.Format()) == info);
}

TEST_CASE("ModsTests.ModInfoDefaults")
{
    CHECK(ModInfo::Parse("walls", "priority=high\n") == ModInfo{"walls", "walls", "", "", "", 0});
}

// Higher priority first, then by folder name; files and hidden folders aren't mods.
TEST_CASE_FIXTURE(ModsFixture, "ModsTests.FindsAndOrdersMods")
{
    Mod("b-low");
    Mod("a-low", "priority=0");
    Mod("z-high", "name=High\npriority=10");
    File::WriteAllText(Path::Combine(_folder, "readme.txt"), "");
    Mod(".git");

    auto catalog = ModCatalog::Scan(_folder);

    CHECK(Folders(catalog->Mods) == std::vector<std::string>{"z-high", "a-low", "b-low"});
    CHECK(catalog->Mods[0].Name == "High");
    CHECK(ModCatalog::Scan(Path::Combine(_folder, "missing"))->Mods.empty());
}

TEST_CASE_FIXTURE(ModsFixture, "ModsTests.SettingsSwitchMods")
{
    Mod("a");
    Mod("b");
    auto catalog = ModCatalog::Scan(_folder);
    Settings settings;

    // A new mod is on.
    CHECK(Folders(catalog->Enabled(settings)) == std::vector<std::string>{"a", "b"});

    settings.Mods["a"] = ModState::Off;
    CHECK(Folders(catalog->Enabled(settings)) == std::vector<std::string>{"b"});

    // --mod=a: that one only, for this run.
    catalog->Only(settings, {"A"});
    CHECK(Folders(catalog->Enabled(settings)) == std::vector<std::string>{"a"});
}

TEST_CASE("ModsTests.ModSettingsRoundTrip")
{
    Settings settings;
    settings.Mods["walls"] = ModState::Off;
    settings.Mods["hd-textures"] = ModState::On;

    auto read = Settings::Parse(settings.Format());

    CHECK(read.Format() == settings.Format());
    CHECK(read.StateOf("WALLS") == ModState::Off);
    CHECK(read.StateOf("hd-textures") == ModState::On);
    CHECK(read.StateOf("new") == ModState::On);
}

// Older settings' HD switch becomes the HD textures mod's.
TEST_CASE("ModsTests.OldHdSwitchBecomesTheMods")
{
    const std::array<std::tuple<const char*, ModState>, 2> cases = {{
        {"Hd", ModState::On},
        {"Original", ModState::Off},
    }};
    for (const auto& [value, state] : cases)
    {
        auto read = Settings::Parse(std::string("textures=") + value + "\n");

        CHECK(read.StateOf(HdGenerator::ModFolder) == state);
    }
}

// --- Texture images ----------------------------------------------------------------------

TEST_CASE_FIXTURE(ModsFixture, "ModsTests.NoModNoImage")
{
    CHECK_FALSE(Open(3, {})->Texture("WALL", *MakeTexture("WALL"), Colours()).has_value());
}

// Any size; premultiplied as the renderer's colour textures.
TEST_CASE_FIXTURE(ModsFixture, "ModsTests.NamedTextureReplaces")
{
    Image(Path::Combine(Mod("m"), "textures", "WALL.png"), 8, 6, 200, 128);

    auto image = Open(3, {"m"})->Texture("WALL", *MakeTexture("WALL"), Colours());

    REQUIRE(image.has_value());
    CHECK(image->Width == 8);
    CHECK(image->Height == 6);
    CHECK(image->Frames == 1);
    CHECK(std::vector<byte>(image->Rgba.begin(), image->Rgba.begin() + Rgba) == std::vector<byte>{100, 0, 0, 128});
}

// Within a mod: the exact view (key), then the level's folder, then all levels; names ignore case.
TEST_CASE_FIXTURE(ModsFixture, "ModsTests.KeyThenLevelThenName")
{
    auto mod = Mod("m");
    auto texture = MakeTexture("WALL");
    Image(Path::Combine(mod, "textures", "wall.png"), 2, 2, 1);
    Image(Path::Combine(mod, "textures", "LEVEL3", "WALL.png"), 2, 2, 2);

    CHECK(Open(3, {"m"})->Texture("WALL", *texture, Colours()).value().Rgba[0] == 2);
    CHECK(Open(4, {"m"})->Texture("WALL", *texture, Colours()).value().Rgba[0] == 1);

    Image(Path::Combine(mod, "textures", "LEVEL7", "WALL@" + HdKey::Of(*texture, Colours()) + ".png"), 2, 2, 3);
    CHECK(Open(3, {"m"})->Texture("WALL", *texture, Colours()).value().Rgba[0] == 3);
    CHECK(Open(3, {"m"})->Texture("WALL", *texture, Palette()).value().Rgba[0] == 2);
}

// Mods in the catalogue's order: the first that has an image wins, even by name over another's key.
TEST_CASE_FIXTURE(ModsFixture, "ModsTests.FirstModWins")
{
    auto texture = MakeTexture("WALL");
    Image(Path::Combine(Mod("high"), "textures", "WALL.png"), 2, 2, 1);
    Image(Path::Combine(Mod("low"), "textures", "LEVEL3", "WALL@" + HdKey::Of(*texture, Colours()) + ".png"), 2, 2, 2);

    CHECK(Open(3, {"high", "low"})->Texture("WALL", *texture, Colours()).value().Rgba[0] == 1);
    CHECK(Open(3, {"low", "high"})->Texture("WALL", *texture, Colours()).value().Rgba[0] == 2);
}

// Animated textures: one image per frame (NAME_0, NAME_1...) or a strip of the frames stacked downwards.
TEST_CASE_FIXTURE(ModsFixture, "ModsTests.AnimatedFrames")
{
    auto mod = Mod("m");
    Image(Path::Combine(mod, "textures", "ANIM_0.png"), 4, 4, 1);
    Image(Path::Combine(mod, "textures", "ANIM_1.png"), 4, 4, 2);
    Image(Path::Combine(mod, "textures", "STRIP.png"), 4, 8, 3);
    Image(Path::Combine(mod, "textures", "ODD.png"), 4, 5, 3);
    Image(Path::Combine(mod, "textures", "MIXED_0.png"), 4, 4, 1);
    Image(Path::Combine(mod, "textures", "MIXED_1.png"), 2, 2, 1);
    auto images = Open(3, {"m"});

    auto frames = images->Texture("ANIM", *MakeTexture("ANIM", 2), Colours());
    REQUIRE(frames.has_value());
    CHECK(frames->Width == 4);
    CHECK(frames->Height == 4);
    CHECK(frames->Frames == 2);
    CHECK(frames->Rgba[4 * 4 * Rgba] == 2);

    auto strip = images->Texture("STRIP", *MakeTexture("STRIP", 2), Colours());
    REQUIRE(strip.has_value());
    CHECK(strip->Width == 4);
    CHECK(strip->Height == 4);
    CHECK(strip->Frames == 2);

    // Frames that don't stack: the original stays.
    CHECK_FALSE(images->Texture("ODD", *MakeTexture("ODD", 2), Colours()).has_value());
    CHECK_FALSE(images->Texture("MIXED", *MakeTexture("MIXED", 2), Colours()).has_value());
    CHECK_FALSE(images->Texture("ANIM", *MakeTexture("ANIM", 3), Colours()).has_value());
}

TEST_CASE_FIXTURE(ModsFixture, "ModsTests.BrokenImageKeepsTheOriginal")
{
    auto path = Path::Combine(Mod("m"), "textures", "WALL.png");
    Directory::CreateDirectory(Path::GetDirectoryName(path));
    File::WriteAllText(path, "not a png");

    CHECK_FALSE(Open(3, {"m"})->Texture("WALL", *MakeTexture("WALL"), Colours()).has_value());
}

// 2D images (HUD, menus) come from images/, textures from textures/.
TEST_CASE_FIXTURE(ModsFixture, "ModsTests.ImagesAreTheirOwn")
{
    auto mod = Mod("m");
    Image(Path::Combine(mod, "images", "SC_STAT.png"), 4, 4, 9);
    auto images = Open(std::nullopt, {"m"});

    CHECK(images->Image("SC_STAT", *MakeTexture("SC_STAT"), Colours()).value().Rgba[0] == 9);
    CHECK_FALSE(images->Texture("SC_STAT", *MakeTexture("SC_STAT"), Colours()).has_value());
}

// Kurt's frames (ANIMATION_frame) in the enhanced look only.
TEST_CASE_FIXTURE(ModsFixture, "ModsTests.KurtTakesModFrames")
{
    Image(Path::Combine(Mod("m"), "textures", "K_RUN_0.png"), 4, 4, 5);
    auto images = Open(3, {"m"});
    auto frame = MakeTexture("K_RUN_0");

    CHECK(KurtSprite::Upscaled(images.get(), Shading::Sprite, "K_RUN_0", *frame, Colours()).has_value());
    CHECK_FALSE(KurtSprite::Upscaled(images.get(), Shading::Original, "K_RUN_0", *frame, Colours()).has_value());
    CHECK_FALSE(KurtSprite::Upscaled(nullptr, Shading::Sprite, "K_RUN_0", *frame, Colours()).has_value());
    CHECK_FALSE(KurtSprite::Upscaled(images.get(), Shading::Sprite, "K_RUN_1", *frame, Colours()).has_value());
}

// The overrides found are counted (the console's mods, F3).
TEST_CASE_FIXTURE(ModsFixture, "ModsTests.CountsWhatItFinds")
{
    Image(Path::Combine(Mod("m"), "textures", "WALL.png"), 2, 2, 1);
    auto images = Open(3, {"m"});

    images->Texture("WALL", *MakeTexture("WALL"), Colours());
    images->Texture("OTHER", *MakeTexture("OTHER"), Colours());

    CHECK(images->Found() == std::vector<std::string>{"WALL"});
}

// Straight alpha becomes premultiplied, rounded: (c x a + 127) / 255; opaque pixels keep their
// colour, clear ones lose it.
TEST_CASE_FIXTURE(ModsFixture, "ModsTests.PremultipliesRounded")
{
    const std::array<byte, 5> alphas = {Opaque, 254, 128, 1, 0};
    std::vector<byte> rgba(256 * alphas.size() * Rgba);
    for (int i = 0; i < static_cast<int>(rgba.size()) / Rgba; i++)
    {
        rgba[i * Rgba] = static_cast<byte>(i);
        rgba[i * Rgba + 1] = static_cast<byte>(~i);
        rgba[i * Rgba + 2] = static_cast<byte>(i * 7);
        rgba[i * Rgba + 3] = alphas[i / 256];
    }

    auto path = Path::Combine(Mod("m"), "textures", "WALL.png");
    Directory::CreateDirectory(Path::GetDirectoryName(path));
    File::WriteAllBytes(path, Png::Encode(256, static_cast<int>(alphas.size()), rgba, Png::Channels::Rgba));
    auto image = Open(3, {"m"})->Texture("WALL", *MakeTexture("WALL"), Colours());
    REQUIRE(image.has_value());

    auto expected = rgba;
    for (size_t i = 0; i < expected.size(); i += Rgba)
    {
        for (int c = 0; c < 3; c++)
        {
            expected[i + c] = static_cast<byte>((rgba[i + c] * rgba[i + 3] + 127) / 255);
        }
    }

    CHECK(image->Rgba == expected);
}

// Prefetched images are the ones a lookup would decode (the exact view, by name).
TEST_CASE_FIXTURE(ModsFixture, "ModsTests.PrefetchesTheSameImages")
{
    auto palette = Colours();
    auto keyed = MakeTexture("K_RUN_0");
    Image(Path::Combine(Mod("m"), "textures", "LEVEL3", "K_RUN_0@" + HdKey::Of(*keyed, palette) + ".png"), 4, 4, 5, 128);
    Image(Path::Combine(_folder, "m", "textures", "K_RUN_1.png"), 4, 4, 9, 200);
    auto plain = Open(8, {"m"});
    auto prefetched = Open(8, {"m"});

    prefetched->Prefetch({{"K_RUN_0", keyed}, {"K_RUN_1", MakeTexture("K_RUN_1")}, {"K_NONE", MakeTexture("K_NONE")}}, palette);

    for (const char* name : {"K_RUN_0", "K_RUN_1", "K_NONE"})
    {
        CHECK(RgbaOf(plain->Texture(name, *MakeTexture(name), palette)) == RgbaOf(prefetched->Texture(name, *MakeTexture(name), palette)));
    }

    CHECK(prefetched->Texture("K_RUN_0", *keyed, palette).has_value());
}
