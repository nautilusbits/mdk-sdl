#include <doctest.h>

#include <algorithm>
#include <format>

#include "formats/Arena.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "formats/Dti.h"
#include "formats/Font.h"
#include "formats/Fti.h"
#include "formats/Mto.h"
#include "formats/Sni.h"
#include "formats/TextureArchive.h"
#include "formats/Wav.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Formats;
using Mdk::Tests::GameData;

// Parses the real game data (MDK must be installed above this folder or in MDK_DATA_DIR).

namespace
{
    // The game's levels are numbered 3 to 8.
    constexpr int FirstLevel = 3;
    constexpr int LastLevel = 8;
    // Level 7's starting arena.
    constexpr const char* FirstArena = "DANT_1";

    std::string LevelPath(int level, const std::string& suffix) { return GameData::Data()->PathOf(std::format("TRAVERSE/LEVEL{}/LEVEL{}{}", level, level, suffix)); }

    bool Contains(const std::vector<byte>& bytes, byte value) { return std::find(bytes.begin(), bytes.end(), value) != bytes.end(); }
}

DATA_TEST_CASE("FormatTests.EveryLevelParses")
{
    for (auto level = FirstLevel; level <= LastLevel; level++)
    {
        auto dti = Dti::Load(LevelPath(level, ".DTI"));
        auto mto = Mto::Load(LevelPath(level, "O.MTO"));
        CHECK(!dti->Arenas.empty());
        for (auto& name : mto->ArenaNames())
        {
            auto arena = mto->GetArena(name);
            CHECK(!arena->Vertices.empty());
            CHECK(arena->TriangleCount() * 3 == arena->TriangleIndices.size());
            auto inRange = std::all_of(arena->TriangleIndices.begin(), arena->TriangleIndices.end(),
                [&](int i) { return i >= 0 && i <= static_cast<int>(arena->Vertices.size()) - 1; });
            CHECK(inRange);
        }
    }
}

DATA_TEST_CASE("FormatTests.Level7StartsInDant1")
{
    auto dti = Dti::Load(LevelPath(7, ".DTI"));
    auto mto = Mto::Load(LevelPath(7, "O.MTO"));
    CHECK(FirstArena == dti->Arenas[dti->StartArena].Name);
    CHECK(mto->Has(FirstArena));
    CHECK(Arena::PaletteColors * 3 == mto->GetArena(FirstArena)->PaletteRgb.size());
}

// LEVEL8's walls paint black with index 0: drawn opaque, as the original uploads every
// texture without kind bit 0 (0x474c9c). GUNT_2's beam showed the sky through them.
DATA_TEST_CASE("FormatTests.OnlyKeyedTexturesSeeThrough")
{
    auto arena = Mto::Load(LevelPath(8, "O.MTO"))->GetArena("GUNT_2");
    CHECK(!Contains(arena->Textures->Textures.At("I2_WALL1")->Indices, 0));

    auto level = TextureArchive::Load(LevelPath(8, "S.MTI"), TextureArchive::Zero::ByKind);
    CHECK(!Contains(level->Textures.At("BULLET")->Indices, 0));
    CHECK(Contains(level->Textures.At("EXPLODE")->Indices, 0));
}

DATA_TEST_CASE("FormatTests.CorridorsParseFromSni")
{
    auto sni = Sni::Load(LevelPath(7, "O.SNI"));
    std::vector<std::pair<std::string, Sni::Entry>> corridors;
    std::copy_if(sni->Entries.begin(), sni->Entries.end(), std::back_inserter(corridors), [&](const auto& e) { return !sni->IsSound(e.second); });
    CHECK(!corridors.empty());
    for (auto& [name, entry] : corridors)
    {
        auto corridor = Arena::ParseWorld(name, *sni->Bytes(), entry.Offset);
        CHECK(!corridor->Materials.empty());
    }
}

DATA_TEST_CASE("FormatTests.ModelAnimationsBake")
{
    auto mto = Mto::Load(LevelPath(7, "O.MTO"));
    auto arena = mto->GetArena(FirstArena);
    CHECK(arena->Models.Count() > 0);
    for (auto& [animationName, animation] : arena->Animations)
    {
        std::shared_ptr<Model> model;
        for (auto& [modelName, m] : arena->Models)
        {
            if (m->PartList.size() > 1)
            {
                model = m;
                break;
            }
        }

        if (model == nullptr)
        {
            return;
        }

        CHECK(animation->FrameCount == animation->Bake(*model).size());
    }
}

DATA_TEST_CASE("FormatTests.KurtRunFramesDecode")
{
    auto bni = Bni::Load(GameData::Data()->PathOf("TRAVERSE/TRAVSPRT.BNI"));
    auto run = bni->GetAnimation("K_RUN");
    CHECK(run->FrameCount() > 1);
    for (auto i = 0; i < run->FrameCount(); i++)
    {
        auto frame = run->GetFrame(i);
        auto& indices = frame.Image->Indices;
        CHECK(std::any_of(indices.begin(), indices.end(), [](byte index) { return index != 0; }));
    }
}

DATA_TEST_CASE("FormatTests.ScriptsAndSoundsParse")
{
    for (auto level = FirstLevel; level <= LastLevel; level++)
    {
        auto cmi = Cmi::Load(LevelPath(level, ".CMI"));
        CHECK(cmi->ArenaScripts.Count() > 0);
        CHECK(cmi->ArenaMusic.Count() > 0);

        auto sni = Sni::Load(LevelPath(level, "S.SNI"));
        for (auto& [name, entry] : sni->Entries)
        {
            if (!sni->IsSound(entry))
            {
                continue;
            }

            auto wav = Wav::Parse(sni->GetBytes(entry));
            CAPTURE(name);
            CHECK((wav && !wav->Data.empty()));
        }
    }
}

DATA_TEST_CASE("FormatTests.FontsParse")
{
    auto fti = Fti::Load(GameData::Data()->PathOf("MISC/MDKFONT.FTI"));
    auto big = Font::Parse(fti->GetBytes("FONTBIG"), 6);
    std::string text = "MDK";
    CHECK(big->Width(std::vector<byte>(text.begin(), text.end())) > big->SpaceWidth() * 3);
}

DATA_TEST_CASE("FormatTests.FontArchiveHasMenuTexts")
{
    auto fti = Fti::Load(GameData::Data()->PathOf("MISC/MDKFONT.FTI"));
    CHECK(fti->Has("FONTBIG"));
    CHECK(!fti->GetText("OPT0").empty());
}
