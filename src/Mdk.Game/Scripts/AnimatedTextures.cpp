#include "Mdk.Game/Scripts/AnimatedTextures.h"

#include <cmath>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Game::Scripts
{
    AnimatedTextures::Mode AnimatedTextures::ModeOf(int mode)
    {
        switch (mode)
        {
            case 0:
                return Mode::Absolute;
            case 1:
                return Mode::PerSecond;
            default:
                return Mode::Frames;
        }
    }

    void AnimatedTextures::Set(const std::string& arena, const std::string& texture, Mode mode, float value, float dt)
    {
        auto count = _frameCount(arena, texture);
        if (count < 1)
        {
            return;
        }

        auto current = _frames.GetValueOrDefault(Key{arena, texture});
        float target;
        switch (mode)
        {
            case Mode::Absolute:
                target = value - 1.0f;
                break;
            case Mode::PerSecond:
                target = current + value * dt;
                break;
            default:
                target = current + value;
                break;
        }

        // Into [0, count): a forward loop, never ping-pong.
        target = std::fmod(target, static_cast<float>(count));
        if (target < 0.0f)
        {
            target += count;
        }

        _frames[Key{arena, texture}] = target;
    }

    int AnimatedTextures::FrameOf(const std::string& arena, const std::string& texture) const
    {
        return ToInt(_frames.GetValueOrDefault(Key{arena, texture}));
    }
}
