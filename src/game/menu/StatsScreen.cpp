#include "game/menu/StatsScreen.h"

#include <algorithm>
#include <format>

#include "core/Console.h"
#include "core/Drawing.h"
#include "engine/audio/AudioDevice.h"
#include "engine/platform/Input.h"
#include "formats/Bni.h"
#include "formats/Fti.h"
#include "formats/MdkData.h"
#include "formats/Palette.h"
#include "formats/Texture.h"
#include "game/flow/GameState.h"
#include "game/flow/LevelFlow.h"
#include "game/flow/SaveGames.h"
#include "game/flow/Settings.h"
#include "game/hud/FontView.h"
#include "game/menu/HeadsView.h"
#include "game/menu/SavePrompt.h"
#include "game/menu/Typesetter.h"
#include "game/menu/Ui.h"
#include "game/scripts/GameStats.h"

namespace Mdk::Game::Menu
{
    using Engine::Audio::Looping;
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Engine::Platform::MenuKey;
    using Flow::Event;
    using Flow::GameState;
    using Flow::SaveKind;

    namespace
    {
        // C#'s Phase.ToString().
        const char* NameOf(StatsScreen::Phase phase)
        {
            switch (phase)
            {
                case StatsScreen::Phase::Score:
                    return "Score";
                case StatsScreen::Phase::Intermission:
                    return "Intermission";
                case StatsScreen::Phase::Briefing:
                    return "Briefing";
                case StatsScreen::Phase::Debriefing:
                    return "Debriefing";
            }

            return "";
        }
    }

    StatsScreen::StatsScreen(Ui& ui, GameState& state, Flow::SaveGames& saves, StatsPages pages, std::optional<Phase> first)
        : _ui(ui), _state(state), _saves(saves)
    {
        _bni = Formats::Bni::Load(ui.Data()->PathOf(Archive));
        _systemRgb = std::vector<byte>(PaletteSize);
        auto system = ui.Fti->GetBytes("SYS_PAL");
        std::copy_n(system.begin(), std::min(static_cast<int>(system.size()), PaletteSize), _systemRgb.begin());
        _fonts = std::make_shared<Fonts>(ui.Renderer(), *ui.Fti, nullptr, ui.CanvasMods());
        _headsView = std::make_shared<HeadsView>(ui.Renderer(), *_bni, [this](std::span<const byte> rgb) { return PageRgb(rgb); }, *ui.Data());
        for (const std::string name : {"CGUN", "SNIPER", "RICO1", "RICO2", "RICO3", "ALDIE", "XGHEAD1", "XGHEAD2", "TELETYPE"})
        {
            _sounds[name] = Ui::SoundOf(*_bni, name, name == "CGUN" ? Looping::Forever : Looping::Once);
        }

        _index = Math::Max(GameState::IndexOf(state.Level), 0);
        _phases = pages == StatsPages::Briefing
            ? std::deque<Phase>{Phase::Briefing}
            : std::deque<Phase>{Phase::Intermission, Phase::Debriefing, Phase::Score, Phase::Briefing};
        while (first && _phases.size() > 1 && _phases.front() != *first)
        {
            _phases.pop_front();
        }

        auto phase = _phases.front();
        _phases.pop_front();
        Start(phase);
    }

    std::vector<std::string> StatsScreen::Status() const
    {
        return {_prompt != nullptr ? std::string("save prompt") : std::format("statistics: {}", NameOf(_phase))};
    }

    Engine::Platform::Typing StatsScreen::Typing() const
    {
        return _prompt != nullptr ? _prompt->Typing() : Engine::Platform::Typing::Off;
    }

    Event StatsScreen::Frame(float elapsed, const std::optional<std::string>& screenshot)
    {
        auto& input = _ui.Input();
        _ui.View->Layout(ScreenView::Fit::Inside);
        if (_prompt != nullptr)
        {
            _prompt->Update(input, elapsed);
            _prompt->Draw();
            _ui.Present(screenshot);
            if (_prompt->Closed())
            {
                _prompt = nullptr;
                NextPhase();
            }

            return _next;
        }

        if (input.AnyPressed())
        {
            _skip |= input.WasPressed(MenuKey::Back);
            _pressed = true;
        }

        Update(elapsed, input);
        Draw();
        _ui.Present(screenshot);
        return _next;
    }

