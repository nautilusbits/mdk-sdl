#pragma once

#include <memory>
#include <string>

namespace Mdk::Engine::Render
{
    class Renderer;
}

namespace Mdk::Formats
{
    class Palette;
    class Texture;
}

namespace Mdk::Game::Mods
{
    class ModImages;

    // The canvas's 2D images (HUD, menus, fonts): an index texture, and a mod's image for it
    // when one has it (ModImages.Image, drawn by the enhanced look's canvas at the window's
    // resolution; the original look keeps the index texture).
    struct CanvasImages
    {
        // A 2D image's index texture, its mod image given to the renderer.
        static int Create(Engine::Render::Renderer& renderer, const std::shared_ptr<ModImages>& mods, const std::string& name,
            const Formats::Texture& texture, const Formats::Palette& palette, int paletteId);

        // Gives an existing index texture its mod image, if any.
        static void Replace(Engine::Render::Renderer& renderer, const std::shared_ptr<ModImages>& mods, const std::string& name,
            const Formats::Texture& texture, const Formats::Palette& palette, int textureId, int paletteId);

        // An animation's frame's name in mods (PICKUPS_3).
        static std::string FrameName(const std::string& animation, int frame)
        {
            return animation + "_" + std::to_string(frame);
        }
    };
}
