#include <doctest.h>

#include <vector>

#include "formats/Bni.h"
#include "formats/SniperScreen.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Formats;
using Mdk::Tests::GameData;

// Sniper mode's screen in TRAVSPRT.BNI (godot-mdk docs/gameplay.md "Sniper mode").

TEST_CASE("SniperScreenTests.MaskWordsSkipAndCopy")
{
    // 1 x 4 literals, skip 3, 2 short literals, end.
    std::vector<byte> bytes = {0x01, 0x00, 1, 2, 3, 4, 0x03, 0x80, 0x02, 0xFF, 5, 6, 0x00, 0xFF};
    auto mask = SniperScreen::DecodeMask(bytes, 0);
    CHECK(SniperScreen::ViewWidth * SniperScreen::ViewHeight == mask.size());
    CHECK(std::vector<byte>{1, 2, 3, 4, 0, 0, 0, 5, 6, 0} == std::vector<byte>(mask.begin(), mask.begin() + 10));
}

DATA_TEST_CASE("SniperScreenTests.ScreenDecodesWithHoles")
{
    auto sprites = Bni::Load(GameData::Data()->PathOf("TRAVERSE/TRAVSPRT.BNI"));
    auto frame = SniperScreen::Frame(*sprites);
    CHECK(SniperScreen::ScreenWidth * SniperScreen::ScreenHeight == frame->Indices.size());

    // The scope's centre (299, 219 of the view) is a hole of the mask; its corner isn't.
    auto mask = SniperScreen::Mask(*sprites);
    CHECK(0 == mask->Indices[219 * SniperScreen::ViewWidth + 299]);
    CHECK(0 != mask->Indices[SniperScreen::ViewWidth * (SniperScreen::ViewHeight - 1)]);
}
