#include "Mdk.Formats/MdkData.h"

#include <filesystem>

#include "Mdk.Core/Files.h"
#include "Mdk.Formats/CaseInsensitivePath.h"
#include "Mdk.Formats/LocalPaths.h"

namespace Mdk::Formats
{
    std::shared_ptr<MdkData> MdkData::Find()
    {
        std::vector<std::string> candidates;
        auto env = GetEnv(EnvironmentVariable);
        if (env && !env->empty())
        {
            candidates.push_back(*env);
        }

        auto local = LocalPaths::Get(LocalPaths::Game);
        if (!local.empty())
        {
            candidates.push_back(local);
        }

        // The project folder placed within the MDK folder: walk up from the executable.
        auto base = Path::BaseDirectory();
        auto dir = std::filesystem::path(std::u8string(base.begin(), base.end()));
        if (!dir.has_filename())
        {
            dir = dir.parent_path();
        }

        while (true)
        {
            auto text = dir.generic_u8string();
            candidates.emplace_back(text.begin(), text.end());
            auto parent = dir.parent_path();
            if (parent == dir || parent.empty())
            {
                break;
            }

            dir = parent;
        }

        candidates.emplace_back("C:/GOG Games/MDK");
        candidates.emplace_back("C:/Program Files (x86)/GOG Galaxy/Games/MDK");
        candidates.emplace_back("C:/Program Files (x86)/Steam/steamapps/common/MDK");

        for (auto& candidate : candidates)
        {
            if (File::Exists(CaseInsensitivePath::Resolve(candidate, Marker)))
            {
                return std::shared_ptr<MdkData>(new MdkData(candidate));
            }
        }

        return nullptr;
    }

    std::shared_ptr<MdkData> MdkData::At(const std::string& folder)
    {
        try
        {
            if (HasMarker(folder))
            {
                return std::shared_ptr<MdkData>(new MdkData(folder));
            }

            if (!Directory::Exists(folder))
            {
                return nullptr;
            }

            for (auto& inner : Directory::GetDirectories(folder))
            {
                if (HasMarker(inner))
                {
                    return std::shared_ptr<MdkData>(new MdkData(inner));
                }
            }

            return nullptr;
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return nullptr;
        }
        catch (const IOException&)
        {
            return nullptr;
        }
    }

    bool MdkData::HasMarker(const std::string& folder) { return File::Exists(CaseInsensitivePath::Resolve(folder, Marker)); }

    std::string MdkData::PathOf(const std::string& relative) const { return CaseInsensitivePath::Resolve(_dir, relative); }

    std::vector<byte> MdkData::Read(const std::string& relative) const { return File::ReadAllBytes(PathOf(relative)); }
}