    void StatsScreen::Update(float delta, Input& input)
    {
        _fast = _skip || input.IsDown(Key::Fire) || input.IsDown(Key::Jump);
        auto speed = _fast ? Fast : 1.f;
        _ticks += delta * TicksPerSecond;
        if (_fadeOut >= 0.f)
        {
            _fadeOut += FadeSpeed * speed * delta;
            if (_fadeOut >= 1.f)
            {
                EndPhase();
            }
        }
        else if (_fadeIn < 1.f)
        {
            _fadeIn = MathF::Min(_fadeIn + FadeSpeed * speed * delta, 1.f);
        }
        else if (UpdatePage(delta, speed) && (_pressed || _fast))
        {
            _fadeOut = 0.f;
            _ui.Audio().Stop(_loopVoice);
        }

        _skip = false;
        _pressed = false;
    }

    void StatsScreen::Start(Phase phase)
    {
        Console::WriteLine(std::format("Statistics: {}", NameOf(phase)));
        _phase = phase;
        _fadeIn = 0.f;
        _fadeOut = -1.f;
        _fadeColour = phase == Phase::Intermission ? Ui::White : Ui::Black;
        _texts.clear();
        _textY.clear();
        _current = 0;
        _count = 1.f;
        _shown = 0;
        _image = nullptr;
        switch (phase)
        {
            case Phase::Intermission:
                _image = LoadImage("L1_INTRM");
                break;
            case Phase::Debriefing:
            {
                _image = LoadImage(std::format("L{}_MAP", _index + 1));
                auto result = DebriefResults[(_state.Stats->TownFlags >> TownShift) & TownMask];
                auto name = std::format("DEB{}{}", _index + 1, result);
                if (!_ui.Fti->Has(name))
                {
                    name = std::format("DEB{}S", _index + 1);
                }

                for (const auto& entry : {std::string("DEBTOP"), name, std::string("DEBBOT")})
                {
                    _texts.push_back(_ui.Fti->GetTextBytes(entry));
                }

                _textY.insert(_textY.end(), DebriefY.begin(), DebriefY.end());
                break;
            }
            case Phase::Score:
                StartScore();
                break;
            case Phase::Briefing:
                // On entry the inventory is emptied and the health raised to 100 (every level starts with both).
                _image = LoadImage(std::format("L{}_MAP", _index + 1));
                _texts.push_back(_ui.Fti->GetTextBytes(std::format("BRIEF{}", _index + 1)));
                _textY.push_back(BriefY);
                break;
        }
    }

    void StatsScreen::EndPhase()
    {
        if (_phase != Phase::Score)
        {
            NextPhase();
            return;
        }

        _index = Math::Min(_index + 1, static_cast<int>(GameState::Order.size()) - 1);
        _state.Level = GameState::Order[_index];
        _prompt = std::make_shared<SavePrompt>(_ui, _fonts, _saves, _state.Save(SaveKind::BeforeLevel), Flow::LevelFlow::SaveName(_state.Level));
        _phase = Phase::Briefing;
        _image = nullptr;
    }

    void StatsScreen::NextPhase()
    {
        if (_phases.empty())
        {
            _next = Event::Fall;
            return;
        }

        auto phase = _phases.front();
        _phases.pop_front();
        Start(phase);
    }

    std::vector<byte> StatsScreen::PageRgb(std::span<const byte> rgb) const
    {
        std::vector<byte> page(_systemRgb.begin(), _systemRgb.begin() + SystemColours);
        page.insert(page.end(), rgb.begin() + SystemColours, rgb.begin() + PaletteSize);
        return page;
    }

