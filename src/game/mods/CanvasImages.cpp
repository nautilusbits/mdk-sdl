#include "game/mods/CanvasImages.h"

#include "engine/render/Renderer.h"
#include "formats/Palette.h"
#include "formats/Texture.h"
#include "game/mods/ModImages.h"

namespace Mdk::Game::Mods
{
    int CanvasImages::Create(Engine::Render::Renderer& renderer, const std::shared_ptr<ModImages>& mods, const std::string& name,
        const Formats::Texture& texture, const Formats::Palette& palette, int paletteId)
    {
        const auto id = renderer.CreateIndexTexture(texture.Width, texture.Height, texture.Indices);
        Replace(renderer, mods, name, texture, palette, id, paletteId);
        return id;
    }

    void CanvasImages::Replace(Engine::Render::Renderer& renderer, const std::shared_ptr<ModImages>& mods, const std::string& name,
        const Formats::Texture& texture, const Formats::Palette& palette, int textureId, int paletteId)
    {
        if (mods == nullptr)
        {
            return;
        }

        if (auto image = mods->Image(name, texture, palette))
        {
            renderer.ReplaceImage(textureId, paletteId, image->Width, image->Height, std::move(image->Rgba));
        }
    }
}
