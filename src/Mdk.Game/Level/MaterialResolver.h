#pragma once

#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <functional>
#include <utility>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/Dti.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Formats/TextureArchive.h"

namespace Mdk::Game::HdTextures
{
    struct HdImage;
}

namespace Mdk::Game::Mods
{
    class ModImages;
}

namespace Mdk::Game::Level
{
    // Hashes a renderer material by value, for the C# dictionaries keyed by it (C# records hash by value).
    struct MaterialHash
    {
        size_t operator()(const Engine::Render::Material& m) const
        {
            size_t h = std::hash<int>()(m.Texture);
            auto mix = [&h](size_t v) { h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2); };
            mix(std::hash<int>()(m.Palette));
            mix(std::hash<float>()(m.Colour.X));
            mix(std::hash<float>()(m.Colour.Y));
            mix(std::hash<float>()(m.Colour.Z));
            mix(std::hash<float>()(m.Colour.W));
            mix(std::hash<int>()(m.FrameCount));
            mix(std::hash<int>()(static_cast<int>(m.Pass)));
            mix(std::hash<float>()(m.RowShift));
            mix(std::hash<int>()(static_cast<int>(m.Shading)));
            mix(std::hash<int>()(static_cast<int>(m.Blend)));
            mix(std::hash<int>()(m.DepthLayer));
            return h;
        }
    };

    // Turns MDK material references (texture names, palette colours, special values) into
    // renderer materials, creating each texture and palette on the GPU once. Opaque surfaces take
    // shading (the look); glass and mirrors keep the original's. Lit textures take
    // a mod's image when mods has one (ModImages: HD textures too).
    //   value < 0          palette colour -value
    //   value >= 0         material name: texture, archive colour, PEN_n, NONE
    //   colour 256-1028    special: glass (GLASS1-4), mirrors, NONE, PEN_ENV, RIPPLE
    class MaterialResolver
    {
    public:
        // A resolved surface and the texture whose texels its UVs count (null when flat).
        struct Surface
        {
            Engine::Render::Material Material;
            std::shared_ptr<Formats::Texture> Texture;

            bool operator==(const Surface&) const = default;
        };

        MaterialResolver(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Dti> dti,
            Engine::Render::Shading shading = Engine::Render::Shading::Original, std::shared_ptr<Mods::ModImages> mods = nullptr);

        // Textures through a palette looked up in the mods, and found.
        int ModLooked() const { return static_cast<int>(_looked.size()); }
        int ModFound = 0;

        // The surface of a triangle's material value, or null when it isn't drawn.
        std::optional<Surface> Resolve(int value, std::span<const std::string> names, const std::shared_ptr<Formats::Palette>& palette,
            std::span<const std::shared_ptr<Formats::TextureArchive>> archives, Engine::Render::Pass pass);

        // The surface of a material name (a mod's model naming an original material), or null.
        std::optional<Surface> Named(const std::string& name, const std::shared_ptr<Formats::Palette>& palette,
            std::span<const std::shared_ptr<Formats::TextureArchive>> archives, Engine::Render::Pass pass);

        // A flat colour (sRGB) of the look's shading (a mod's model).
        Surface Flat(Vector4 colour, Engine::Render::Pass pass) const;

        // A mod's image as a surface of its own: a 1 x 1 index texture stands for it, the
        // renderer draws the image (the enhanced look's colour texture).
        Surface Image(const HdTextures::HdImage& image, Engine::Render::Pass pass);

        // Whether a material value is drawn (Resolve isn't null), without the GPU.
        static bool IsDrawn(int value, std::span<const std::string> names, std::span<const std::shared_ptr<Formats::TextureArchive>> archives);

        // Puts every texture of archives and palette on the GPU (a level's load: no frame
        // creates any). The enhanced look's colour textures are made as surfaces resolve (every
        // texture through every palette would take seconds).
        void Preload(const std::shared_ptr<Formats::Palette>& palette, std::span<const std::shared_ptr<Formats::TextureArchive>> archives);

        // Uploads a texture's pixels again if it's on the GPU (a bullet hole changed them).
        void Refresh(const Formats::Texture& texture);

    private:
        static constexpr int SpecialFirst = 256;
        static constexpr int MirrorFirst = 990;
        static constexpr int MirrorLast = 1010;
        static constexpr int GlassFirst = 1024;
        static constexpr int GlassLast = 1027;
        static constexpr const char* PenPrefix = "PEN_";
        static constexpr float ByteToUnit = 1.f / 255.f;
        // A mirror's panorama row is 8 rows per value from MIRRMED (1000): MIRRLOW (990) 80
        // lower, MIRRHIGH (1010) 80 higher.
        static constexpr int MirrorMiddle = 1000;
        static constexpr float MirrorRowsPerValue = 8.f;
        // The index of Image's texture: opaque white in ImagePalette.
        static constexpr byte ImageIndex = 1;

        Engine::Render::Renderer& _renderer;
        std::shared_ptr<Formats::Dti> _dti;
        Engine::Render::Shading _shading;
        std::shared_ptr<Mods::ModImages> _mods;

        // Keyed by identity, as the C# reference keys.
        std::unordered_map<const Formats::Texture*, int> _textures;
        std::unordered_map<const Formats::Palette*, int> _palettes;
        // The textures and palettes looked up in mods (once each).
        std::set<std::pair<const Formats::Texture*, const Formats::Palette*>> _looked;

        static const std::shared_ptr<Formats::Palette>& ImagePalette();

        static std::shared_ptr<Formats::Palette> WhitePalette();

        // Palette colours, glass and mirrors are drawn; other specials aren't.
        static bool IsDrawnColour(int index)
        {
            return index < SpecialFirst || (index >= GlassFirst && index <= GlassLast) || (index >= MirrorFirst && index <= MirrorLast);
        }

        std::optional<Surface> Colour(int index, const std::shared_ptr<Formats::Palette>& palette, Engine::Render::Pass pass);

        // A lit texture's mod image, given to the renderer the first time it's seen.
        void Replace(const Engine::Render::Material& material, const std::string& name, const Formats::Texture& texture, const Formats::Palette& palette);

        Engine::Render::Shading ShadingOf(Engine::Render::Pass pass) const
        {
            return pass == Engine::Render::Pass::Solid || pass == Engine::Render::Pass::DoubleSided ? _shading : Engine::Render::Shading::Original;
        }

        int TextureId(const std::shared_ptr<Formats::Texture>& texture);

        int PaletteId(const std::shared_ptr<Formats::Palette>& palette);
    };
}
