#pragma once

#include <functional>
#include <string>
#include <utility>

#include "core/Dictionary.h"
#include "game/scripts/PairHash.h"

namespace Mdk::Game::Scripts
{
    // The frames of arena textures that scripts animate (arena_texture_frame, opcode
    // 133 → texture_set_frame 0x42d9c0; godot-mdk docs/animated_textures.md). No clock drives
    // them: level 4's MEAT_3 and level 7's DANT_5 add 6 (12 once hit) frames per second to
    // M_COMM each tick, and stop when the device is destroyed.
    //
    //   mode 0: frame = value - 1      mode 1: frame += value · dt      else: frame += value
    //   frame wraps into [0, frames); drawn frame = trunc(frame)
    class AnimatedTextures
    {
    public:
        // How opcode 133's value sets the frame.
        enum class Mode { Absolute, PerSecond, Frames };

        using Key = std::pair<std::string, std::string>;

        explicit AnimatedTextures(std::function<int(const std::string&, const std::string&)> frameCount)
            : _frameCount(std::move(frameCount))
        {
        }

        // The script's mode number as a Mode.
        static Mode ModeOf(int mode);

        // Opcode 133 for an arena's texture, dt seconds a tick.
        void Set(const std::string& arena, const std::string& texture, Mode mode, float value, float dt);

        // The frame drawn for an arena's texture (0 until a script sets it).
        int FrameOf(const std::string& arena, const std::string& texture) const;

        // The textures animated so far, by arena and name, and their frames.
        const Dictionary<Key, float, PairHash>& Frames() const { return _frames; }

    private:
        std::function<int(const std::string&, const std::string&)> _frameCount;
        Dictionary<Key, float, PairHash> _frames;
    };
}
