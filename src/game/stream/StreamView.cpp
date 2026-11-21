#include "game/stream/StreamView.h"

#include <string>

#include "core/DotNet.h"
#include "formats/Dti.h"
#include "game/Angles.h"
#include "game/kurt/FollowCamera.h"
#include "game/mods/CanvasImages.h"

namespace Mdk::Game::Stream
{
    using Engine::Render::Blend;
    using Engine::Render::Material;
    using Engine::Render::Pass;
    using Engine::Render::Renderer;
    using Engine::Render::Vertex;
    using Formats::Model;
    using Formats::ModelAnimation;

    StreamView::StreamView(Renderer& renderer, std::shared_ptr<Formats::Bni> bni, std::shared_ptr<Formats::Palette> palette,
        std::shared_ptr<Formats::TextureArchive> archive, std::shared_ptr<Mods::ModImages> mods)
        : _renderer(renderer), _bni(std::move(bni)), _palette(std::move(palette)), _archives{std::move(archive)}
    {
        _resolver = std::make_shared<Level::MaterialResolver>(renderer, std::make_shared<Formats::Dti>());
        _paletteId = renderer.CreatePalette(_palette->Rgba);
        _mods = std::move(mods);
        _background = Load("BG");
        _light = Load("LIGHT");
        _planet = Load("PLANET");
        _panel = Load("SC_STAT");
        _digits = Load("SNIP_TXT");
        _tubeMesh = renderer.CreateDynamicMesh(TubeCapacity);
        _spriteMesh = renderer.CreateDynamicMesh(SpriteCapacity * QuadVertices);
        _backMesh = renderer.CreateDynamicMesh(QuadVertices);
    }

    StreamView::Image StreamView::Load(const std::string& name)
    {
        auto texture = _bni->GetImage(name);
        return Image{Mods::CanvasImages::Create(_renderer, _mods, name, *texture, *_palette, _paletteId), Vector2(static_cast<float>(texture->Width), static_cast<float>(texture->Height))};
    }

    Engine::Render::View StreamView::ViewOf(const Flight& flight) const
    {
        auto fieldOfView = RadiansToDegrees(2.f * MathF::Atan(ViewHeight / 2.f / Focal));
        return Kurt::CameraMath::View(flight.Eye, flight.Forward, flight.Up(), Kurt::CameraMath::KeepWidth(fieldOfView, _renderer.AspectRatio()), _renderer.AspectRatio(), Near, Far);
    }

    void StreamView::Draw(const Flight& flight, const std::string& kurtAnimation, const std::string& otherModel, const std::string& otherAnimation)
    {
        DrawBackground(flight);
        DrawModel("KURT", kurtAnimation, flight.KurtFrame(), flight.KurtTransform());
        if (flight.Bones != nullptr)
        {
            DrawModel("BONES", "BONESANIM", flight.BonesFrame(), flight.KurtTransform());
        }

        if (flight.Other != nullptr)
        {
            const auto& other = *flight.Other;
            DrawModel(otherModel, otherAnimation, flight.OtherFrame(other), flight.Transform(other));
        }

        DrawTube(flight);
        DrawSprites(flight);
        DrawHealth(flight.Health, flight.Blink);
        DrawFade(flight.Fade);
    }

    void StreamView::DrawBackground(const Flight& flight)
    {
        auto width = _renderer.CanvasWidth();
        auto unit = BackgroundDistance / Focal;
        auto centre = flight.Eye + flight.Forward * BackgroundDistance;
        auto right = flight.Right * (width / 2.f * unit);
        auto up = flight.Up() * (ViewHeight / 2.f * unit);
        auto uv0 = flight.BackgroundOffset / _background.Size;
        auto uv1 = (flight.BackgroundOffset + Vector2(width, ViewHeight)) / _background.Size;

        _vertices.clear();
        AddQuad(centre - right + up, centre + right + up, centre + right - up, centre - right - up, uv0, uv1);
        _renderer.UpdateMesh(_backMesh, _vertices);
        _renderer.Draw(_backMesh, 0, QuadVertices, Material(_background.Texture, _paletteId, Vector4::One(), 1, Pass::DoubleSided));
    }

