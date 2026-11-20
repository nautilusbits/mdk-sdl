#include <doctest.h>

#include <algorithm>
#include <format>
#include <set>
#include <string>
#include <vector>

#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Fall3d.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/TextureArchive.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Formats;
using Mdk::Tests::GameData;

// The fall's files (FALL3D/; godot-mdk docs/formats.md, "Fall files").

namespace
{
    constexpr int Falls = 5;
    constexpr int HazeFrames = 16;
    constexpr int MaxHazeOffset = 8;

    std::shared_ptr<Bni> FallBni() { return Bni::Load(GameData::Data()->PathOf("FALL3D/FALL3D.BNI")); }
}

DATA_TEST_CASE("FallFormatTests.PickupsOfEachFall")
{
    auto bni = FallBni();
    CHECK(std::vector<std::string>{"SW_HOME", "SW_GATT", "SW_HOME"} == Fall3d::Pickups(*bni, "FALLPU_1"));
    for (auto n = 2; n <= Falls; n++)
    {
        CHECK(std::vector<std::string>{"SW_GATT", "SW_HBOMB", "SW_SGREN", "SW_HOME"} == Fall3d::Pickups(*bni, std::format("FALLPU_{}", n)));
    }
}

// A clear middle and blend offsets 1-8 around it.
DATA_TEST_CASE("FallFormatTests.HazeFrames600By180")
{
    auto bni = FallBni();
    for (auto i = 0; i < HazeFrames; i++)
    {
        auto haze = Fall3d::Haze(*bni, std::format("ZOOM{:04}", i));
        CHECK(Fall3d::HazeWidth * Fall3d::HazeRows == haze.size());
        CHECK(std::all_of(haze.begin(), haze.end(), [](byte b) { return b <= MaxHazeOffset; }));
        CHECK(0 == haze[Fall3d::HazeRows / 2 * Fall3d::HazeWidth + Fall3d::HazeWidth / 2]);
        CHECK(std::any_of(haze.begin(), haze.end(), [](byte b) { return b > 0; }));
    }
}

// Every model's textures and colours are in each fall's MTI (or PEN_n colours).
DATA_TEST_CASE("FallFormatTests.ModelMaterialsResolve")
{
    auto bni = FallBni();
    std::vector<std::string> named = {"KURT", "MISSILE", "CHUTE", "BONES", "SW_DUMMY", "SW_H150", "SW_THUMP", "SW_TWIST", "SW_INTER"};
    std::vector<std::string> single = {"EXPLODE", "SW_GATT", "SW_HBOMB", "SW_HOME", "SW_SGREN"};
    std::vector<std::string> all = named;
    all.insert(all.end(), single.begin(), single.end());
    for (auto n = 1; n <= Falls; n++)
    {
        auto mti = TextureArchive::Load(GameData::Data()->PathOf(std::format("FALL3D/FALL3D_{}.MTI", n)));
        CHECK(1024 == mti->Textures.At(std::format("LEVEL{}", n))->Width);
        CHECK(64 == mti->Textures.At(std::format("POD{}", n))->Width);
        for (auto& name : all)
        {
            if (!bni->Has(name))
            {
                continue;
            }

            auto parts = std::find(named.begin(), named.end(), name) != named.end() ? Model::Parts::Named : Model::Parts::Single;
            auto model = Model::Parse(name, *bni->Bytes(), bni->Entries().At(name).Offset, Model::Header::NoFlags, parts);
            std::set<int> used;
            for (auto& part : model->PartList)
            {
                for (auto m : part->TriangleMaterials)
                {
                    if (m >= 0)
                    {
                        used.insert(m);
                    }
                }
            }

            for (auto m : used)
            {
                auto& material = model->Materials.at(m);
                CAPTURE(name);
                CAPTURE(material);
                CHECK((mti->Textures.ContainsKey(material) || mti->Colors.ContainsKey(material) || Str::StartsWith(material, "PEN_")));
            }
        }
    }
}
