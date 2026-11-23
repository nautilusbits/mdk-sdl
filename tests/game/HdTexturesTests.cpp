#include <doctest.h>

#include "core/Cancellation.h"
#include "core/DotNet.h"
#include "core/Files.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "formats/Dti.h"
#include "formats/MdkData.h"
#include "formats/Palette.h"
#include "formats/Png.h"
#include "formats/SpriteAnimation.h"
#include "formats/Texture.h"
#include "game/audio/SoundBank.h"
#include "game/hd_textures/HdGenerator.h"
#include "game/hd_textures/HdKey.h"
#include "game/hd_textures/HdManifest.h"
#include "game/hd_textures/TextureExport.h"
#include "game/hd_textures/UpscaleImages.h"
#include "game/level/LevelData.h"
#include "game/mods/ModCatalog.h"
#include "game/mods/ModFiles.h"
#include "game/mods/ModImages.h"
#include "shared/GameData.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

using namespace Mdk;
using namespace Mdk::Formats;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::HdTextures;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Mods;
using Mdk::Tests::GameData;

// HD textures without the upscaler: the export (keys, colours, alpha, padding), the finishing
// of the upscaler's images, the cache's manifest and the loader's choice.
namespace
{
    constexpr int Rgba = 4;
    constexpr byte Opaque = 255;
    constexpr int Scale = 2;

    // A fresh temp folder per test, deleted after (the C# IDisposable).
    struct TempFolder
    {
        std::string Path;

        TempFolder()
        {
            std::random_device random;
            auto name = "mdk-hd-" + std::to_string(random()) + std::to_string(random());
            Path = (std::filesystem::temp_directory_path() / name).string();
            std::filesystem::create_directories(Path);
        }

        ~TempFolder() { Directory::Delete(Path, true); }
    };

    // Index 1 red, 2 blue, 3 green, the rest grey.
    std::shared_ptr<Palette> Colours()
    {
        auto palette = std::make_shared<Palette>();
        palette->Rgba.fill(128);
        const byte red[] = {200, 0, 0, Opaque};
        const byte blue[] = {0, 0, 100, Opaque};
        const byte green[] = {0, 150, 0, Opaque};
        std::copy(std::begin(red), std::end(red), palette->Rgba.begin() + 1 * Rgba);
        std::copy(std::begin(blue), std::end(blue), palette->Rgba.begin() + 2 * Rgba);
        std::copy(std::begin(green), std::end(green), palette->Rgba.begin() + 3 * Rgba);
        return palette;
    }

    std::shared_ptr<Texture> TextureOf(const std::string& name, int width, int height, std::vector<byte> indices, int frames = 1)
    {
        auto texture = std::make_shared<Texture>();
        texture->Name = name;
        texture->Width = width;
        texture->Height = height;
        texture->FrameCount = frames;
        texture->Indices = std::move(indices);
        return texture;
    }

    std::shared_ptr<Palette> With(const Palette& palette, int index, byte r)
    {
        auto copy = std::make_shared<Palette>();
        copy->Rgba = palette.Rgba;
        copy->Rgba[index * Rgba] = r;
        return copy;
    }

    std::vector<byte> Slice(const std::vector<byte>& bytes, size_t start, size_t length)
    {
        return std::vector<byte>(bytes.begin() + start, bytes.begin() + start + length);
    }

    using Bytes = std::vector<byte>;
}

// --- Keys --------------------------------------------------------------------------------

TEST_CASE("HdTexturesTests.KeyFollowsTheColoursUsed")
{
    auto texture = TextureOf("WALL", 2, 1, {1, 2});
    auto palette = Colours();
    auto key = HdKey::Of(*texture, *palette);

    CHECK(HdKey::Of(*TextureOf("WALL", 2, 1, {1, 2}), *Colours()) == key);
    // A colour the texture doesn't use (3), or index 0 (always clear), changes nothing.
    CHECK(HdKey::Of(*texture, *With(*palette, 3, 7)) == key);
    CHECK(HdKey::Of(*texture, *With(*palette, 0, 7)) == key);
    // A colour it uses, or its indices, make another image.
    CHECK(HdKey::Of(*texture, *With(*palette, 2, 7)) != key);
    CHECK(HdKey::Of(*TextureOf("WALL", 2, 1, {2, 1}), *palette) != key);
    CHECK(HdKey::Of(*TextureOf("WALL", 1, 2, {1, 2}), *palette) != key);
}