    void StreamView::AddQuad(Vector3 topLeft, Vector3 topRight, Vector3 bottomRight, Vector3 bottomLeft, Vector2 uv0, Vector2 uv1)
    {
        _vertices.push_back(Vertex(topLeft, uv0));
        _vertices.push_back(Vertex(topRight, Vector2(uv1.X, uv0.Y)));
        _vertices.push_back(Vertex(bottomRight, uv1));
        _vertices.push_back(Vertex(topLeft, uv0));
        _vertices.push_back(Vertex(bottomRight, uv1));
        _vertices.push_back(Vertex(bottomLeft, Vector2(uv0.X, uv1.Y)));
    }

    void StreamView::DrawTube(const Flight& flight)
    {
        const auto& tube = *flight.Tube();
        _vertices.clear();
        for (auto n : tube.DrawnSegments())
        {
            _tubeVertices.clear();
            tube.AddTriangles(n, flight.Eye, _tubeVertices);
            for (const auto& v : _tubeVertices)
            {
                auto c = tube.Rgba(v);
                _vertices.push_back(Vertex(v.Position, Vector2::Zero(), Vertex::Rgba(c.R, c.G, c.B, c.A)));
            }
        }

        if (_vertices.empty())
        {
            return;
        }

        _renderer.UpdateMesh(_tubeMesh, _vertices);
        _renderer.Draw(_tubeMesh, 0, static_cast<int>(_vertices.size()), Material::Flat(Vector4::One(), Pass::Blended));
    }

    void StreamView::DrawSprites(const Flight& flight)
    {
        _vertices.clear();
        auto planets = AddSprites(flight, StreamTube::SpawnKind::Planet, PlanetScale);
        auto lights = AddSprites(flight, StreamTube::SpawnKind::Light, LightScale);
        if (_vertices.empty())
        {
            return;
        }

        _renderer.UpdateMesh(_spriteMesh, _vertices);
        if (planets > 0)
        {
            _renderer.Draw(_spriteMesh, 0, planets, Material(_planet.Texture, _paletteId, Vector4::One(), 1, Pass::DoubleSided));
        }

        if (lights > 0)
        {
            // The lights glow, added to what's behind (as stream.gd; ❓ the original's blend isn't known).
            auto glow = Material(_light.Texture, _paletteId, Vector4::One(), 1, Pass::Blended, 0.f, Engine::Render::Shading::Original, Blend::Add);
            _renderer.Draw(_spriteMesh, planets, lights, glow);
        }
    }

    int StreamView::AddSprites(const Flight& flight, StreamTube::SpawnKind kind, float scale)
    {
        auto first = static_cast<int>(_vertices.size());
        for (const auto& sprite : flight.Sprites())
        {
            const auto& spawn = sprite.Spawn;
            const auto& thing = *sprite.Thing;
            if (spawn.Kind != kind || static_cast<int>(_vertices.size()) >= SpriteCapacity * QuadVertices)
            {
                continue;
            }

            auto centre = flight.Tube()->Place(thing.T, thing.X, thing.Z);
            auto half = spawn.Size * scale / 2.f;
            auto right = flight.Right * half;
            auto up = flight.Up() * half;
            AddQuad(centre - right + up, centre + right + up, centre + right - up, centre - right - up, Vector2::Zero(), Vector2::One());
        }

        return static_cast<int>(_vertices.size()) - first;
    }

    void StreamView::DrawModel(const std::string& name, const std::string& animation, int frame, Matrix4x4 transform)
    {
        auto key = std::make_tuple(name, std::optional<std::string>(animation), frame);
        auto found = _meshes.find(key);
        if (found == _meshes.end())
        {
            auto model = ModelOf(name);
            const auto& pose = Pose(name, animation, frame);
            found = _meshes.emplace(key, Build(*model, pose)).first;
        }

        if (!found->second.has_value())
        {
            return;
        }

        const auto& mesh = *found->second;
        for (const auto& batch : mesh.Batches)
        {
            _renderer.Draw(mesh.Mesh, batch.First, batch.Count, batch.Material, 0, transform);
        }
    }

    std::shared_ptr<Model> StreamView::ModelOf(const std::string& name)
    {
        auto found = _models.find(name);
        if (found != _models.end())
        {
            return found->second;
        }

        auto model = Model::Parse(name, *_bni->Bytes(), _bni->Entries().At(name).Offset, Model::Header::NoFlags, Model::Parts::Named);
        _models[name] = model;
        return model;
    }

    std::shared_ptr<ModelAnimation> StreamView::AnimationOf(const std::string& name)
    {
        auto found = _animations.find(name);
        if (found != _animations.end())
        {
            return found->second;
        }

        auto animation = ModelAnimation::Parse(name, _bni->Bytes(), _bni->Entries().At(name).Offset);
        _animations[name] = animation;
        return animation;
    }

