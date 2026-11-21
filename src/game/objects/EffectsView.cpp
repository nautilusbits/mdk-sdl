#include "game/objects/EffectsView.h"

#include <algorithm>
#include <limits>

#include "core/DotNet.h"
#include "game/level/ArenaBand.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/Effects.h"
#include "game/scripts/Items.h"
#include "game/scripts/ScriptMath.h"
#include "game/scripts/ScriptRuntime.h"
#include "game/scripts/Twister.h"

namespace Mdk::Game::Objects
{
    using Engine::Render::DepthBand;
    using Engine::Render::Material;
    using Engine::Render::Pass;
    using Engine::Render::Vertex;
    using Level::ArenaBand;
    using Level::MaterialResolver;

    EffectsView::EffectsView(Engine::Render::Renderer& renderer, std::shared_ptr<MaterialResolver> resolver, Level::LevelData& level,
        Engine::Render::Shading sprites)
        : _renderer(renderer), _resolver(std::move(resolver)), _level(level), _spritesShading(sprites),
          _mesh(renderer.CreateDynamicMesh(MaxVertices))
    {
    }

    void EffectsView::Draw(Scripts::ScriptRuntime& scripts, const Engine::Render::View& camera, float tick, const std::string& cameraArena)
    {
        _vertices.clear();
        _draws.clear();

        // A look-at view times a perspective: its columns hold the camera's right, up and forward.
        const auto& m = camera.ViewProjection;
        auto forward = Vector3::Normalize(Vector3(m.M14, m.M24, m.M34));
        auto right = Vector3::Normalize(Vector3(m.M11, m.M21, m.M31));
        auto billboardUp = Vector3::Cross(right, forward);

        AddPieces(scripts.Debris->Pieces(), forward, tick, cameraArena);
        const auto& effects = scripts.Effects->All();
        for (size_t i = 0; i < effects.size(); i++)
        {
            const auto& effect = effects[i];
            auto sprite = Sprite(effect->Arena, effect->Texture);
            if (sprite.has_value())
            {
                auto texel = effect->Scale / Scripts::Effects::TexelsPerUnit;
                AddQuad(effect->PositionAt(tick), right * texel, billboardUp * texel, *sprite, effect->Frame(), ArenaBand::Of(effect->Arena, cameraArena));
            }
        }

        // spawn_box sprites: the box's width and height, the image stretched (0x43b2b0: box sizes
        // 0x316 and 0x31e, e.g. a 64 × 36 BONEFLC shown 9 × 6), playing at 30 frames per second.
        for (const auto& box : scripts.Boxes())
        {
            if (box->Dead || !box->Visible || box->Model == nullptr)
            {
                continue;
            }

            auto sprite = Sprite(box->Arena, box->Model->Name);
            if (!sprite.has_value())
            {
                continue;
            }

            const auto& s = *sprite;
            auto size = Scripts::ScriptMath::Size(box->Model->Bounds);
            auto texelX = size.X / std::max(s.Texture->Width, 1);
            auto texelZ = size.Z / std::max(s.Texture->Height, 1);
            AddQuad(box->Position, right * texelX, billboardUp * texelZ, s, scripts.TickCount() % s.Material.FrameCount, ArenaBand::Of(box->Arena, cameraArena));
        }

        const auto& twisters = scripts.Items->Twisters();
        for (size_t i = 0; i < twisters.size(); i++)
        {
            AddRibbon(*twisters[i], ArenaBand::Of(twisters[i]->Arena, cameraArena));
        }

        if (_vertices.empty())
        {
            return;
        }

        _renderer.UpdateMesh(_mesh, _vertices);
        for (const auto& draw : _draws)
        {
            _renderer.Band = draw.Band;
            _renderer.Draw(_mesh, draw.First, draw.Count, draw.Material, draw.Frame);
        }

        _renderer.Band = DepthBand::Front;
    }

    void EffectsView::AddPieces(std::span<const std::shared_ptr<Scripts::Debris::Piece>> pieces, Vector3 forward, float tick, const std::string& cameraArena)
    {
        _pieceOrder.clear();
        _pieceBatch.clear();
        for (size_t p = 0; p < pieces.size(); p++)
        {
            const auto& piece = *pieces[p];
            auto look = LookOf(piece.Arena);
            auto center = piece.CenterAt(tick);
            auto orientation = piece.OrientationAt(tick);
            for (size_t t = 0; t < piece.Corners.size() / 3; t++)
            {
                auto a = Vector3::Transform(piece.Corners[t * 3], orientation);
                auto b = Vector3::Transform(piece.Corners[t * 3 + 1], orientation);
                auto c = Vector3::Transform(piece.Corners[t * 3 + 2], orientation);
                auto surface = piece.IsSpark() ? std::optional(SparkSurface(piece, *look.Palette, a, b, c, forward)) : PieceSurface(piece, static_cast<int>(t), look);
                if (!surface.has_value())
                {
                    continue;
                }

                const auto& s = *surface;
                auto& vertices = PieceBatch(s.Material, ArenaBand::Of(piece.Arena, cameraArena));

                // UVs are in texels of the texture.
                auto scale = s.Texture == nullptr ? Vector2::Zero() : Vector2(1.f / s.Texture->Width, 1.f / s.Texture->Height);
                auto uv = [&](size_t k) { return piece.IsSpark() ? Vector2::Zero() : piece.Uvs[t * 3 + k] * scale; };
                vertices.push_back(Vertex(center + a, uv(0)));
                vertices.push_back(Vertex(center + b, uv(1)));
                vertices.push_back(Vertex(center + c, uv(2)));
            }
        }

        for (const auto& key : _pieceOrder)
        {
            const auto& vertices = _pieceVertices[_pieceBatch.at(key)];
            auto count = std::min(static_cast<int>(vertices.size()), MaxVertices - static_cast<int>(_vertices.size())) / 3 * 3;
            if (count == 0)
            {
                return;
            }

            _draws.push_back(Queued{static_cast<int>(_vertices.size()), count, key.Material, 0, key.Band});
            _vertices.insert(_vertices.end(), vertices.begin(), vertices.begin() + count);
        }
    }