// --- Export ------------------------------------------------------------------------------

TEST_CASE("HdTexturesTests.FramesTakeThePaletteIndexZeroClear")
{
    auto texture = TextureOf("ANIM", 2, 1, {1, 0, 2, 3}, 2);

    auto first = TextureExport::Frame(*texture, *Colours(), 0);
    auto second = TextureExport::Frame(*texture, *Colours(), 1);

    CHECK(first == Bytes{200, 0, 0, Opaque, 0, 0, 0, 0});
    CHECK(second == Bytes{0, 0, 100, Opaque, 0, 150, 0, Opaque});
}

// The upscaler's input: clear texels take their neighbours' colour (no black fringe on
// cut-outs), and the image wraps around by the margin (tiling textures meet without seams).
TEST_CASE("HdTexturesTests.InputBleedsAndWraps")
{
    auto rgba = TextureExport::Frame(*TextureOf("CUT", 3, 1, {1, 0, 2}), *Colours(), 0);
    constexpr int margin = 2;

    auto padded = UpscaleImages::Input(rgba, 3, 1, margin);

    auto [width, height] = UpscaleImages::Padded(3, 1, margin);
    CHECK(std::make_tuple(width, height) == std::make_tuple(7, 5));
    CHECK(static_cast<int>(padded.size()) == width * height * Rgba);
    // Row 2 is the image's: (wrapped) 0 2 | 1 [between] 2 | 1 0 ...
    auto row = Slice(padded, 2 * width * Rgba, width * Rgba);
    CHECK(Slice(row, 2 * Rgba, Rgba) == Bytes{200, 0, 0, Opaque});
    CHECK(Slice(row, 4 * Rgba, Rgba) == Bytes{0, 0, 100, Opaque});
    CHECK(Slice(row, 1 * Rgba, Rgba) == Bytes{0, 0, 100, Opaque});
    // The clear texel: the mean of red and blue, opaque.
    CHECK(Slice(row, 3 * Rgba, Rgba) == Bytes{100, 0, 50, Opaque});
}

TEST_CASE("HdTexturesTests.SkySourceIsOpaqueAndDoesNotWrapVertically")
{
    Dti dti;
    dti.Sky = TextureOf("SKY", 2, 2, {0, 1, 2, 3});
    dti.Palette = Colours();

    auto sky = TextureExport::Sky(3, dti);
    auto source = TextureExport::Frame(*sky.Texture, *sky.Palette, 0, sky.ZeroOpaque);
    auto padded = UpscaleImages::Input(source, 2, 2, 1, sky.WrapY);

    CHECK_FALSE(sky.WrapY);
    CHECK(sky.ZeroOpaque);
    for (int i = 0; i < 4; i++)
    {
        CAPTURE(i);
        CHECK(source[i * Rgba + 3] == Opaque);
    }

    CHECK(Slice(source, 0, Rgba) == Bytes{128, 128, 128, Opaque});
    auto [width, height] = UpscaleImages::Padded(2, 2, 1);
    Bytes top;
    Bytes bottom;
    for (int x = 0; x < width; x++)
    {
        auto topPixel = Slice(source, static_cast<size_t>((x + 1) % 2) * Rgba, Rgba);
        auto bottomPixel = Slice(source, static_cast<size_t>(2 + (x + 1) % 2) * Rgba, Rgba);
        top.insert(top.end(), topPixel.begin(), topPixel.end());
        bottom.insert(bottom.end(), bottomPixel.begin(), bottomPixel.end());
    }

    CHECK(Slice(padded, 0, static_cast<size_t>(width * Rgba)) == top);
    CHECK(Slice(padded, static_cast<size_t>((height - 1) * width * Rgba), static_cast<size_t>(width * Rgba)) == bottom);
}

