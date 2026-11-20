#include "Mdk.Game/HdTextures/HdManifest.h"

#include <algorithm>
#include <format>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Files.h"

namespace Mdk::Game::HdTextures
{
    std::shared_ptr<HdManifest> HdManifest::Load(const std::string& folder)
    {
        const auto path = Path::Combine(folder, FileName);
        if (!File::Exists(path))
        {
            return nullptr;
        }

        Dictionary<std::string, std::string> settings;
        std::vector<Entry> entries;
        for (const auto& line : File::ReadAllLines(path))
        {
            const auto setting = Str::SplitCount(line, '=', 2);
            if (setting.size() == 2)
            {
                settings[setting[0]] = setting[1];
                continue;
            }

            if (auto entry = Parse(line))
            {
                entries.push_back(std::move(*entry));
            }
        }

        int version = 0;
        int scale = 0;
        const auto* format = settings.Find("format");
        const auto* scaleText = settings.Find("scale");
        if (format == nullptr || !Str::TryParseInt(*format, version) || scaleText == nullptr || !Str::TryParseInt(*scaleText, scale))
        {
            return nullptr;
        }

        auto manifest = std::make_shared<HdManifest>(settings.GetValueOrDefault("model", ""), scale);
        manifest->Version = version;
        for (const auto& entry : entries)
        {
            manifest->Entries[entry.Key] = entry;
        }

        return manifest;
    }

    std::optional<HdManifest::Entry> HdManifest::Parse(const std::string& line)
    {
        const auto f = Str::Split(line, ' ');
        if (std::ssize(f) != Fields)
        {
            return std::nullopt;
        }

        int numbers[4];
        const std::string* texts[] = {&f[1], &f[3], &f[4], &f[5]};
        for (size_t i = 0; i < std::size(texts); i++)
        {
            int v = 0;
            numbers[i] = Str::TryParseInt(*texts[i], v) ? v : -1;
        }

        if (std::any_of(std::begin(numbers), std::end(numbers), [](int n) { return n < 0; }))
        {
            return std::nullopt;
        }

        return Entry{f[0], numbers[0], f[2], numbers[1], numbers[2], numbers[3], f[6]};
    }

    void HdManifest::Save(const std::string& folder) const
    {
        std::vector<std::string> lines{std::format("format={}", Version), std::format("model={}", Model()), std::format("scale={}", Scale())};
        std::vector<Entry> sorted;
        for (const auto& [key, e] : Entries)
        {
            sorted.push_back(e);
        }

        std::stable_sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) { return a.File < b.File; });
        for (const auto& e : sorted)
        {
            lines.push_back(std::format("{} {} {} {} {} {} {}", e.Key, e.Level, e.Name, e.Width, e.Height, e.Frames, e.File));
        }

        const auto path = Path::Combine(folder, FileName);
        File::WriteAllLines(path + ".tmp", lines);
        File::Move(path + ".tmp", path, true);
    }
}