    std::shared_ptr<PalettedImage> StatsScreen::LoadImage(const std::string& name)
    {
        if (!_bni->Has(name))
        {
            return nullptr;
        }

        const auto& entry = _bni->Entries().At(name);
        const auto& bytes = *_bni->Bytes();
        std::span<const byte> rgb(bytes.data() + entry.Offset, PaletteSize);
        return PalettedImage::Of(_ui.Renderer(), *Formats::Texture::Parse(name, bytes, entry.Offset + PaletteSize), PageRgb(rgb), _ui.CanvasMods());
    }

    bool StatsScreen::UpdatePage(float delta, float speed)
    {
        switch (_phase)
        {
            case Phase::Debriefing:
            case Phase::Briefing:
                return UpdateTyping(delta);
            case Phase::Score:
                return UpdateScore(delta, speed);
            default:
                return true;
        }
    }

    void StatsScreen::Play(const std::string& name)
    {
        auto found = _sounds.find(name);
        if (found == _sounds.end() || found->second == nullptr)
        {
            return;
        }

        auto free = -1;
        for (auto i = 0; i < Players; i++)
        {
            if (!_ui.Audio().IsPlaying(_voices[i]))
            {
                free = i;
                break;
            }
        }

        auto slot = free >= 0 ? free : 0;
        _ui.Audio().Stop(_voices[slot]);
        _voices[slot] = _ui.Play(found->second);
    }

    // --- Typing ---------------------------------------------------------------------------------

    bool StatsScreen::UpdateTyping(float delta)
    {
        if (_current >= static_cast<int>(_texts.size()))
        {
            return true;
        }

        auto total = std::get<1>(Typesetter::Layout(_texts[_current], 0.f));
        if (_skip)
        {
            if (_phase == Phase::Briefing)
            {
                _current = static_cast<int>(_texts.size());
                return false;
            }

            _count = static_cast<float>(total);
        }
        else
        {
            _count += (_fast ? TypeRateFast : TypeRate) * delta;
        }

        auto shown = Math::Min(ToInt(MathF::Round(_count)), total);
        if (shown != _shown)
        {
            _shown = shown;
            _ui.Audio().Stop(_teletypeVoice);
            auto teletype = _sounds.find("TELETYPE");
            _teletypeVoice = _ui.Play(teletype != _sounds.end() ? teletype->second : nullptr);
        }

        if (_count < static_cast<float>(total))
        {
            return false;
        }

        _current++;
        _count = 0.f;
        _shown = 0;
        return _current >= static_cast<int>(_texts.size());
    }

    void StatsScreen::DrawTyped(std::span<const byte> text, float y, int budget)
    {
        auto [lines, count] = Typesetter::Layout(text, y);
        auto remaining = budget < 0 ? count : budget;
        auto& font = *_fonts->Big;
        for (auto n = 0; n < static_cast<int>(lines.size()); n++)
        {
            const auto& line = lines[n];
            if (line.Start > remaining)
            {
                break;
            }

            auto length = Math::Min(static_cast<int>(line.Text.size()), remaining - line.Start);
            std::vector<byte> part(line.Text.begin(), line.Text.begin() + length);

            // The cursor follows the last line being typed.
            auto last = n + 1 == static_cast<int>(lines.size()) || lines[n + 1].Start > remaining;
            if (last && remaining < count)
            {
                part.push_back((ToInt(_ticks) & CursorBlink) <= CursorOn ? static_cast<byte>('_') : static_cast<byte>(' '));
            }

            // A centred line is placed by its whole width, so it doesn't move while typed.
            auto left = line.Centred ? line.X - static_cast<float>(font.Width(line.Text) >> 1) : line.X;
            _ui.View->Text(font, part, left, line.Y);
        }
    }

    // --- Score-O-matic --------------------------------------------------------------------------

    void StatsScreen::StartScore()
    {
        const auto& s = *_state.Stats;
        std::array<int, Rows.size()> targets{s.Shots, Scripts::GameStats::Percent(s.ShotHits, s.Shots), s.SniperShots, Scripts::GameStats::Percent(s.SniperHits, s.SniperShots), s.Kills};
        std::array<int, Rows.size()> fulls{s.Shots, 100, s.SniperShots, 100, s.Enemies};
        _targets = targets;
        _fulls = fulls;
        _values.fill(0.f);
        _slides.fill(Hidden);
        _row = 0;
        _rowStage = 0;
        _rowTime = 0.f;
        _heads = 0;
        _headTime = 0.f;
        std::string rows;
        for (size_t i = 0; i < _targets.size(); i++)
        {
            rows += std::format("{}{}/{}", i == 0 ? "" : ", ", _targets[i], _fulls[i]);
        }

        Console::WriteLine(std::format("Score-O-matic: {}, heads {}", rows, s.HeadShots));
    }

