#pragma once

#include "core/Numerics.h"

namespace Mdk::Game::Audio
{
    // Something a sound follows (an object): where a point of it is now.
    class ISoundSource
    {
    public:
        virtual ~ISoundSource() = default;

        // The world position of offset in the source's own frame.
        virtual Vector3 SoundPosition(Vector3 offset) = 0;
    };
}
