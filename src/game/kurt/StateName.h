#pragma once

#include <string_view>

#include "game/kurt/Kurt.h"

namespace Mdk::Game::Kurt
{
    // C#'s Kurt.State.ToString(): the member's name.
    inline std::string_view StateName(Kurt::State state)
    {
        using enum Kurt::State;
        switch (state)
        {
        case Still: return "Still";
        case Idle: return "Idle";
        case Run: return "Run";
        case Side: return "Side";
        case Turn: return "Turn";
        case Jump: return "Jump";
        case RunJump: return "RunJump";
        case Fall: return "Fall";
        case Chute: return "Chute";
        case Land: return "Land";
        case Shot: return "Shot";
        case RunFire: return "RunFire";
        case Throw: return "Throw";
        case Knocked: return "Knocked";
        case GetUp: return "GetUp";
        case Dead: return "Dead";
        case Slip: return "Slip";
        case Slide: return "Slide";
        case SlideFast: return "SlideFast";
        case SlideBrake: return "SlideBrake";
        case Hang: return "Hang";
        case HardLand: return "HardLand";
        case RollLeft: return "RollLeft";
        case RollRight: return "RollRight";
        case HelmetOn: return "HelmetOn";
        case HelmetOff: return "HelmetOff";
        }

        return "";
    }
}
