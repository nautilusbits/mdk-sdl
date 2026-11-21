#include <string>

#include <doctest.h>

#include "game/scripts/AnimatedTextures.h"
#include "game/scripts/ScriptRuntime.h"

using namespace Mdk::Game::Scripts;

// Arena textures animated by opcode 133 (texture_set_frame 0x42d9c0).

namespace
{
    constexpr const char* Arena = "DANT_5";
    constexpr const char* Comm = "M_COMM";
    constexpr int CommFrames = 6;
    constexpr float Tick = ScriptRuntime::Tick;

    AnimatedTextures Create()
    {
        return AnimatedTextures([](const std::string&, const std::string& texture) { return texture == Comm ? CommFrames : 0; });
    }
}

TEST_CASE("AnimatedTextureTests.FramesPerSecondAddUpEachTick")
{
    auto textures = Create();
    // DANT_5's 6 frames a second: frame 3 after half a second.
    for (int tick = 0; tick < 16; tick++)
    {
        textures.Set(Arena, Comm, AnimatedTextures::Mode::PerSecond, 6.f, Tick);
    }

    CHECK(3 == textures.FrameOf(Arena, Comm));
    CHECK(0 == textures.FrameOf("MEAT_3", Comm));
}

TEST_CASE("AnimatedTextureTests.FramesWrapForward")
{
    auto textures = Create();
    textures.Set(Arena, Comm, AnimatedTextures::Mode::Frames, 5.f, Tick);
    textures.Set(Arena, Comm, AnimatedTextures::Mode::Frames, 2.f, Tick);
    CHECK(1 == textures.FrameOf(Arena, Comm));
    textures.Set(Arena, Comm, AnimatedTextures::Mode::Frames, -2.f, Tick);
    CHECK(5 == textures.FrameOf(Arena, Comm));
}

TEST_CASE("AnimatedTextureTests.AbsoluteFramesCountFromOne")
{
    auto textures = Create();
    textures.Set(Arena, Comm, AnimatedTextures::ModeOf(0), 4.f, Tick);
    CHECK(3 == textures.FrameOf(Arena, Comm));
}

TEST_CASE("AnimatedTextureTests.TexturesWithoutFramesStay")
{
    auto textures = Create();
    textures.Set(Arena, "D5_WALL", AnimatedTextures::Mode::Frames, 2.f, Tick);
    CHECK(0 == textures.Frames().Count());
}