    bool StatsScreen::UpdateScore(float delta, float speed)
    {
        for (size_t i = 0; i < _slides.size(); i++)
        {
            if (_slides[i] >= 0.f && _slides[i] < 1.f)
            {
                _slides[i] = _skip ? 1.f : MathF::Min(_slides[i] + SlideSpeed * speed * delta, 1.f);
            }
        }

        if (_row < static_cast<int>(Rows.size()))
        {
            UpdateRow(delta, speed);
            return false;
        }

        return UpdateHeads(delta, speed);
    }

    void StatsScreen::UpdateRow(float delta, float speed)
    {
        _rowTime += delta * speed;
        auto waited = _rowTime >= RowWait || _skip;
        if (_rowStage == 0 && waited)
        {
            _rowStage = 1;
            _rowTime = 0.f;
            _slides[_row] = Big;
            Play("SNIPER");
        }
        else if (_rowStage == 1 && waited)
        {
            _rowStage = 2;
        }
        else if (_rowStage == 2)
        {
            CountRow(delta, speed);
        }
    }

    void StatsScreen::CountRow(float delta, float speed)
    {
        auto target = static_cast<float>(_targets[_row]);
        if (_skip)
        {
            _values[_row] = target;
        }
        else
        {
            _values[_row] = MathF::Min(_values[_row] + MathF::Max(1.f, MathF::Round(target * delta * CountRate)) * speed, target);
            if (_row == KillsRow && !_ui.Audio().IsPlaying(_voices[0]))
            {
                Play("ALDIE");
            }
            else if (_row != KillsRow && !_ui.Audio().IsPlaying(_loopVoice))
            {
                auto cgun = _sounds.find("CGUN");
                _loopVoice = _ui.Play(cgun != _sounds.end() ? cgun->second : nullptr);
            }

            if ((_row == 1 || _row == 3) && _random.Next(RicochetChance) == 0)
            {
                Play(std::format("RICO{}", _random.Next(Ricochets) + 1));
            }
        }

        if (_values[_row] < target)
        {
            return;
        }

        _ui.Audio().Stop(_loopVoice);
        _slides[_row] = 0.f;
        _row = _row + 1;
        _rowStage = 0;
        _rowTime = 0.f;
    }

    bool StatsScreen::UpdateHeads(float delta, float speed)
    {
        _headsView->Update(_heads, delta);
        if (!_ui.Settings().Gore)
        {
            return true;
        }

        auto count = _state.Stats->HeadShots;
        if (_row == static_cast<int>(Rows.size()))
        {
            _rowTime += delta * speed;
            if (_rowStage == 0 && (_rowTime >= RowWait || _skip))
            {
                Play("SNIPER");
                _rowStage = 1;
                _rowTime = 0.f;
            }
            else if (_rowStage == 1 && (_rowTime >= RowWait || _skip))
            {
                _row++;
                _headTime = 1.f;
            }

            return false;
        }

        if (_heads >= count)
        {
            return true;
        }

        if (_skip)
        {
            _heads = count;
            return true;
        }

        _headTime += delta * speed;
        if (_headTime < 1.f)
        {
            return false;
        }

        _headTime = 0.f;
        _heads++;
        Play(std::format("XGHEAD{}", _random.Next(2) + 1));
        return _heads >= count;
    }

    // --- Drawing --------------------------------------------------------------------------------

