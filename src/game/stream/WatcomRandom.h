#pragma once

#include "core/DotNet.h"

namespace Mdk::Game::Stream
{
    // Watcom's rand() (0x4794dd): state = state × 0x41C64E6D + 0x3039, returns
    // bits 16-30 (0-0x7FFF). The stream draws its tube, lights and damage from it.
    class WatcomRandom
    {
    public:
        // The C library's seed when srand wasn't called.
        static constexpr uint DefaultSeed = 1;
        static constexpr int Max = 0x7FFF;

        explicit WatcomRandom(uint seed = DefaultSeed) : _state(seed) {}

        int Next();

    private:
        static constexpr uint Multiplier = 0x41C64E6D;
        static constexpr uint Increment = 0x3039;
        static constexpr int Shift = 16;

        uint _state;
    };
}
