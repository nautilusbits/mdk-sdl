#include "game/stream/WatcomRandom.h"

namespace Mdk::Game::Stream
{
    int WatcomRandom::Next()
    {
        _state = _state * Multiplier + Increment;
        return static_cast<int>(_state >> Shift) & Max;
    }
}
