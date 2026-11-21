#include "game/flow/UserFolders.h"

#include "core/DotNet.h"
#include "core/Files.h"
#include "core/SpecialFolder.h"

namespace Mdk::Game::Flow
{
    std::string UserFolders::Personal()
    {
        auto root = Environment::IsWindows() ? Environment::SpecialFolder::ApplicationData : Environment::SpecialFolder::LocalApplicationData;
        return Path::Combine(Environment::GetFolderPath(root), Name);
    }

    std::string UserFolders::Choose(const std::string& beside, const std::string& personal)
    {
        if (IsWritable(beside))
        {
            return beside;
        }

        Directory::CreateDirectory(personal);
        return Path::GetFullPath(personal);
    }

    bool UserFolders::IsWritable(const std::string& folder)
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