    void StatsScreen::Draw()
    {
        auto& view = *_ui.View;
        if (_image != nullptr)
        {
            _image->Draw(view, Vector2::Zero());
        }

        auto shown = _fadeIn >= 1.f;
        if ((_phase == Phase::Debriefing || _phase == Phase::Briefing) && shown)
        {
            for (auto i = 0; i < Math::Min(_current + 1, static_cast<int>(_texts.size())); i++)
            {
                DrawTyped(_texts[i], _textY[i], i < _current ? -1 : ToInt(MathF::Round(_count)));
            }
        }
        else if (_phase == Phase::Score)
        {
            DrawScore(shown);
        }

        // Fades: the page is blended with white or black.
        auto fade = _fadeOut < 0.f ? 1.f - _fadeIn : _fadeOut;
        if (fade > 0.f)
        {
            auto colour = _fadeOut < 0.f ? _fadeColour : Ui::Black;
            colour.W = Math::Min(fade, 1.f);
            view.Fill(RectangleF(0.f, 0.f, ScreenView::Size.X, ScreenView::Size.Y), colour);
        }
    }

    void StatsScreen::DrawScore(bool shown)
    {
        auto& view = *_ui.View;
        auto& big = *_fonts->Big;
        auto& small = *_fonts->Small;
        auto title = _ui.Fti->GetTextBytes("ST_SCR");
        view.Text(big, title, static_cast<float>(300 - (big.Width(title) >> 1)), TitleY);
        auto name = _ui.Fti->GetTextBytes("ST_DAMP");
        view.Text(small, name, static_cast<float>(300 - (small.Width(name) >> 1)), NameY);
        if (_ui.Settings().Gore)
        {
            _headsView->Draw(_heads, _state.Stats->HeadShots, view.ToCanvas(RectangleF(0.f, 0.f, ScreenView::Size.X, ScreenView::Size.Y)));
        }

        if (!shown)
        {
            return;
        }

        const auto& palette = Formats::Palette::FromRgb(_systemRgb)->Rgba;
        constexpr float maxByte = 255.f;
        auto barColour = Vector4(palette[BarColour * 4], palette[BarColour * 4 + 1], palette[BarColour * 4 + 2], maxByte) / maxByte;
        for (size_t i = 0; i < Rows.size(); i++)
        {
            if (_slides[i] == Hidden)
            {
                continue;
            }

            auto t = Math::Clamp(_slides[i], 0.f, 1.f);
            auto place = Vector4::Lerp(Rows[i].From, Rows[i].To, t);
            place = Vector4(MathF::Round(place.X), MathF::Round(place.Y), MathF::Round(place.Z), MathF::Round(place.W));
            auto scale = place.W / FullScale;
            auto label = _ui.Fti->GetTextBytes(Rows[i].Label);
            view.Text(big, label, place.X - MathF::Round(static_cast<float>(big.Width(label)) * scale * 0.5f), place.Y, scale);
            auto bar = Vector2(place.X - place.Z / 2.f, MathF::Round(place.Y + BarRise * scale));
            auto value = ToInt(_values[i]);
            if (_fulls[i] > 0 && value > 0)
            {
                view.Fill(RectangleF(bar.X, bar.Y, MathF::Floor(place.Z * static_cast<float>(value) / static_cast<float>(_fulls[i])), BarHeight), barColour);
            }

            auto valueText = ValueText(static_cast<int>(i), value, _fulls[i]);
            std::vector<byte> text(valueText.begin(), valueText.end());
            view.Text(small, text, bar.X - static_cast<float>(small.Width(text)) - ValueGap, bar.Y + ValueDrop);
        }

        if (_row >= static_cast<int>(Rows.size()) && _ui.Settings().Gore)
        {
            auto label = _ui.Fti->GetTextBytes("ST_HEAD");
            view.Text(big, label, HeadLabel.X - static_cast<float>(big.Width(label) >> 1), HeadLabel.Y);
        }
    }

    std::string StatsScreen::ValueText(int row, int value, int full)
    {
        if (row == KillsRow)
        {
            return std::format("{}/{}", value, full);
        }

        if (row == 1 || row == 3)
        {
            return std::format("{}%", value);
        }

        return std::to_string(value);
    }

    void StatsScreen::Dispose()
    {
        _ui.Audio().StopAll();
    }
}
