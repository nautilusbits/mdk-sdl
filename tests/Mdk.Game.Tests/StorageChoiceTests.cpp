#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <doctest.h>

#include "Mdk.Core/Files.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Game/Flow/SaveGames.h"
#include "Mdk.Game/Flow/Settings.h"
#include "Mdk.Game/Flow/StorageChoice.h"
#include "Mdk.Game/Flow/TreePaths.h"
#include "Shared/TempDir.h"

using namespace Mdk;
using namespace Mdk::Game::Flow;
using Mdk::Formats::MdkData;

// Android's game files: a picked folder's path, the folder chosen to play from in place
// (remembered, checked each start) or the copy in the app.
namespace
{
    const std::string Primary = "/storage/emulated/0";

    struct Fixture
    {
        Mdk::Tests::TempDir Temp{"mdk-storage-"};

        std::string App() const { return Path::Combine(Temp.Path(), "app"); }

        // An installation under the temp folder; returns its folder.
        std::string Installation(const std::string& under) const
        {
            auto folder = Path::Combine(Temp.Path(), under);
            Directory::CreateDirectory(Path::Combine(folder, "TRAVERSE"));
            File::WriteAllText(Path::Combine(folder, "TRAVERSE", "travsprt.bni"), "");
            return folder;
        }
    };

    SaveGame Snapshot(int level) { return {SaveKind::Snapshot, level, SaveGame::FullHealth, 0, false, ""}; }
}

TEST_CASE("StorageChoiceTests.DeviceStorageFoldersHavePaths")
{
    const std::pair<std::string, std::string> cases[] = {
        {"primary:MDK", Primary + "/MDK"},
        {"primary:Games/MDK", Primary + "/Games/MDK"},
        {"primary:", Primary},
        {"home:MDK", Primary + "/Documents/MDK"},
        {"1234-ABCD:MDK", "/storage/1234-ABCD/MDK"},
    };
    for (const auto& [document, path] : cases)
    {
        CAPTURE(document);
        CHECK(std::optional<std::string>(path) == TreePaths::Resolve(TreePaths::ExternalStorage, document, Primary));
    }
}

TEST_CASE("StorageChoiceTests.DownloadsFoldersHavePathsWhenRaw")
{
    CHECK(std::optional<std::string>(Primary + "/Download/MDK") == TreePaths::Resolve(TreePaths::Downloads, "raw:" + Primary + "/Download/MDK", Primary));
}

TEST_CASE("StorageChoiceTests.OtherFoldersHaveNoPath")
{
    const std::pair<std::string, std::string> cases[] = {
        {TreePaths::Downloads, "msf:42"},
        {"com.google.android.apps.docs.storage", "acc=1;doc=2"},
        {TreePaths::ExternalStorage, "MDK"},
        // Only folders on the storage volumes, never climbing out of them.
        {TreePaths::Downloads, "raw:/data/data/io.github.nemo22.mdk"},
        {TreePaths::Downloads, "raw:/storage/emulated/0/../../../data"},
        {TreePaths::ExternalStorage, "primary:MDK/../../.."},
        {TreePaths::ExternalStorage, "../data:MDK"},
    };
    for (const auto& [authority, document] : cases)
    {
        CAPTURE(document);
        CHECK(TreePaths::Resolve(authority, document, Primary) == std::nullopt);
    }
}

TEST_CASE("StorageChoiceTests.ThePrimaryStorageIsAboveTheAppsFolder")
{
    CHECK(std::optional<std::string>(Primary) == TreePaths::PrimaryRoot(Primary + "/Android/data/io.github.nemo22.mdk/files"));
    CHECK(TreePaths::PrimaryRoot("/data/user/0/io.github.nemo22.mdk/files") == std::nullopt);
}

TEST_CASE("StorageChoiceTests.WithoutAChoiceAccessIsAsked")
{
    Fixture f;
    CHECK(StorageKind::AskAccess == StorageChoice(f.App()).Plan(FolderAccess::Askable, nullptr).Kind);
}

