#include "game/level/MaterialResolver.h"

#include <array>
#include <limits>
#include <string_view>

#include "game/hd_textures/HdImage.h"
#include "game/mods/ModImages.h"

namespace Mdk::Game::Level
{
    using Engine::Render::Material;
    using Engine::Render::Pass;
    using Engine::Render::Shading;

    namespace
    {
        // C#'s name.StartsWith(PEN_) && int.TryParse(name[PEN_.Length..], out pen).
        bool TryPen(const std::string& name, std::string_view prefix, int& pen)
        {
            return Str::StartsWith(name, prefix) && Str::TryParseInt(std::string_view(name).substr(prefix.size()), pen);
        }
    }

    MaterialResolver::MaterialResolver(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Dti> dti,
        Engine::Render::Shading shading, std::shared_ptr<Mods::ModImages> mods)
        : _renderer(renderer), _dti(std::move(dti)), _shading(shading), _mods(std::move(mods))
    {
    }

    std::optional<MaterialResolver::Surface> MaterialResolver::Resolve(int value, std::span<const std::string> names,
        const std::shared_ptr<Formats::Palette>& palette, std::span<const std::shared_ptr<Formats::TextureArchive>> archives, Pass pass)
    {
        if (value < 0)
        {
            return Colour(-value, palette, pass);
        }

        // Index loops: a foreach over the interfaces would make an enumerator per call (every frame's pieces).
        const auto& name = names[value];
        for (size_t i = 0; i < archives.size(); i++)
        {
            const auto* texture = archives[i]->Textures.Find(name);
            if (texture == nullptr)
            {
                continue;
            }

            auto material = Material(TextureId(*texture), PaletteId(palette), Vector4::One(), (*texture)->FrameCount, pass, 0.f, ShadingOf(pass));
            Replace(material, name, **texture, *palette);
            _renderer.Prepare(material);
            return Surface{material, *texture};
        }

        for (size_t i = 0; i < archives.size(); i++)
        {
            const auto* index = archives[i]->Colors.Find(name);
            if (index != nullptr)
            {
                return Colour(*index, palette, pass);
            }
        }

        auto pen = 0;
        if (TryPen(name, PenPrefix, pen))
        {
            return Colour(pen, palette, pass);
        }

        return std::nullopt;
    }

    std::optional<MaterialResolver::Surface> MaterialResolver::Named(const std::string& name, const std::shared_ptr<Formats::Palette>& palette,
        std::span<const std::shared_ptr<Formats::TextureArchive>> archives, Pass pass)
    {
        std::array<std::string, 1> names{name};
        return Resolve(0, names, palette, archives, pass);
    }

    MaterialResolver::Surface MaterialResolver::Flat(Vector4 colour, Pass pass) const
    {
        auto material = Material::Flat(colour, pass);
        material.Shading = ShadingOf(pass);
        return Surface{material, nullptr};
    }

    MaterialResolver::Surface MaterialResolver::Image(const HdTextures::HdImage& image, Pass pass)
    {
        std::array<byte, 1> indices{ImageIndex};
        auto texture = _renderer.CreateIndexTexture(1, 1, indices);
        auto material = Material(texture, PaletteId(ImagePalette()), Vector4::One(), image.Frames, pass, 0.f, ShadingOf(pass));
        _renderer.Replace(material, image.Width, image.Height, image.Rgba);
        _renderer.Prepare(material);
        return Surface{material, nullptr};
    }

    const std::shared_ptr<Formats::Palette>& MaterialResolver::ImagePalette()
    {
        static const std::shared_ptr<Formats::Palette> imagePalette = WhitePalette();
        return imagePalette;
    }

    std::shared_ptr<Formats::Palette> MaterialResolver::WhitePalette()
    {
        auto palette = std::make_shared<Formats::Palette>();
        palette->Rgba.fill(std::numeric_limits<byte>::max());
        return palette;
    }

