#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/Glb.h"

namespace Mdk::Formats
{
    class Model;
    class Palette;
    class Texture;
    class TextureArchive;
}

namespace Mdk::Game::Mods
{
    // A model as a glTF scene for modders (Glb): a node per part named as the loader expects
    // (ModelSwap.PartName), in the rest pose, glTF's axes; one primitive per drawn material. Materials
    // keep the original names; a texture's first frame through the palette is embedded, colours are
    // base colours (glTF's linear).
    //   part ─► node "HEAD" ─► per material: corners welded by (vertex, UV), UVs in 0-1 of the texture
    struct ModelExport
    {
        static Formats::Glb::Scene Scene(const Formats::Model& model, const Formats::Palette& palette,
            const std::vector<std::shared_ptr<Formats::TextureArchive>>& archives);

    private:
        static constexpr const char* PenPrefix = "PEN_";
        static constexpr int Corners = 3;
        static constexpr int Channels = 4;
        static constexpr float ByteToUnit = 1.f / 255.f;
        // Special colours (glass, mirrors) have no palette colour: grey.
        static constexpr Vector4 SpecialColour{0.5f, 0.5f, 0.5f, 1.f};

        // A texture's first frame embedded; a colour (archive, PEN_n) as the base colour.
        static Formats::Glb::Material Material(const std::string& name, const Formats::Palette& palette,
            const std::vector<std::shared_ptr<Formats::TextureArchive>>& archives);

        // The first archive's texture of that name (the resolver's order).
        static std::shared_ptr<Formats::Texture> Find(const std::string& name,
            const std::vector<std::shared_ptr<Formats::TextureArchive>>& archives);
    };
}
