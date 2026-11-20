#include <doctest.h>

#include <string>
#include <utility>

#include "Mdk.Formats/LocalPaths.h"

using namespace Mdk::Formats;

// mdk_paths.cfg (the Godot port's ConfigFile): the folders of the game and the demo.

namespace
{
    const std::string Config = R"cfg(; my folders
[other]
mdk="C:/Wrong"

[paths]
mdk="C:/GOG Games/MDK"
beta = "C:/Games/MDK (1996-08-06) (beta demo)"
plain=D:/MDK
escaped="C:\\Games\\\"MDK\"")cfg";
}

TEST_CASE("LocalPathsTests.KeysOfThePathsSectionAreRead")
{
    std::pair<std::string, std::string> data[] = {
        {LocalPaths::Game, "C:/GOG Games/MDK"},
        {LocalPaths::Beta, "C:/Games/MDK (1996-08-06) (beta demo)"},
        {"plain", "D:/MDK"},
        {"escaped", "C:\\Games\\\"MDK\""},
        {"missing", ""},
    };
    for (auto& [key, expected] : data)
    {
        CAPTURE(key);
        CHECK(expected == LocalPaths::Parse(Config, key));
    }
}

TEST_CASE("LocalPathsTests.WindowsLineEndingsAreRead")
{
    CHECK("C:/MDK" == LocalPaths::Parse("[paths]\r\nmdk=\"C:/MDK\"\r\n", LocalPaths::Game));
}

TEST_CASE("LocalPathsTests.NoFileNoPath")
{
    CHECK("" == LocalPaths::Parse("", LocalPaths::Game));
}
