#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

#include "core/Files.h"
#include "formats/CaseInsensitivePath.h"
#include "formats/MdkData.h"

namespace Mdk::Formats
{
    namespace
    {
        // The folders the game reads; the rest (executables, saves) stays behind.
        constexpr std::array<const char*, 4> GameFolders = {"TRAVERSE", "FALL3D", "STREAM", "MISC"};
        // Stream.CopyTo's buffer.
        constexpr std::streamsize CopyBuffer = 81920;

        // FirstOrDefault over a folder's entries: the first of `kind` named `name` in any case.
        std::optional<DataEntry> FindEntry(IDataTree& tree, const std::string& folder, EntryKind kind, const std::string& name)
        {
            for (auto& entry : tree.List(folder))
            {
                if (entry.Kind == kind && Str::EqualsIgnoreCase(entry.Name, name))
                {
                    return entry;
                }
            }

            return std::nullopt;
        }
    }

    std::optional<std::string> MdkData::FindIn(IDataTree& tree)
    {
        if (HasMarker(tree, ""))
        {
            return "";
        }

        for (auto& entry : tree.List(""))
        {
            if (entry.Kind == EntryKind::Folder && HasMarker(tree, entry.Name))
            {
                return entry.Name;
            }
        }

        return std::nullopt;
    }

    std::shared_ptr<MdkData> MdkData::Import(IDataTree& tree, const std::string& root, const std::string& dir, const std::function<void(int, int)>& progress)
    {
        // A copy over an older one: interrupted until the stamp is back.
        auto stamp = Path::Combine(dir, ImportStamp);
        Directory::CreateDirectory(dir);
        File::Delete(stamp);

        std::vector<std::pair<std::string, long long>> files;
        for (auto& entry : tree.List(root))
        {
            if (entry.Kind != EntryKind::Folder)
            {
                continue;
            }

            for (auto folder : GameFolders)
            {
                if (Str::EqualsIgnoreCase(entry.Name, folder))
                {
                    FilesOf(tree, Join(root, entry.Name), files);
                    break;
                }
            }
        }

        CopyFiles(tree, files, root, dir, progress);
        File::WriteAllText(stamp, "");
        return std::shared_ptr<MdkData>(new MdkData(dir));
    }

    int MdkData::CopyFolder(IDataTree& tree, const std::string& root, const std::string& name, const std::string& target, const std::function<void(int, int)>& progress)
    {
        auto folder = FindEntry(tree, root, EntryKind::Folder, name);
        if (!folder)
        {
            return 0;
        }

        auto from = Join(root, folder->Name);
        std::vector<std::pair<std::string, long long>> files;
        FilesOf(tree, from, files);
        return CopyFiles(tree, files, from, target, progress);
    }

    int MdkData::CopyFiles(IDataTree& tree, const std::vector<std::pair<std::string, long long>>& files, const std::string& from, const std::string& dir, const std::function<void(int, int)>& progress)
    {
        auto done = 0;
        auto copied = 0;
        auto total = static_cast<int>(files.size());
        for (auto& [file, size] : files)
        {
            auto relative = from.empty() ? file : file.substr(from.size() + 1);
            auto path = Path::Combine(dir, relative);
            done++;
            if (size != DataEntry::UnknownSize && File::Exists(path) && File::Length(path) == size)
            {
                if (progress)
                {
                    progress(done, total);
                }

                continue;
            }

            Directory::CreateDirectory(Path::GetDirectoryName(path));
            {
                auto source = tree.Open(file);
                std::ofstream target(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::trunc);
                if (!target)
                {
                    throw IOException("Could not create " + path);
                }

                // The buffer's own reads, as Stream.CopyTo: a stream's failure passes on (<< rdbuf() swallows it).
                std::vector<char> buffer(CopyBuffer);
                for (auto read = source->rdbuf()->sgetn(buffer.data(), CopyBuffer); read > 0; read = source->rdbuf()->sgetn(buffer.data(), CopyBuffer))
                {
                    if (!target.write(buffer.data(), read))
                    {
                        throw IOException("Could not write " + path);
                    }
                }
            }

            copied++;
            if (progress)
            {
                progress(done, total);
            }
        }

        return copied;
    }

    std::shared_ptr<MdkData> MdkData::Imported(const std::string& dir)
    {
        if (!File::Exists(Path::Combine(dir, ImportStamp)))
        {
            return nullptr;
        }

        return File::Exists(CaseInsensitivePath::Resolve(dir, Marker)) ? std::shared_ptr<MdkData>(new MdkData(dir)) : nullptr;
    }

    bool MdkData::HasMarker(IDataTree& tree, const std::string& folder)
    {
        auto path = folder;
        auto parts = Str::Split(Marker, Separator);
        for (size_t i = 0; i < parts.size(); i++)
        {
            auto kind = i == parts.size() - 1 ? EntryKind::File : EntryKind::Folder;
            auto entry = FindEntry(tree, path, kind, parts[i]);
            if (!entry)
            {
                return false;
            }

            path = Join(path, entry->Name);
        }

        return true;
    }

    void MdkData::FilesOf(IDataTree& tree, const std::string& folder, std::vector<std::pair<std::string, long long>>& files)
    {
        for (auto& entry : tree.List(folder))
        {
            auto path = Join(folder, entry.Name);
            if (entry.Kind == EntryKind::File)
            {
                files.emplace_back(path, entry.Size);
                continue;
            }

            FilesOf(tree, path, files);
        }
    }

    std::string MdkData::Join(const std::string& folder, const std::string& name) { return folder.empty() ? name : folder + Separator + name; }
}
