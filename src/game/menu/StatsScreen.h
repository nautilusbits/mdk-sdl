#pragma once

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "game/flow/Screen.h"

namespace Mdk::Engine::Audio
{
    class Sound;
}

namespace Mdk::Engine::Platform
{
    class Input;
}

namespace Mdk::Formats
{
    class Bni;
}

namespace Mdk::Game::Flow
{
    class GameState;
    class SaveGames;
}

namespace Mdk::Game::Menu
{
    class Fonts;
    class HeadsView;
    class PalettedImage;
    class SavePrompt;
    class Ui;

    // Which pages the statistics show: all of them after a level, or the briefing alone (a new
    // game, a save made before a level).
    enum class StatsPages { All, Briefing };

    // The screens between levels (game state 6: 0x431b00, each frame 0x43200c; stats_screen.gd).
    // After a level: the intermission (L1_INTRM), the debriefing on the level's map, the
    // Score-O-matic, the save prompt, then the briefing of the next level on its map. Every page fades
    // in (from white for the intermission, else from black) over 0.5 s, waits for a key once it's fully
    // shown and fades out to black. Esc skips; holding Fire or Jump runs everything twice as fast
    // (typing 4x). All on the 600 x 360 screen.
    //   intermission ─► debriefing ─► Score-O-matic ─► save prompt ─► briefing ─► fall ─► level
    class StatsScreen : public Flow::IScreen
    {
    public:
        enum class Phase { Score = 1, Intermission = 2, Briefing = 3, Debriefing = 4 };

        StatsScreen(Ui& ui, Flow::GameState& state, Flow::SaveGames& saves, StatsPages pages, std::optional<Phase> first);

        // "statistics: Score", "statistics: Briefing", or the save prompt between them.
        std::vector<std::string> Status() const override;

        Engine::Platform::Typing Typing() const override;

        Flow::Event Frame(float elapsed, const std::optional<std::string>& screenshot) override;

        // A row's value: %d/%d for kills, %d%% for accuracies, else %d.
        static std::string ValueText(int row, int value, int full);

        void Dispose() override;

    private:
        // A Score-O-matic row: label, start and end (x, y, bar width, scale).
        struct RowKeys
        {
            const char* Label;
            Vector4 From;
            Vector4 To;
        };

        static constexpr const char* Archive = "MISC/STATS.BNI";
        static constexpr float FadeSpeed = 2.f;
        static constexpr float Fast = 2.f;
        // Typing (0x4335c0): characters per second, and when fast.
        static constexpr float TypeRate = 15.f;
        static constexpr float TypeRateFast = 60.f;
        static constexpr std::array<float, 3> DebriefY{64.f, 120.f, 300.f};
        static constexpr float BriefY = 32.f;
        // Debriefing texts by the town flags' bits 31-29 (0x57440f).
        static constexpr std::array<const char*, 8> DebriefResults{"S", "S", "F", "S", "SS", "SF", "FS", "FF"};
        static constexpr int TownShift = 29;
        static constexpr int TownMask = 7;
        // Score-O-matic rows (tables 0x491b78 and 0x491bd8).
        static constexpr std::array<RowKeys, 5> Rows{{
            {"ST_SHF", Vector4(300, 85, 240, 256), Vector4(180, 75, 120, 128)},
            {"ST_ACC", Vector4(300, 155, 240, 256), Vector4(420, 75, 120, 128)},
            {"ST_SNF", Vector4(300, 155, 240, 256), Vector4(180, 125, 120, 128)},
            {"ST_ACC", Vector4(300, 225, 240, 256), Vector4(420, 125, 120, 128)},
            {"ST_KILL", Vector4(300, 225, 240, 256), Vector4(300, 175, 120, 128)},
        }};
        static constexpr int KillsRow = 4;
        static constexpr float FullScale = 256.f;
        static constexpr Vector2 HeadLabel{300.f, 255.f};
        static constexpr float RowWait = 0.5f;
        static constexpr int BarColour = 63;
        static constexpr float BarHeight = 16.f;
        static constexpr float BarRise = 18.f;
        static constexpr float ValueGap = 8.f;
        static constexpr float ValueDrop = 14.f;
        static constexpr float TitleY = 28.f;
        static constexpr float NameY = 48.f;
        static constexpr float CountRate = 0.5f;
        static constexpr float SlideSpeed = 2.f;
        static constexpr int RicochetChance = 8;
        static constexpr int Ricochets = 3;
        static constexpr int CursorBlink = 31;
        static constexpr int CursorOn = 15;
        static constexpr float TicksPerSecond = 30.f;
        static constexpr float Hidden = -1.f;
        // A row shows big (t = 0) until it slides.
        static constexpr float Big = -0.5f;
        static constexpr int PaletteSize = 768;
        static constexpr int SystemColours = 192;
        static constexpr int Players = 4;