    const StreamView::Parts& StreamView::Pose(const std::string& model, const std::string& animation, int frame)
    {
        auto key = std::make_pair(model, animation);
        auto found = _baked.find(key);
        if (found == _baked.end())
        {
            auto frames = AnimationOf(animation)->Bake(*ModelOf(model));
            found = _baked.emplace(key, std::move(frames)).first;
        }

        const auto& frames = found->second;
        return frames[Math::Clamp(frame, 0, static_cast<int>(frames.size()) - 1)];
    }

    std::optional<StreamView::PoseMesh> StreamView::Build(const Model& model, const Parts& pose)
    {
        std::vector<Vertex> vertices;
        std::vector<MeshBatch> batches;
        for (size_t p = 0; p < model.PartList.size(); p++)
        {
            const auto& part = *model.PartList[p];
            for (size_t t = 0; t < part.TriangleMaterials.size(); t++)
            {
                auto resolved = _resolver->Resolve(part.TriangleMaterials[t], model.Materials, _palette, _archives, Pass::DoubleSided);
                if (!resolved.has_value())
                {
                    continue;
                }

                const auto& surface = *resolved;
                const auto& texture = surface.Texture;
                auto scale = texture == nullptr ? Vector2::Zero() : Vector2(1.f / texture->Width, 1.f / texture->Height);
                auto first = static_cast<int>(vertices.size());
                for (auto k = 0; k < 3; k++)
                {
                    auto index = part.TriangleIndices[t * 3 + k];
                    vertices.push_back(Vertex(pose[p][index], part.TriangleUvs[t * 3 + k] * scale));
                }

                // Neighbouring triangles of one surface draw together.
                if (!batches.empty() && batches.back().Material == surface.Material)
                {
                    batches.back().Count += 3;
                    continue;
                }

                batches.push_back(MeshBatch{first, 3, surface.Material});
            }
        }

        if (vertices.empty())
        {
            return std::nullopt;
        }

        return PoseMesh{_renderer.CreateMesh(vertices), std::move(batches)};
    }

    void StreamView::DrawHealth(int health, int blink)
    {
        auto width = _renderer.CanvasWidth();
        auto at = Vector2(width - (_panel.Size.X + PanelRight), Renderer::CanvasHeight - (_panel.Size.Y + PanelBottom));
        DrawImage(_panel, RectangleF(0.f, 0.f, _panel.Size.X, _panel.Size.Y), RectangleF(at.X, at.Y, _panel.Size.X, _panel.Size.Y));
        if (health <= LowHealth && blink >= BlinkOn)
        {
            return;
        }

        // A number centred on the panel, 8-pixel digits (0x420bd0).
        auto text = std::to_string(Math::Min(health, MaxNumber));
        auto x = at.X + (ToInt(_panel.Size.X) >> 1) - DigitWidth / 2.f * text.size();
        auto y = at.Y + ((ToInt(_panel.Size.Y) - ToInt(_digits.Size.Y)) >> 1);
        for (auto c : text)
        {
            auto source = RectangleF(static_cast<float>((c - '0') * DigitWidth), 0.f, static_cast<float>(DigitWidth), _digits.Size.Y);
            DrawImage(_digits, source, RectangleF(x, y, static_cast<float>(DigitWidth), _digits.Size.Y));
            x += DigitWidth;
        }
    }

    void StreamView::DrawImage(const Image& image, RectangleF source, RectangleF target)
    {
        _renderer.DrawImage(image.Texture, _paletteId, image.Size, source, target, Vector4::One());
    }

    void StreamView::DrawFade(const Fade& fade)
    {
        auto screen = RectangleF(0.f, 0.f, _renderer.CanvasWidth(), Renderer::CanvasHeight);
        if (fade.Red > 0.f)
        {
            _renderer.AddRect(screen, Vector4(1.f, 0.f, 0.f, fade.Red));
        }

        if (fade.Whiten < 1.f)
        {
            _renderer.FillRect(screen, Vector4(1.f, 1.f, 1.f, 1.f - fade.Whiten));
        }

        if (fade.Dark < 1.f)
        {
            _renderer.FillRect(screen, Vector4(0.f, 0.f, 0.f, 1.f - fade.Dark));
        }
    }
}