// The upscaler's output: the margin cut, brought to the cache's scale, the alpha the source's
// (hard edges: 0 or 255).
TEST_CASE("HdTexturesTests.OutputCropsScalesAndKeysAlpha")
{
    auto source = TextureExport::Frame(*TextureOf("CUT", 2, 2, {1, 0, 1, 1}), *Colours(), 0);
    constexpr int margin = 1;
    constexpr int factor = 4;
    auto [paddedWidth, paddedHeight] = UpscaleImages::Padded(2, 2, margin);
    // A uniform grey output from a 4x upscaler.
    Bytes upscaled(paddedWidth * factor * paddedHeight * factor * Rgba, 90);

    auto hd = UpscaleImages::Output(upscaled, factor, source, 2, 2, margin, Scale);

    CHECK(hd.size() == 4 * 4 * Rgba);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;
        bool clear = x >= 2 && y < 2;
        CAPTURE(i);
        CHECK(hd[i * Rgba + 3] == (clear ? 0 : Opaque));
        CHECK(hd[i * Rgba] == 90);
    }
}

// Sprites (Kurt) keep their cover soft: the shader's half-cover cut then follows the source's
// outline as smoothly as the original's, not in steps.
TEST_CASE("HdTexturesTests.SpritesKeepSoftEdges")
{
    auto source = TextureExport::Frame(*TextureOf("K_RUN_0", 2, 1, {1, 0}), *Colours(), 0);
    constexpr int margin = 1;
    constexpr int factor = 4;
    auto [paddedWidth, paddedHeight] = UpscaleImages::Padded(2, 1, margin);
    Bytes upscaled(paddedWidth * factor * paddedHeight * factor * Rgba);

    auto hard = UpscaleImages::Output(upscaled, factor, source, 2, 1, margin, Scale);
    auto soft = UpscaleImages::Output(upscaled, factor, source, 2, 1, margin, Scale, HdAlpha::Soft);

    // Row 0: x = 0..3 at source x 0 (clamped), 0.25, 0.75, 1: cover 1, 0.75, 0.25, 0.
    Bytes hardCover;
    Bytes softCover;
    for (int x = 0; x < 4; x++)
    {
        hardCover.push_back(hard[x * Rgba + 3]);
        softCover.push_back(soft[x * Rgba + 3]);
    }

    CHECK(hardCover == Bytes{Opaque, Opaque, 0, 0});
    CHECK(softCover == Bytes{Opaque, 191, 64, 0});
}

// --- Cache -------------------------------------------------------------------------------

TEST_CASE("HdTexturesTests.ManifestRoundTrips")
{
    TempFolder folder;
    HdManifest manifest("realesrgan-x4plus", Scale);
    manifest.Entries["abc"] = HdManifest::Entry{"abc", 7, "B_WALL", 64, 32, 2, "LEVEL7/B_WALL_abc.png"};
    manifest.Save(folder.Path);

    auto loaded = HdManifest::Load(folder.Path);

    REQUIRE(loaded != nullptr);
    CHECK(loaded->Model() == "realesrgan-x4plus");
    CHECK(loaded->Scale() == Scale);
    CHECK(loaded->Entries["abc"] == manifest.Entries["abc"]);
}

