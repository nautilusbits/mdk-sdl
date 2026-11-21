#include "game/hud/Messages.h"

#include <algorithm>

#include "formats/Fti.h"
#include "game/hud/FontView.h"

namespace Mdk::Game::Hud
{
    bool Messages::Push(const std::string& textName, int flags, float seconds)
    {
        return PushText(_fti->GetTextBytes(textName), flags, seconds);
    }

    bool Messages::PushText(std::span<const byte> text, int flags, float seconds)
    {
        if (text.empty())
        {
            return false;
        }

        // At most two lines of 35 characters, split at the two-character \n escape.
        std::vector<std::vector<byte>> lines;
        auto length = static_cast<int>(text.size());
        auto start = 0;
        while (static_cast<int>(lines.size()) < MaxLines)
        {
            auto rest = text.subspan(start);
            auto hit = std::search(rest.begin(), rest.end(), LineBreak.begin(), LineBreak.end(),
                [](byte a, char b) { return a == static_cast<byte>(b); });
            auto found = hit == rest.end() ? -1 : static_cast<int>(hit - rest.begin());
            auto end = found < 0 ? length : start + found;
            lines.emplace_back(text.begin() + start, text.begin() + std::min(end, start + MaxLineLength));
            if (end >= length)
            {
                break;
            }

            start = end + static_cast<int>(LineBreak.size());
        }

        Message message{lines, flags, seconds};
        if ((flags & FlagFront) != 0)
        {
            _queue.push_front(message);
        }
        else
        {
            _queue.push_back(message);
        }

        if (static_cast<int>(_queue.size()) > QueueSize)
        {
            _queue.pop_back();
        }

        return true;
    }

    void Messages::Update(float delta)
    {
        auto step = !_queue.empty() ? delta * 2.f : delta;
        if (_time == 0.f)
        {
            if (_zoom > 0.f)
            {
                _zoom = MathF::Max(_zoom - delta, 0.f);
                return;
            }

            _current = _queue.empty() ? std::nullopt : std::optional<Message>(_queue.front());
            if (_current)
            {
                _queue.pop_front();
                _time = _current->Time;
            }

            return;
        }

        if ((_current->Flags & FlagZoom) == 0 || _zoom == ZoomTime)
        {
            _time = MathF::Max(_time - step, 0.f);
            return;
        }

        _zoom = MathF::Min(_zoom + delta, ZoomTime);
    }

    void Messages::Draw(float width)
    {
        if (!_current)
        {
            return;
        }

        auto full = _time > 0.f && ((_current->Flags & FlagZoom) == 0 || _zoom == ZoomTime);
        auto scale = full ? 1.f : _zoom * 2.f;
        if (scale <= 0.f)
        {
            return;
        }

        const auto& lines = _current->Lines;
        for (size_t i = 0; i < lines.size(); i++)
        {
            auto y = Baseline;
            if (static_cast<int>(lines.size()) == MaxLines)
            {
                y += (i == 1 ? LineOffset : -LineOffset) * scale;
            }

            auto& font = full && _big->Width(lines[i]) >= ViewWidth ? *_small : *_big;
            auto x = MathF::Round(width / 2.f - font.Width(lines[i]) * scale / 2.f);
            font.Draw(lines[i], x, MathF::Round(y), scale);
        }
    }
}