    bool MaterialResolver::IsDrawn(int value, std::span<const std::string> names, std::span<const std::shared_ptr<Formats::TextureArchive>> archives)
    {
        if (value < 0)
        {
            return IsDrawnColour(-value);
        }

        const auto& name = names[value];
        for (const auto& archive : archives)
        {
            if (archive->Textures.ContainsKey(name))
            {
                return true;
            }
        }

        for (const auto& archive : archives)
        {
            const auto* index = archive->Colors.Find(name);
            if (index != nullptr)
            {
                return IsDrawnColour(*index);
            }
        }

        auto pen = 0;
        return TryPen(name, PenPrefix, pen) && IsDrawnColour(pen);
    }

    std::optional<MaterialResolver::Surface> MaterialResolver::Colour(int index, const std::shared_ptr<Formats::Palette>& palette, Pass pass)
    {
        constexpr auto byteMax = static_cast<float>(std::numeric_limits<byte>::max());
        if (index < SpecialFirst)
        {
            const auto& rgba = palette->Rgba;
            auto colour = Vector4(rgba[index * 4], rgba[index * 4 + 1], rgba[index * 4 + 2], byteMax) * ByteToUnit;
            auto material = Material::Flat(colour, pass);
            material.Shading = ShadingOf(pass);
            return Surface{material, nullptr};
        }

        // Glass: the level's colour blended by its alpha, both faces (0x471290).
        if (index >= GlassFirst && index <= GlassLast)
        {
            const auto& glass = _dti->Glass[index - GlassFirst];
            auto colour = Vector4(static_cast<float>(glass[0]), static_cast<float>(glass[1]), static_cast<float>(glass[2]),
                static_cast<float>(glass[3])) * ByteToUnit;
            return Surface{Material::Flat(colour, Pass::Blended), nullptr};
        }

        if (index >= MirrorFirst && index <= MirrorLast)
        {
            return Surface{Material::Mirror((MirrorMiddle - index) * MirrorRowsPerValue), nullptr};
        }

        // NONE, PEN_ENV and RIPPLE aren't drawn (the Direct3D renderer skips RIPPLE).
        return std::nullopt;
    }

    void MaterialResolver::Replace(const Material& material, const std::string& name, const Formats::Texture& texture, const Formats::Palette& palette)
    {
        if (_mods == nullptr || material.Shading != Shading::Lit || !_looked.insert({&texture, &palette}).second)
        {
            return;
        }

        auto image = _mods->Texture(name, texture, palette);
        if (!image.has_value())
        {
            return;
        }

        _renderer.Replace(material, image->Width, image->Height, std::move(image->Rgba));
        ModFound++;
    }

    void MaterialResolver::Preload(const std::shared_ptr<Formats::Palette>& palette, std::span<const std::shared_ptr<Formats::TextureArchive>> archives)
    {
        PaletteId(palette);
        for (const auto& archive : archives)
        {
            for (const auto& [name, texture] : archive->Textures)
            {
                TextureId(texture);
            }
        }
    }

    void MaterialResolver::Refresh(const Formats::Texture& texture)
    {
        auto it = _textures.find(&texture);
        if (it != _textures.end())
        {
            _renderer.UpdateTexture(it->second, texture.Width, texture.Height * texture.FrameCount, texture.Indices);
        }
    }

    int MaterialResolver::TextureId(const std::shared_ptr<Formats::Texture>& texture)
    {
        auto it = _textures.find(texture.get());
        if (it != _textures.end())
        {
            return it->second;
        }

        auto id = _renderer.CreateIndexTexture(texture->Width, texture->Height * texture->FrameCount, texture->Indices);
        _textures[texture.get()] = id;
        return id;
    }

    int MaterialResolver::PaletteId(const std::shared_ptr<Formats::Palette>& palette)
    {
        auto it = _palettes.find(palette.get());
        if (it != _palettes.end())
        {
            return it->second;
        }

        auto id = _renderer.CreatePalette(palette->Rgba);
        _palettes[palette.get()] = id;
        return id;
    }
}
