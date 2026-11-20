#pragma once

#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Formats
{
    class Fti;
}

namespace Mdk::Game::Hud
{
    class FontView;

    // On-screen messages: texts of MDKFONT.FTI queued by hud_message (opcode 247), pickups and a few
    // events (0x425400), shown one after the other in the big font (0x425474). One or two lines
    // (split at the \n escape), centred on the 600x360 view, baseline at y 120 (two lines: 105 and
    // 135). Flag 1 grows the message from nothing in half a second and shrinks it back when its time
    // is up; its time runs twice as fast while others wait. Flag 2 queues it first. A line too wide
    // for the big font is drawn in the small one (0x415b30).
    class Messages
    {
    public:
        static constexpr int FlagZoom = 1;
        static constexpr int FlagFront = 2;

        Messages(std::shared_ptr<Formats::Fti> fti, std::shared_ptr<FontView> big, std::shared_ptr<FontView> small)
            : _fti(std::move(fti)), _big(std::move(big)), _small(std::move(small)) {}

        // Queues the text `textName`; false if it doesn't exist.
        bool Push(const std::string& textName, int flags, float seconds);

        // Queues a text of the port's own ("Game saved"); false if it's empty.
        bool PushText(std::span<const byte> text, int flags, float seconds);

        // Advances the messages (0x425474, once per frame).
        void Update(float delta);

        // Draws the current message, centred on a canvas `width` wide.
        void Draw(float width);

    private:
        // The queue has 4 entries (0x57ecf0).
        static constexpr int QueueSize = 4;
        // Seconds to grow or shrink (0.5 × 2 = full size).
        static constexpr float ZoomTime = 0.5f;
        static constexpr float ViewWidth = 600.f;
        static constexpr float Baseline = 120.f;
        static constexpr float LineOffset = 15.f;
        static constexpr int MaxLines = 2;
        static constexpr int MaxLineLength = 35;
        static constexpr std::string_view LineBreak = "\\n";

        struct Message
        {
            std::vector<std::vector<byte>> Lines;
            int Flags = 0;
            float Time = 0;

            bool operator==(const Message&) const = default;
        };

        std::shared_ptr<Formats::Fti> _fti;
        std::shared_ptr<FontView> _big;
        std::shared_ptr<FontView> _small;
        std::list<Message> _queue;
        std::optional<Message> _current;
        // Time the current message stays (0x57ece0) and its zoom (0x57ece4, 0-0.5).
        float _time = 0;
        float _zoom = 0;
    };
}
