#pragma once

// Enum.TryParse / Enum.ToString / Enum.IsDefined for enums numbered 0..N-1, given their members'
// names in order:
//   static constexpr std::array<std::string_view, 3> Names{"Easy", "Normal", "Hard"};
//   EnumNames::TryParse("Hard", Names, difficulty);   // true, Difficulty::Hard
//   EnumNames::Name(Difficulty::Normal, Names);       // "Normal"

#include <array>
#include <string>
#include <string_view>

#include "Mdk.Core/DotNet.h"

namespace Mdk::EnumNames
{
    // Whether names match whatever their letters' case (Enum.Parse's ignoreCase).
    enum class Case { Sensitive, Ignore };

    // A member's name, or a number as C# accepts (its value, defined or not); spaces around are
    // ignored.
    template <typename E, size_t N>
    bool TryParse(std::string_view text, const std::array<std::string_view, N>& names, E& value,
        Case match = Case::Sensitive)
    {
        auto trimmed = Str::Trim(text);
        for (size_t i = 0; i < N; i++)
        {
            auto same = match == Case::Ignore ? Str::EqualsIgnoreCase(trimmed, names[i]) : trimmed == names[i];
            if (same)
            {
                value = static_cast<E>(i);
                return true;
            }
        }

        int number = 0;
        if (!Str::TryParseInt(trimmed, number))
        {
            return false;
        }

        value = static_cast<E>(number);
        return true;
    }

    template <typename E, size_t N>
    bool IsDefined(E value, const std::array<std::string_view, N>&)
    {
        auto number = static_cast<long long>(value);
        return number >= 0 && number < static_cast<long long>(N);
    }

    // The member's name, or its number when it has none.
    template <typename E, size_t N>
    std::string Name(E value, const std::array<std::string_view, N>& names)
    {
        if (!IsDefined(value, names))
        {
            return std::to_string(static_cast<long long>(value));
        }

        return std::string(names[static_cast<size_t>(value)]);
    }
}
