#include <memory>

#include <doctest.h>

#include "formats/Arena.h"
#include "game/audio/SoundBank.h"
#include "game/audio/SoundMixer.h"
#include "game/level/LevelData.h"
#include "shared/GameData.h"

using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Level;
using Mdk::Tests::GameData;

// Some sounds are kept by the arenas of LEVELnO.MTO, not by an SNI archive: LEVEL7's
// barricade soldiers (DANT_6) sing SING and taunt with TAUNT1 from there. The level's bank finds them.

DATA_TEST_CASE("ArenaSoundsTests.TheBarricadeSoldiersSongIsInTheLevelsBank")
{
    auto& data = *GameData::Data();
    LevelData level(data, 7);
    std::shared_ptr<Mdk::Formats::Arena> dant6;
    for (const auto& a : level.Arenas)
    {
        if (a->Name == "DANT_6")
        {
            dant6 = a;
            break;
        }
    }

    REQUIRE(dant6 != nullptr);
    CHECK(dant6->Sounds.ContainsKey("SING"));

    auto bank = SoundBank::ForLevel(data, 7, level.Arenas);
    CHECK(bank->Get("SING").has_value());
    CHECK(bank->Get("TAUNT1").has_value());
    REQUIRE(bank->Get("SING").has_value());
    CHECK(SoundMixer::FullVolume == bank->Get("SING")->Volume);

    // The archives' sounds are as they were; without the arenas the song is missing.
    CHECK(bank->Get("XG_SPOT1").has_value());
    CHECK_FALSE(SoundBank::ForLevel(data, 7)->Get("SING").has_value());
}
