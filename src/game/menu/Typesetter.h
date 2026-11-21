#pragma once

#include <span>
#include <tuple>
#include <vector>

#include "core/DotNet.h"

namespace Mdk::Game::Menu
{
    // Lays out the briefings' and debriefings' texts like 0x4335c0. Codes (a decimal number N,
    // possibly negative, may precede the letter):
    //   \c, \Nc  end the line; the next is centred on x 300 (or N); y unchanged
    //   \n, \Nn  end the line; x 0, left-aligned; y += 36 (+ N)
    //   \y, \Ny  end the line; x 0, left-aligned; y += 36 (or N)
    //   \x, \Nx  end the line; left-aligned at x N (or 0)
    //   \Np      pause: the budget loses N characters
    //   \d, \i   characters cost one / appear at once
    struct Typesetter
    {
        // A line: its characters, where it starts (x is its centre when centred), and the
        // character count before it.
        struct Line
        {
            std::vector<byte> Text;
            float X = 0;
            float Y = 0;
            bool Centred = false;
            int Start = 0;

            bool operator==(const Line&) const = default;
        };

        static constexpr float LineHeight = 36.f;

        // The lines of a text whose first baseline is `y`, and its character count.
        static std::tuple<std::vector<Line>, int> Layout(std::span<const byte> text, float y);

    private:
        static constexpr float Centre = 300.f;
        static constexpr byte Escape = static_cast<byte>('\\');
    };
}
