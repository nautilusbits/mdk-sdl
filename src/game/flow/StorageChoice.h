#pragma once

#include <memory>
#include <optional>
#include <string>

namespace Mdk::Formats
{
    class MdkData;
}

namespace Mdk::Game::Flow
{
    // Whether the game may read and write shared storage by path (Android's All files
    // access; storage permission before Android 11).
    enum class FolderAccess
    {
        Granted,
        // Not granted, but the user can be asked.
        Askable,
        // A store build: never asked, the files are copied.
        Unavailable,
    };

    // What a start does with the game's files (StorageChoice.Plan).
    enum class StorageKind
    {
        // The chosen folder, read in place; it's the user folder too.
        InPlace,
        // The copy in the app's folder (a store build, or an install from before).
        Copied,
        // The chosen folder is gone or no longer readable: say so, forget it, plan again.
        Lost,
        // Nothing yet: ask for access to play in place (declined: copy).
        AskAccess,
        // Nothing yet and no access to ask for: copy.
        CopyFlow,
    };

    // A start's plan; Data the installation for InPlace and Copied.
    struct StoragePlan
    {
        StorageKind Kind{};
        std::shared_ptr<Formats::MdkData> Data;

        bool operator==(const StoragePlan&) const = default;
    };

    // Android: the MDK folder the user chose to play from in place, remembered in a file of
    // the app's own folder and checked each start.
    //   chosen? ─yes─► access and MDK there? ─yes─► InPlace
    //      │                  └no─► Lost (forget, plan again)
    //      └no─► copied before? ─yes─► Copied
    //               └no─► store build? ─yes─► CopyFlow
    //                        └no─► AskAccess
    class StorageChoice
    {
    public:
        explicit StorageChoice(std::string appFolder) : _appFolder(std::move(appFolder)) {}

        // The chosen folder (its installation's), or null.
        std::optional<std::string> Folder() const;

        void Choose(const std::string& folder);

        void Forget();

        // What this start does, given the access and the copy made before (or null).
        StoragePlan Plan(FolderAccess access, const std::shared_ptr<Formats::MdkData>& copy);

        // Settings and saves follow the user to a new folder; files already there are kept.
        static void CopyUserFiles(const std::string& from, const std::string& to);

        // Writes and deletes a probe file: settings, saves and the log go there.
        static bool IsWritable(const std::string& folder);

    private:
        static constexpr const char* FileName = "data-folder.txt";
        static constexpr const char* Probe = ".write-test";

        std::string _appFolder;

        std::string ChoiceFile() const;
    };
}
