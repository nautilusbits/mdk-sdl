#pragma once

#include <cstddef>
#include <functional>
#include <utility>

namespace Mdk::Game::Scripts
{
    // A hash for the C# tuple keys ((string, int), (string, string)…) of dictionaries; std::hash
    // has none for pairs. Iteration order doesn't depend on it (Mdk::Dictionary keeps .NET's).
    struct PairHash
    {
        template <typename A, typename B>
        std::size_t operator()(const std::pair<A, B>& p) const
        {
            const auto a = std::hash<A>{}(p.first);
            return a ^ (std::hash<B>{}(p.second) + 0x9e3779b9 + (a << 6) + (a >> 2));
        }
    };
}
