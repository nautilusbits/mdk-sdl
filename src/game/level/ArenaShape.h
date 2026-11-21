#pragma once

#include <optional>
#include <utility>

#include "core/Numerics.h"
#include "formats/Arena.h"

namespace Mdk::Game::Level
{
    // Whether an arena is under a roof (indoors) or under the sky.
    enum class Cover { Open, Covered };

    // An arena's faces as the enhanced look's light sees them: how much ceiling there is over
    // its floors, and the faces' mean way. Faces count by area; their outward normal is the negated
    // cross product of their edges (MDK winds a floor's corners clockwise seen from above).
    //   ceilings (normal down) ─┐
    //                           ├─ ceiling / floor ≥ 1/2: covered
    //   floors   (normal up)   ─┘
    struct ArenaShape
    {
        static Cover CoverOf(const Formats::Arena& arena);

        // The faces' mean up component (-1 all ceilings, 1 all floors).
        static float MeanUp(const Formats::Arena& arena);

        // How much the faces turn to a light shining along light, on mean
        // (0 to 1; faces turned away count 0).
        static float MeanFacing(const Formats::Arena& arena, Vector3 light);

    private:
        // Faces turned up (or down) more than this are floors (or ceilings).
        static constexpr float Horizontal = 0.5f;
        // Ceilings over this share of the floors' area make a roof.
        static constexpr float CoveredShare = 0.5f;

        // A drawn triangle's outward normal and area; null when it has none or only blocks Kurt.
        static std::optional<std::pair<Vector3, float>> Face(const Formats::Arena& arena, int triangle);
    };
}
