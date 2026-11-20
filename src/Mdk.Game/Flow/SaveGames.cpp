#include "Mdk.Game/Flow/SaveGames.h"

#include <algorithm>
#include <format>

#include <json.hpp>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Files.h"

namespace Mdk::Game::Flow
{
    namespace
    {
        int Number(const nlohmann::json& root, const char* name, int fallback)
        {
            auto value = root.find(name);
            return value != root.end() && value->is_number() ? ToInt(value->get<double>()) : fallback;
        }
    }

    std::string SaveGame::ToJson() const
    {
        // Utf8JsonWriter's order and compact form, not nlohmann's (sorted keys).
        auto text = std::format(R"({{"type":{},"level":{},"health":{},"deaths":{},"strike_used":{},"time":{})",
            static_cast<int>(Kind), Level, Health, Deaths, StrikeUsed ? "true" : "false", nlohmann::json(Time).dump());
        if (State.has_value())
        {
            text += R"(,"snapshot":)" + *State;
        }

        return text + "}";
    }

    std::optional<SaveGame> SaveGame::Parse(const std::string& text)
    {
        try
        {
            auto root = nlohmann::json::parse(text);
            auto levelValue = root.is_object() ? root.find("level") : root.end();
            if (!root.is_object() || levelValue == root.end())
            {
                return std::nullopt;
            }

            auto level = ToInt(levelValue->get<double>());
            if (level < FirstLevel || level > LastLevel)
            {
                return std::nullopt;
            }

            auto kind = static_cast<SaveKind>(Number(root, "type", static_cast<int>(SaveKind::LevelStart)));
            auto s = root.find("strike_used");
            auto strike = s != root.end() && s->is_boolean() && s->get<bool>();
            auto t = root.find("time");
            auto time = t != root.end() && t->is_string() ? t->get<std::string>() : "";
            auto snapshot = root.find("snapshot");
            auto state = snapshot != root.end() && snapshot->is_object() ? std::optional<std::string>(snapshot->dump()) : std::nullopt;
            return SaveGame{kind, level, Number(root, "health", FullHealth), Number(root, "deaths", 0), strike, time, state};
        }
        catch (const nlohmann::json::parse_error&)
        {
            return std::nullopt;
        }
    }

    bool SaveGames::Write(const std::string& name, const SaveGame& save)
    {
        try
        {
            Mdk::Directory::CreateDirectory(_directory);
            File::WriteAllText(PathOf(name), save.ToJson());
            return true;
        }
        catch (const IOException&)
        {
            return false;
        }
    }

    std::optional<SaveGame> SaveGames::Read(const std::string& name) const
    {
        auto path = PathOf(name);
        return File::Exists(path) ? SaveGame::Parse(File::ReadAllText(path)) : std::nullopt;
    }

    void SaveGames::Delete(const std::string& name)
    {
        if (File::Exists(PathOf(name)))
        {
            File::Delete(PathOf(name));
        }
    }

    std::vector<std::string> SaveGames::List() const
    {
        if (!Mdk::Directory::Exists(_directory))
        {
            return {};
        }

        std::vector<std::string> names;
        for (const auto& file : Mdk::Directory::GetFiles(_directory, std::string("*") + Extension))
        {
            names.push_back(Path::GetFileNameWithoutExtension(file));
        }

        std::sort(names.begin(), names.end());
        return names;
    }

    std::optional<std::string> SaveGames::QuickSlot(const std::optional<std::string>& session) const
    {
        if (session.has_value() && File::Exists(PathOf(*session)))
        {
            return session;
        }

        // OrderByDescending is stable: the first of equally new files wins.
        auto names = List();
        std::stable_sort(names.begin(), names.end(), [this](const std::string& a, const std::string& b)
            {
                return File::LastWriteTime(PathOf(a)) > File::LastWriteTime(PathOf(b));
            });
        return names.empty() ? std::nullopt : std::optional<std::string>(names.front());
    }

    std::string SaveGames::PathOf(const std::string& name) const
    {
        return Path::Combine(_directory, name + Extension);
    }

    SaveGames SaveGames::In(const std::string& userFolder)
    {
        return SaveGames(Path::Combine(userFolder, SavesFolder));
    }
}
