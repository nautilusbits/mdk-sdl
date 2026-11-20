#include "Mdk.Game/Menu/HeadsView.h"

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Formats/TextureArchive.h"
#include "Mdk.Game/Angles.h"
#include "Mdk.Game/Kurt/FollowCamera.h"
#include "Mdk.Game/Level/MaterialResolver.h"
#include "Mdk.Game/Menu/Ui.h"

namespace Mdk::Game::Menu
{
    using Engine::Render::Material;
    using Engine::Render::Pass;
    using Engine::Render::Vertex;

    HeadsView::HeadsView(Engine::Render::Renderer& renderer, const Formats::Bni& bni,
        const std::function<std::vector<byte>(std::span<const byte>)>& pageRgb, const Formats::MdkData& data)
        : _renderer(renderer)
    {
        if (!bni.Has(ModelName) || !bni.Has(PaletteName))
        {
            return;
        }

        const auto& bytes = *bni.Bytes();
        auto model = Formats::Model::Parse(ModelName, bytes, bni.Entries().At(ModelName).Offset, Formats::Model::Header::NoFlags);
        auto offset = bni.Entries().At(PaletteName).Offset;
        std::vector<byte> rgb(bytes.begin() + offset, bytes.begin() + offset + PaletteBytes);
        auto palette = Formats::Palette::FromRgb(pageRgb(rgb));
        auto archive = Formats::TextureArchive::Load(data.PathOf(Textures));
        _mesh = Build(*model, *palette, *archive);
    }

    void HeadsView::Update(int shown, float delta)
    {
        for (auto i = 0; i < Math::Min(shown, MaxHeads); i++)
        {
            _angles[i] += Spin * delta;
        }
    }

    void HeadsView::Draw(int shown, int count, RectangleF area)
    {
        if (_mesh < 0 || shown <= 0)
        {
            return;
        }

        auto fieldOfView = RadiansToDegrees(2.f * MathF::Atan(ScreenView::Size.Y / 2.f / Focal));
        auto view = Kurt::CameraMath::View(Vector3::Zero(), Vector3::UnitY(), Vector3::UnitZ(), fieldOfView,
            area.Width / area.Height, Near, Far);
        _renderer.BeginInset(view, area, Engine::Render::InsetContent::Own, Engine::Render::InsetLayer::UnderCanvas);
        auto places = Places(shown, count);
        for (size_t i = 0; i < places.size(); i++)
        {
            auto world = Matrix4x4::CreateScale(Scale) * Matrix4x4::CreateRotationZ(DegreesToRadians(_angles[i]))
                * Matrix4x4::CreateTranslation(places[i]);
            for (const auto& batch : _batches)
            {
                _renderer.Draw(_mesh, batch.First, batch.Count, batch.Material, 0, world);
            }
        }

        _renderer.EndInset();
    }

    std::vector<Vector3> HeadsView::Places(int shown, int count)
    {
        std::vector<Vector3> places;
        auto perRow = count < OneRow ? count : count <= MaxHeads ? (count + 1) / 2 : MaxPerRow;
        shown = Math::Min(shown, perRow * 2);
        for (auto i = 0; i < shown; i++)
        {
            auto row = i / perRow;
            auto k = i % perRow;
            auto m = Math::Min(perRow, shown - row * perRow);
            auto x = RowWidth * (k + 1) / (m + 1) - RowWidth / 2.f;
            places.push_back(Vector3(x, Distance, row == 0 ? FirstRowZ : SecondRowZ));
        }

        return places;
    }

    int HeadsView::Build(const Formats::Model& model, const Formats::Palette& palette, const Formats::TextureArchive& archive)
    {
        auto paletteId = _renderer.CreatePalette(palette.Rgba);
        std::unordered_map<const Formats::Texture*, int> textures;
        Mdk::Dictionary<Material, std::vector<Vertex>, Level::MaterialHash> bySurface;
        for (const auto& part : model.PartList)
        {
            for (size_t t = 0; t < part->TriangleMaterials.size(); t++)
            {
                auto surface = Surface(part->TriangleMaterials[t], model, palette, archive, paletteId, textures);
                if (!surface.has_value())
                {
                    continue;
                }

                const auto& [material, texture] = *surface;
                auto scale = texture == nullptr
                    ? Vector2::Zero()
                    : Vector2(1.f / static_cast<float>(texture->Width), 1.f / static_cast<float>(texture->Height));
                auto& vertices = bySurface[material];
                for (size_t k = 0; k < 3; k++)
                {
                    vertices.push_back(Vertex(part->Vertices[part->TriangleIndices[t * 3 + k]], part->TriangleUvs[t * 3 + k] * scale));
                }
            }
        }

        std::vector<Vertex> all;
        for (const auto& [material, vertices] : bySurface)
        {
            _batches.push_back(Batch{static_cast<int>(all.size()), static_cast<int>(vertices.size()), material});
            all.insert(all.end(), vertices.begin(), vertices.end());
        }

        return all.empty() ? -1 : _renderer.CreateMesh(all);
    }

    std::optional<std::tuple<Material, std::shared_ptr<Formats::Texture>>> HeadsView::Surface(int value,
        const Formats::Model& model, const Formats::Palette& palette, const Formats::TextureArchive& archive, int paletteId,
        std::unordered_map<const Formats::Texture*, int>& textures)
    {
        auto materials = static_cast<int>(model.Materials.size());
        std::shared_ptr<Formats::Texture> texture;
        if (value >= 0 && value < materials && archive.Textures.TryGetValue(model.Materials[value], texture))
        {
            auto found = textures.find(texture.get());
            auto id = 0;
            if (found == textures.end())
            {
                id = textures[texture.get()] = _renderer.CreateIndexTexture(texture->Width, texture->Height * texture->FrameCount,
                    texture->Indices);
            }
            else
            {
                id = found->second;
            }

            return std::tuple{Material(id, paletteId, Vector4::One(), texture->FrameCount, Pass::DoubleSided), texture};
        }

        auto colour = 0;
        auto index = value < 0 ? -value
            : value < materials && archive.Colors.TryGetValue(model.Materials[value], colour) ? colour : -1;
        if (index < 0 || index >= Formats::Palette::Size)
        {
            return std::nullopt;
        }

        const auto& rgba = palette.Rgba;
        constexpr float ByteMax = 255.f;
        auto flat = Vector4(rgba[index * 4], rgba[index * 4 + 1], rgba[index * 4 + 2], ByteMax) / ByteMax;
        return std::tuple{Material::Flat(flat, Pass::DoubleSided), std::shared_ptr<Formats::Texture>()};
    }
}
