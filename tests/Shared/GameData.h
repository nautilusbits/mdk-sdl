#pragma once

// Whether the original game's files (and the 1996 demo's) are installed. They are never in the
// repository (nor on CI), so tests reading them are skipped without them:
//   DATA_TEST_CASE("Formats.EveryLevelParses") { auto data = GameData::Data(); ... }
//   BETA_TEST_CASE("BetaDemo.TheDemoLevelsLoad") { auto& demo = BetaData::Demo(); ... }

#include <doctest.h>

#include <memory>

#include "Mdk.Formats/BetaDemo.h"
#include "Mdk.Formats/MdkData.h"

namespace Mdk::Tests
{
    struct GameData
    {
        static constexpr const char* Missing = "MDK data not found: install MDK or set MDK_DATA_DIR";

        // The installation (MdkData::Find), null when missing.
        static const std::shared_ptr<Formats::MdkData>& Data();

        static bool Present() { return Data() != nullptr; }
    };

    // Whether the 1996 beta demo is installed (godot-mdk docs/beta96.md; MDK_BETA_DIR).
    struct BetaData
    {
        static constexpr const char* Missing = "The 1996 beta demo not found: set MDK_BETA_DIR";

        static const std::shared_ptr<Formats::BetaDemo>& Demo();
    };
}

// [DataFact] / [DataTheory]: a test case reading the game's files, skipped when they are missing.
#define DATA_TEST_CASE(name) TEST_CASE(name * doctest::skip(!::Mdk::Tests::GameData::Present()) * doctest::description(::Mdk::Tests::GameData::Missing))

// [BetaFact]: a test case reading the 1996 demo's files, skipped when it's missing.
#define BETA_TEST_CASE(name) TEST_CASE(name * doctest::skip(::Mdk::Tests::BetaData::Demo() == nullptr) * doctest::description(::Mdk::Tests::BetaData::Missing))
