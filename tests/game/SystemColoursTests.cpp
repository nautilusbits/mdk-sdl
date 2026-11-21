#include <doctest.h>

#include "formats/Fti.h"
#include "formats/MdkData.h"
#include "game/level/LevelData.h"
#include "shared/GameData.h"

#include <algorithm>

using namespace Mdk;
using namespace Mdk::Formats;
using namespace Mdk::Game::Level;
using namespace Mdk::Tests;

// A level keeps the system colours (SYS_PAL) at palette 0-63: its load copies only the DTI's
// 64-255 (0x41ba68: base palette + 0xc0 to 0x5736a4). LEVEL8's DTI has magenta and purple at
// 10-12, which its aliens, shots and the grenade icon use.
namespace
{
    constexpr int Level = 8;
    constexpr int Channels = 3;
    constexpr int Changed[] = {10, 11, 12};
}

DATA_TEST_CASE("SystemColoursTests.LevelKeepsSystemColours")
{
    auto& data = *GameData::Data();
    auto system = Fti::Load(data.PathOf("MISC/MDKFONT.FTI"))->GetBytes("SYS_PAL");
    LevelData level(data, Level);

    for (int index : Changed)
    {
        CAPTURE(index);
        const auto* rgba = level.Dti->Palette->Rgba.data() + index * 4;
        CHECK(std::equal(rgba, rgba + Channels, system.begin() + index * Channels));
    }
}