// Older builds' textures-hd/ moves into the HD textures mod, where the loader finds each
// image by its key (the texture through that palette).
TEST_CASE("HdTexturesTests.OldCacheBecomesTheMod")
{
    TempFolder folder;
    auto texture = TextureOf("WALL", 2, 2, {1, 2, 3, 1});
    auto key = HdKey::Of(*texture, *Colours());
    auto old = Path::Combine(folder.Path, HdGenerator::OldFolder);
    Directory::CreateDirectory(Path::Combine(old, "LEVEL3"));
    Bytes rgba(4 * 4 * Rgba, 77);
    File::WriteAllBytes(Path::Combine(old, "LEVEL3", "WALL_" + key + ".png"), Png::Encode(4, 4, rgba, Png::Channels::Rgba));
    HdManifest manifest("model", Scale);
    manifest.Entries[key] = HdManifest::Entry{key, 3, "WALL", 2, 2, 1, "LEVEL3/WALL_" + key + ".png"};
    manifest.Save(old);

    CHECK(HdGenerator::Migrate(folder.Path) == 1);

    auto mod = HdGenerator::FolderIn(folder.Path);
    CHECK_FALSE(Directory::Exists(old));
    CHECK(File::Exists(Path::Combine(mod, "textures", "LEVEL3", "WALL@" + key + ".png")));
    auto modManifest = HdManifest::Load(mod);
    REQUIRE(modManifest != nullptr);
    CHECK(modManifest->Entries[key].File == "textures/LEVEL3/WALL@" + key + ".png");
    auto catalog = ModCatalog::Scan(ModCatalog::FolderIn(folder.Path));
    REQUIRE(catalog->Mods.size() == 1);
    CHECK(catalog->Mods[0].Priority == -100);
    auto image = ModImages::Open({ModFiles::Folder(mod)}, 3)->Texture("WALL", *texture, *Colours());
    REQUIRE(image.has_value());
    CHECK(std::make_tuple(image->Width, image->Height, image->Frames) == std::make_tuple(4, 4, 1));

    // Once: nothing left to move.
    CHECK(HdGenerator::Migrate(folder.Path) == 0);
}

// Making the HD textures (menu or --upscale-textures) first moves an older build's cache into
// the mod, so its images aren't made again.
TEST_CASE("HdTexturesTests.RunMovesTheOldCacheFirst")
{
    TempFolder folder;
    auto old = Path::Combine(folder.Path, HdGenerator::OldFolder);
    Directory::CreateDirectory(old);
    HdManifest("model", Scale).Save(old);
    CancellationTokenSource cancel;
    cancel.Cancel();

    // Cancelled at the upscaler's download, before any game data is read.
    HdProgress progress;
    CHECK_THROWS_AS(HdGenerator::Run(nullptr, folder.Path, HdOptions::Default(), progress, cancel.Token()),
        OperationCanceledException);

    CHECK_FALSE(Directory::Exists(old));
    CHECK(HdManifest::Load(HdGenerator::FolderIn(folder.Path)) != nullptr);
}

// --- The game's textures --------------------------------------------------------------------

// Level 3's arenas and models: each texture once per distinct look, all in palettes of the
// level's arenas.
DATA_TEST_CASE("HdTexturesTests.LevelExportsItsTextures")
{
    auto& data = *GameData::Data();
    LevelData level(data, 3);
    auto cmi = Cmi::Load(data.PathOf("TRAVERSE/LEVEL3/LEVEL3.CMI"));

    auto sources = TextureExport::Collect(level, *cmi);

    CAPTURE(sources.size());
    CHECK(sources.size() > 50);
    std::unordered_set<std::string> keys;
    for (const auto& source : sources)
    {
        keys.insert(source.Key);
        CHECK(source.Key == HdKey::Of(*source.Texture, *source.Palette));
    }

    CHECK(keys.size() == sources.size());
}

// Kurt's frames: TRAVSPRT.BNI's he's drawn with, through the level's palette, and the level's
// own (LEVEL4's snowboard).
DATA_TEST_CASE("HdTexturesTests.LevelExportsKurt")
{
    auto& data = *GameData::Data();
    LevelData level(data, 4);
    auto sprites = Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
    auto run = sprites->GetAnimation("K_RUN");
    auto bank = SoundBank::ForLevel(data, 4);

    auto sources = TextureExport::Kurt(level, *sprites, [&](const std::string& name) { return bank->Animation(name); });

    std::unordered_set<std::string> keys;
    bool surf = false;
    bool chute = false;
    for (const auto& source : sources)
    {
        keys.insert(source.Key);
        surf |= source.Name == "K_SURF_0";
        chute |= source.Name == "K_CHUTEC_0";
        CHECK(source.Palette == level.Dti->Palette);
        CHECK(source.Alpha == HdAlpha::Soft);
    }

    CHECK(keys.contains(HdKey::Of(*run->GetFrame(0).Image, *level.Dti->Palette)));
    CHECK(surf);
    CHECK(chute);
    CHECK(sources.size() == keys.size());
}
