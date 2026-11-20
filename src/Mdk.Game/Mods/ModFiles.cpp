#include "Mdk.Game/Mods/ModFiles.h"

#include <filesystem>

#include "Mdk.Core/FileStream.h"
#include "Mdk.Core/Files.h"
#include "Mdk.Game/Mods/ZipFiles.h"

namespace Mdk::Game::Mods
{
    namespace
    {
        // Path.DirectorySeparatorChar.
        constexpr char DirectorySeparator = static_cast<char>(std::filesystem::path::preferred_separator);
    }

    std::shared_ptr<ModFiles> ModFiles::Folder(const std::string& folder)
    {
        return std::make_shared<FolderFiles>(folder);
    }

    std::shared_ptr<ModFiles> ModFiles::Zip(const std::string& path)
    {
        return ZipFiles::Open(path);
    }

    FolderFiles::FolderFiles(const std::string& folder) : ModFiles(Mdk::Path::GetFileName(folder)), _root(Mdk::Path::GetFullPath(folder))
    {
    }

    bool FolderFiles::Has(const std::string& path)
    {
        return File::Exists(Where(path));
    }

    std::vector<std::string> FolderFiles::List(const std::string& folder)
    {
        const auto directory = Where(folder);
        if (!Directory::Exists(directory))
        {
            return {};
        }

        // Path.GetRelativePath(_root, f): the files are under the root.
        std::vector<std::string> files;
        for (const auto& f : Directory::GetFiles(directory, "*", Directory::Search::AllDirectories))
        {
            const auto relative = FileStream::NativePath(f).lexically_relative(FileStream::NativePath(_root)).u8string();
            files.push_back(Str::Replace(std::string(relative.begin(), relative.end()), std::string_view(&DirectorySeparator, 1), std::string_view(&Separator, 1)));
        }

        return files;
    }

    std::vector<byte> FolderFiles::Read(const std::string& path)
    {
        return File::ReadAllBytes(Where(path));
    }

    std::string FolderFiles::Where(const std::string& path)
    {
        return Mdk::Path::Combine(_root, Str::Replace(path, std::string_view(&Separator, 1), std::string_view(&DirectorySeparator, 1)));
    }
}