    std::vector<Vertex>& EffectsView::PieceBatch(const Material& material, DepthBand band)
    {
        PieceKey key{material, band};
        auto found = _pieceBatch.find(key);
        if (found != _pieceBatch.end())
        {
            return _pieceVertices[found->second];
        }

        auto index = static_cast<int>(_pieceOrder.size());
        _pieceOrder.push_back(key);
        _pieceBatch[key] = index;
        if (index == static_cast<int>(_pieceVertices.size()))
        {
            _pieceVertices.emplace_back();
        }

        _pieceVertices[index].clear();
        return _pieceVertices[index];
    }

    std::optional<MaterialResolver::Surface> EffectsView::PieceSurface(const Scripts::Debris::Piece& piece, int triangle, const ObjectView::Look& look)
    {
        return _resolver->Resolve(piece.Materials[triangle], *piece.Names, look.Palette, look.Archives, Pass::DoubleSided);
    }

    MaterialResolver::Surface EffectsView::SparkSurface(const Scripts::Debris::Piece& piece, const Formats::Palette& palette,
        Vector3 a, Vector3 b, Vector3 c, Vector3 forward)
    {
        auto normal = Vector3::Cross(b - a, c - a);
        auto facing = normal == Vector3::Zero() ? 0.f : MathF::Abs(Vector3::Dot(Vector3::Normalize(normal), forward));
        auto index = Math::Clamp(ToInt(MathF::Round(piece.ColourBase + piece.ColourRange * facing)), 0, LastColour);
        const auto& rgba = palette.Rgba;
        auto colour = Vector4(rgba[index * 4], rgba[index * 4 + 1], rgba[index * 4 + 2], std::numeric_limits<byte>::max()) * ByteToUnit;
        return MaterialResolver::Surface{Material::Flat(colour, Pass::DoubleSided), nullptr};
    }

    std::optional<MaterialResolver::Surface> EffectsView::Sprite(const std::string& arena, const std::string& texture)
    {
        auto key = std::make_pair(arena, texture);
        auto known = _sprites.find(key);
        if (known != _sprites.end())
        {
            return known->second;
        }

        auto look = LookOf(arena);
        _spriteName[0] = texture;
        auto surface = _resolver->Resolve(0, _spriteName, look.Palette, look.Archives, Pass::DoubleSided);
        std::optional<MaterialResolver::Surface> sprite;
        if (surface.has_value() && surface->Texture != nullptr)
        {
            sprite = *surface;
            sprite->Material.Shading = _spritesShading;
        }

        if (sprite.has_value())
        {
            _renderer.Prepare(sprite->Material);
        }

        return _sprites[key] = sprite;
    }

    void EffectsView::AddQuad(Vector3 center, Vector3 right, Vector3 up, const MaterialResolver::Surface& sprite, int frame, DepthBand band)
    {
        if (static_cast<int>(_vertices.size()) + QuadVertices > MaxVertices)
        {
            return;
        }

        auto halfWidth = right * (sprite.Texture->Width * 0.5f);
        auto halfHeight = up * (sprite.Texture->Height * 0.5f);
        auto corner = [&](float x, float y) { return Vertex(center + halfWidth * (x * 2.f - 1.f) + halfHeight * (1.f - y * 2.f), Vector2(x, y)); };
        _draws.push_back(Queued{static_cast<int>(_vertices.size()), QuadVertices, sprite.Material, frame, band});
        _vertices.push_back(corner(0, 0));
        _vertices.push_back(corner(1, 0));
        _vertices.push_back(corner(1, 1));
        _vertices.push_back(corner(0, 0));
        _vertices.push_back(corner(1, 1));
        _vertices.push_back(corner(0, 1));
    }

    void EffectsView::AddRibbon(const Scripts::Twister& twister, DepthBand band)
    {
        auto& corners = _ribbon;
        twister.Ribbon->Triangles(corners);
        if (corners.empty() || _vertices.size() + corners.size() > MaxVertices)
        {
            return;
        }

        auto sprite = Sprite(twister.Arena, std::string(Scripts::Ribbon::Texture));
        if (!sprite.has_value())
        {
            return;
        }

        const auto& s = *sprite;
        auto scale = Vector2(1.f / s.Texture->Width, 1.f / s.Texture->Height);
        _draws.push_back(Queued{static_cast<int>(_vertices.size()), static_cast<int>(corners.size()), s.Material, 0, band});
        for (const auto& corner : corners)
        {
            _vertices.push_back(Vertex(corner.Position, corner.Uv * scale));
        }
    }

    ObjectView::Look EffectsView::LookOf(const std::string& name)
    {
        auto found = _looks.find(name);
        if (found != _looks.end())
        {
            return found->second;
        }

        auto arena = _level.ArenaNamed(name);
        auto look = arena != nullptr
            ? ObjectView::Look{_level.PaletteOf(*arena), _level.ArchivesOf(*arena)}
            : ObjectView::Look{_level.Dti->Palette, {_level.LevelTextures}};
        return _looks[name] = look;
    }
}