        Ui& _ui;
        Flow::GameState& _state;
        Flow::SaveGames& _saves;
        std::shared_ptr<Formats::Bni> _bni;
        std::shared_ptr<Fonts> _fonts;
        std::vector<byte> _systemRgb;
        std::deque<Phase> _phases;
        std::unordered_map<std::string, std::shared_ptr<Engine::Audio::Sound>> _sounds;
        std::array<int, Players> _voices{};
        Random _random;
        Phase _phase{};
        int _index = 0;
        std::shared_ptr<PalettedImage> _image;
        float _fadeIn = 0;
        float _fadeOut = -1.f;
        Vector4 _fadeColour;
        bool _fast = false;
        bool _skip = false;
        bool _pressed = false;
        float _ticks = 0;
        std::shared_ptr<SavePrompt> _prompt;
        int _loopVoice = 0;
        int _teletypeVoice = 0;
        Flow::Event _next = Flow::Event::None;
        // Typing: the texts of the page, their baselines, and the characters shown of the current one.
        std::vector<std::vector<byte>> _texts;
        std::vector<float> _textY;
        int _current = 0;
        float _count = 0;
        int _shown = 0;
        // Score-O-matic.
        int _row = 0;
        float _rowTime = 0;
        int _rowStage = 0;
        std::array<float, Rows.size()> _values{};
        std::array<int, Rows.size()> _targets{};
        std::array<int, Rows.size()> _fulls{};
        std::array<float, Rows.size()> _slides{};
        int _heads = 0;
        float _headTime = 0;
        std::shared_ptr<HeadsView> _headsView;

        void Update(float delta, Engine::Platform::Input& input);

        void Start(Phase phase);

        // After the Score-O-matic the index moves on to the next level, which can be saved
        // (named after its number); after the briefing the level starts.
        void EndPhase();

        // The next page; after the last one (the briefing) the fall, then the level.
        void NextPhase();

        // A page's palette: the system colours 0-63 and the image's 64-255.
        std::vector<byte> PageRgb(std::span<const byte> rgb) const;

        // An image of STATS.BNI: a 768-byte palette, u16 width, height, pixels.
        std::shared_ptr<PalettedImage> LoadImage(const std::string& name);

        // Advances the page; true once everything is shown.
        bool UpdatePage(float delta, float speed);

        void Play(const std::string& name);

        // --- Typing ---------------------------------------------------------------------------------

        // Types the page's texts one after the other; Esc finishes the current one (the briefing: all).
        bool UpdateTyping(float delta);

        // Draws a text with a budget of characters (-1: all), a cursor after the last one typed.
        void DrawTyped(std::span<const byte> text, float y, int budget);

        // --- Score-O-matic --------------------------------------------------------------------------

        void StartScore();

        // The rows one at a time (0x4328a4); then the heads.
        bool UpdateScore(float delta, float speed);

        // Wait, show big (SNIPER), wait, count up (about 2 s), slide to the final place.
        void UpdateRow(float delta, float speed);

        void CountRow(float delta, float speed);

        // Head shots (0x433268): one a second (two when fast, all at once on Esc), spinning.
        // Without gore the row is left out (0x43290b).
        bool UpdateHeads(float delta, float speed);

        // --- Drawing --------------------------------------------------------------------------------

        void Draw();

        void DrawScore(bool shown);
    };
}
