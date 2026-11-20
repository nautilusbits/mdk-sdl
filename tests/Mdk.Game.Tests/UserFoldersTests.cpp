#include <doctest.h>

#include "Mdk.Game/Flow/UserFolders.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace Mdk::Game::Flow;

namespace fs = std::filesystem;

// Settings and saves next to the executable, or the user's data folder when it's protected (#9).
namespace
{
    // Path.GetRandomFileName: a fresh name in the temp folder.
    fs::path RandomTemp()
    {
        std::random_device seed;
        return fs::temp_directory_path() / ("mdk-" + std::to_string(seed()) + "-" + std::to_string(seed()));
    }
}

TEST_CASE("UserFoldersTests.WritableFolderBesideTheProgramIsUsed")
{
    auto beside = RandomTemp();
    fs::create_directory(beside);
    auto personal = RandomTemp();

    CHECK(UserFolders::Choose(beside.string(), personal.string()) == beside.string());
    CHECK_FALSE(fs::exists(personal));
    CHECK(fs::is_empty(beside));
}

TEST_CASE("UserFoldersTests.ProtectedFolderFallsBackToThePersonalOne")
{
    // A file where the folder should be: nothing can be written "in" it.
    auto beside = RandomTemp();
    std::ofstream{beside};
    auto personal = RandomTemp();

    CHECK(UserFolders::Choose(beside.string(), personal.string()) == personal.string());
    CHECK(fs::is_directory(personal));
}

TEST_CASE("UserFoldersTests.PersonalFolderIsNamedAfterTheProgram")
{
    CHECK(fs::path(UserFolders::Personal()).filename() == "mdk-sdl");
}
