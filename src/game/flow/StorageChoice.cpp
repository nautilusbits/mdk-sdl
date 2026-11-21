#include "game/flow/StorageChoice.h"

#include "core/DotNet.h"
#include "core/Files.h"
#include "formats/MdkData.h"
#include "game/flow/SaveGames.h"
#include "game/flow/Settings.h"

namespace Mdk::Game::Flow
{
    std::string StorageChoice::ChoiceFile() const
    {
        return Path::Combine(_appFolder, FileName);
    }

    std::optional<std::string> StorageChoice::Folder() const
    {
        if (!File::Exists(ChoiceFile()))
        {
            return std::nullopt;
        }

        auto folder = Str::Trim(File::ReadAllText(ChoiceFile()));
        return folder.empty() ? std::nullopt : std::optional<std::string>(folder);
    }

    void StorageChoice::Choose(const std::string& folder)
    {
        Directory::CreateDirectory(_appFolder);
        File::WriteAllText(ChoiceFile(), folder);
    }

    void StorageChoice::Forget()
    {
        File::Delete(ChoiceFile());
    }

    StoragePlan StorageChoice::Plan(FolderAccess access, const std::shared_ptr<Formats::MdkData>& copy)
    {
        if (auto folder = Folder())
        {
            auto data = access == FolderAccess::Granted ? Formats::MdkData::At(*folder) : nullptr;
            return data != nullptr && IsWritable(data->Dir()) ? StoragePlan{StorageKind::InPlace, data} : StoragePlan{StorageKind::Lost};
        }

        if (copy != nullptr)
        {
            return {StorageKind::Copied, copy};
        }

        return {access == FolderAccess::Unavailable ? StorageKind::CopyFlow : StorageKind::AskAccess};
    }

    void StorageChoice::CopyUserFiles(const std::string& from, const std::string& to)
    {
        if (File::Exists(Settings::PathIn(from)) && !File::Exists(Settings::PathIn(to)))
        {
            File::Copy(Settings::PathIn(from), Settings::PathIn(to));
        }

        auto saves = SaveGames::In(from).Directory();
        if (!Directory::Exists(saves))
        {
            return;
        }

        Directory::CreateDirectory(SaveGames::In(to).Directory());
        auto target = Path::GetFullPath(SaveGames::In(to).Directory());
        for (const auto& file : Directory::GetFiles(saves))
        {
            auto copy = Path::Combine(target, Path::GetFileName(file));
            if (!File::Exists(copy))
            {
                File::Copy(file, copy);
            }
        }
    }

    bool StorageChoice::IsWritable(const std::string& folder)
    {
        auto probe = Path::Combine(folder, Probe);
        try
        {
            File::WriteAllBytes(probe, {});
            File::Delete(probe);
            return true;
        }
        catch (const IOException&)
        {
            return false;
        }
    }
}
