#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Mdk::Game::Flow
{
    // A saved game's kind (the original's GAME type): at the level's start, before the level (its
    // briefing first), or a full snapshot (F2).
    enum class SaveKind { LevelStart = 3, BeforeLevel = 6, Snapshot = 1003 };

    // A saved game: its kind, level (LEVELn), health, deaths, the air strike and when; a full
    // snapshot also holds the level's state (JSON: {"kurt": ..., "level": ...}).
    struct SaveGame
    {
        static constexpr int FullHealth = 100;

        SaveKind Kind{};
        int Level = 0;
        int Health = 0;
        int Deaths = 0;
        bool StrikeUsed = false;
        std::string Time;
        std::optional<std::string> State;

        bool operator==(const SaveGame&) const = default;

        // As the Godot port writes it: {"type": 3, "level": 7, "health": 100, "deaths": 0,
        // "strike_used": false, "time": "..."}.
        std::string ToJson() const;

        // A save from its JSON, or null when it's invalid (no level 3-8).
        static std::optional<SaveGame> Parse(const std::string& text);

    private:
        static constexpr int FirstLevel = 3;
        static constexpr int LastLevel = 8;

        // Number(JsonElement, ...) lives in SaveGames.cpp: it takes a JSON node.
    };

    // The saved games: <NAME>.sav files of JSON in a directory (game_state.gd).
    // Dying writes LastGame, which only lasts for the session.
    class SaveGames
    {
    public:
        static constexpr const char* LastGame = "LASTGAME";

        explicit SaveGames(std::string directory) : _directory(std::move(directory)) {}

        const std::string& Directory() const { return _directory; }

        // Writes a save; false when it can't.
        bool Write(const std::string& name, const SaveGame& save);

        // A save, or null when it's missing or invalid.
        std::optional<SaveGame> Read(const std::string& name) const;

        // Deletes a save, if there is one.
        void Delete(const std::string& name);

        // The saves' names, sorted.
        std::vector<std::string> List() const;

        // The save quick load takes: `session` (this session's last full save) while it exists,
        // else the newest file; null when there is none.
        std::optional<std::string> QuickSlot(const std::optional<std::string>& session) const;

        // The saves of a user folder (saves/ in it).
        static SaveGames In(const std::string& userFolder);

    private:
        static constexpr const char* Extension = ".sav";
        static constexpr const char* SavesFolder = "saves";

        std::string _directory;

        std::string PathOf(const std::string& name) const;
    };
}
