#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Mdk
{
    // Encoding.Latin1 on UTF-8 std::string: the code points, and the bytes (those above 0xFF are '?').
    struct Latin1
    {
        static constexpr char32_t Last = 0xFF;
        static constexpr std::uint8_t Unknown = '?';

        // The code point at `i` (UTF-8), `i` moved past it; a bad byte is taken as itself.
        static char32_t Next(std::string_view s, size_t& i)
        {
            auto lead = static_cast<std::uint8_t>(s[i++]);
            auto extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
            if (extra == 0 || i + extra > s.size())
            {
                return lead;
            }

            char32_t c = lead & (0x3F >> extra);
            for (auto k = 0; k < extra; k++)
            {
                c = (c << 6) | (static_cast<std::uint8_t>(s[i++]) & 0x3F);
            }

            return c;
        }

        // Appends a code point as UTF-8.
        static void Append(std::string& s, char32_t c)
        {
            if (c < 0x80)
            {
                s += static_cast<char>(c);
                return;
            }

            if (c < 0x800)
            {
                s += static_cast<char>(0xC0 | (c >> 6));
                s += static_cast<char>(0x80 | (c & 0x3F));
                return;
            }

            if (c < 0x10000)
            {
                s += static_cast<char>(0xE0 | (c >> 12));
                s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                s += static_cast<char>(0x80 | (c & 0x3F));
                return;
            }

            s += static_cast<char>(0xF0 | (c >> 18));
            s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (c & 0x3F));
        }

        // Removes the last code point (C#: s[..^1] on a Latin-1 string).
        static void RemoveLast(std::string& s)
        {
            while (!s.empty() && (static_cast<std::uint8_t>(s.back()) & 0xC0) == 0x80)
            {
                s.pop_back();
            }

            if (!s.empty())
            {
                s.pop_back();
            }
        }

        // The number of code points (the length of a Latin-1 string).
        static size_t Length(std::string_view s)
        {
            size_t count = 0;
            for (size_t i = 0; i < s.size();)
            {
                Next(s, i);
                count++;
            }

            return count;
        }

        // Encoding.Latin1.GetBytes: one byte a code point; `bytes` holds at least Length(s).
        static size_t GetBytes(std::string_view s, std::span<std::uint8_t> bytes)
        {
            size_t count = 0;
            for (size_t i = 0; i < s.size();)
            {
                auto c = Next(s, i);
                bytes[count++] = c <= Last ? static_cast<std::uint8_t>(c) : Unknown;
            }

            return count;
        }
    };
}