TEST_CASE("StorageChoiceTests.AStoreBuildCopies")
{
    Fixture f;
    CHECK(StorageKind::CopyFlow == StorageChoice(f.App()).Plan(FolderAccess::Unavailable, nullptr).Kind);
}

TEST_CASE("StorageChoiceTests.AnEarlierCopyIsKept")
{
    Fixture f;
    auto copy = MdkData::At(f.Installation(Path::Combine("app", "mdk")));

    auto plan = StorageChoice(f.App()).Plan(FolderAccess::Granted, copy);

    CHECK(StorageKind::Copied == plan.Kind);
    CHECK(copy == plan.Data);
}

TEST_CASE("StorageChoiceTests.TheChosenFolderIsRememberedAndPlayedInPlace")
{
    Fixture f;
    auto folder = f.Installation("MDK");
    StorageChoice(f.App()).Choose(folder);

    StorageChoice choice(f.App());
    auto plan = choice.Plan(FolderAccess::Granted, MdkData::At(f.Installation(Path::Combine("app", "mdk"))));

    CHECK(std::optional<std::string>(folder) == choice.Folder());
    CHECK(StorageKind::InPlace == plan.Kind);
    REQUIRE(plan.Data != nullptr);
    CHECK(folder == plan.Data->Dir());
}

TEST_CASE("StorageChoiceTests.AGoneFolderIsLost")
{
    Fixture f;
    auto folder = f.Installation("MDK");
    StorageChoice choice(f.App());
    choice.Choose(folder);
    Directory::Delete(folder, true);

    CHECK(StorageKind::Lost == choice.Plan(FolderAccess::Granted, nullptr).Kind);
}

TEST_CASE("StorageChoiceTests.ARevokedAccessIsLost")
{
    Fixture f;
    StorageChoice choice(f.App());
    choice.Choose(f.Installation("MDK"));

    CHECK(StorageKind::Lost == choice.Plan(FolderAccess::Askable, nullptr).Kind);
}

TEST_CASE("StorageChoiceTests.AForgottenChoiceAsksAgain")
{
    Fixture f;
    StorageChoice choice(f.App());
    choice.Choose(f.Installation("MDK"));

    choice.Forget();

    CHECK(StorageChoice(f.App()).Folder() == std::nullopt);
    CHECK(StorageKind::AskAccess == choice.Plan(FolderAccess::Granted, nullptr).Kind);
}

TEST_CASE("StorageChoiceTests.TheInstallationIsThePickedFolderOrOneInIt")
{
    Fixture f;
    auto folder = f.Installation(Path::Combine("Games", "MDK"));

    auto direct = MdkData::At(folder);
    REQUIRE(direct != nullptr);
    CHECK(folder == direct->Dir());
    auto inside = MdkData::At(Path::Combine(f.Temp.Path(), "Games"));
    REQUIRE(inside != nullptr);
    CHECK(folder == inside->Dir());
    CHECK(MdkData::At(f.Temp.Path()) == nullptr);
    CHECK(MdkData::At(Path::Combine(f.Temp.Path(), "none")) == nullptr);
}

TEST_CASE("StorageChoiceTests.SettingsAndSavesMoveWithoutOverwriting")
{
    Fixture f;
    auto from = Path::Combine(f.Temp.Path(), "app");
    auto to = f.Installation("MDK");
    auto saves = SaveGames::In(from);
    saves.Write("1", Snapshot(3));
    saves.Write("2", Snapshot(4));
    File::WriteAllText(Settings::PathIn(from), "master_volume=10\n");
    SaveGames::In(to).Write("2", Snapshot(7));

    StorageChoice::CopyUserFiles(from, to);

    CHECK(std::string("master_volume=10\n") == File::ReadAllText(Settings::PathIn(to)));
    auto first = SaveGames::In(to).Read("1");
    REQUIRE(first.has_value());
    CHECK(3 == first->Level);
    auto second = SaveGames::In(to).Read("2");
    REQUIRE(second.has_value());
    CHECK(7 == second->Level);
}
