#include <doctest.h>

#include <algorithm>
#include <format>
#include <set>

#include "Mdk.Formats/Flc.h"
#include "Mdk.Formats/Gif.h"
#include "Mdk.Formats/Mve.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Formats;
using Mdk::Tests::GameData;

// The animations and stills of MISC decode.

namespace
{
    constexpr int SlideCount = 8;

    template <typename T>
    bool AnyNonZero(const T& values) { return std::any_of(values.begin(), values.end(), [](byte v) { return v != 0; }); }
}

DATA_TEST_CASE("MediaTests.FlcFramesDecode")
{
    for (auto path : {"MISC/FLIC/MDKEND.FLC", "MISC/FLIC/Mdk12.flc", "MISC/FLIC/pie.flc", "MISC/FLIC/shiny.flc"})
    {
        CAPTURE(path);
        auto flc = Flc::Load(GameData::Data()->PathOf(path));
        REQUIRE(flc != nullptr);
        while (flc->NextFrame())
        {
        }

        CHECK(flc->FrameCount() == flc->Frame());
        CHECK(AnyNonZero(flc->Indices));
        CHECK(AnyNonZero(flc->Palette));
    }
}

DATA_TEST_CASE("MediaTests.EndMovieDecodes")
{
    auto mve = Mve::Load(GameData::Data()->PathOf("MISC/FLIC/MDKBZK.MVE"));
    REQUIRE(mve != nullptr);
    auto frames = 0;
    auto audio = 0;
    while (mve->NextFrame())
    {
        frames++;
        audio += static_cast<int>(mve->Audio.size());
    }

    CHECK(frames > 1);
    CHECK(audio > 0);
    CHECK(mve->SampleRate() > 0);
    CHECK(AnyNonZero(mve->Indices()));
}

DATA_TEST_CASE("MediaTests.SlidesDecode")
{
    for (auto i = 1; i <= SlideCount; i++)
    {
        auto gif = Gif::Load(GameData::Data()->PathOf(std::format("MISC/MDKS_{:03}.GIF", i)));
        REQUIRE(gif != nullptr);
        CHECK(gif->Width() * gif->Height() == gif->Indices().size());

        // A real picture uses many colours.
        std::set<byte> colours(gif->Indices().begin(), gif->Indices().end());
        CAPTURE(i);
        CHECK(colours.size() > 16);
    }
}
