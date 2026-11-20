#include "Mdk.Game/Fall/FallView.h"

#include <algorithm>
#include <format>

#include "Mdk.Formats/Fall3d.h"
#include "Mdk.Game/Angles.h"
#include "Mdk.Game/Kurt/FollowCamera.h"

namespace Mdk::Game::Fall
{
    using Engine::Render::Material;
    using Engine::Render::Pass;
    using Engine::Render::Vertex;
    using Formats::Palette;

    namespace
    {
        constexpr int ByteMax = 255;
    }

    // Kurt lies face down (+0x4c yaw 90°, +0x13c pitch −90°).
    Matrix4x4 FallView::KurtTurn()
    {
        static const Matrix4x4 turn = Matrix4x4::CreateRotationY(MathF::PI / 2.f) * Matrix4x4::CreateRotationZ(MathF::PI / 2.f);
        return turn;
    }

    // The explosion is pitched 90° (+0x13c), before its random yaw: the model is a fireball
    // thin along x, so this turns its face to the camera above. Unpitched, it was seen edge-on, a
    // sliver beside Kurt.
    Matrix4x4 FallView::ExplosionTurn()
    {
        static const Matrix4x4 turn = Matrix4x4::CreateRotationY(-MathF::PI / 2.f);
        return turn;
    }

    FallView::FallView(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Bni> bni, std::shared_ptr<Formats::TextureArchive> mti, int index,
        std::shared_ptr<Formats::Fti> fti, std::shared_ptr<Mods::ModImages> mods)
        : _renderer(renderer), _index(index)
    {
        auto n = index + 1;
        auto palette = PaletteOf(*bni, std::format("FALLP{}", n));
        _effects = std::make_shared<FallPalette>(std::vector<byte>(palette->Rgba.begin(), palette->Rgba.end()));
        _palette = renderer.CreatePalette(palette->Rgba);
        _spacePalette = renderer.CreatePalette(PaletteOf(*bni, "SPACEPAL")->Rgba);
        std::vector<byte> white(Palette::Size * 4, static_cast<byte>(ByteMax));
        _whitePalette = renderer.CreatePalette(white);
        Models = std::make_shared<FallModels>(renderer, bni, mti, _effects, _palette);

        // The missile's leftmost and rightmost vertices (MinBy/MaxBy: the first of equals).
        std::vector<Vector3> missileVertices;
        if (auto missile = Models->Find("MISSILE"))
        {
            for (const auto& part : missile->PartList)
            {
                missileVertices.insert(missileVertices.end(), part->Vertices.begin(), part->Vertices.end());
            }
        }

        if (missileVertices.empty())
        {
            _trailEnds = { -Vector3::UnitX(), Vector3::UnitX() };
        }
        else
        {
            auto left = missileVertices[0];
            auto right = missileVertices[0];
            for (const auto& v : missileVertices)
            {
                if (v.X < left.X)
                {
                    left = v;
                }

                if (v.X > right.X)
                {
                    right = v;
                }
            }

            _trailEnds = { left, right };
        }

        Hud = std::make_shared<FallHud>(renderer, *bni, _palette, fti, palette, mods);

        _ground = std::make_unique<FallGround>(*mti->Textures.At(std::format("LEVEL{}", n)), mti->Textures.At(std::format("POD{}", n)));
        _groundTexture = renderer.CreateIndexTexture(FallGround::Size, FallGround::Size, _ground->Indices);
        for (auto i = 1; i <= FallSim::CrawlerFrames; i++)
        {
            if (const auto* frame = mti->Textures.Find(std::format("L{}_C{:04}", n, i)))
            {
                _crawler.push_back(MakeImage(**frame));
            }
        }

        _space = MakeImage(*bni->GetImage("SPACE"));
        _moon = MakeImage(*bni->GetImage("MOON"));
        _earth = MakeImage(*bni->GetImage("EARTH"));
        for (auto i = 0; i < FallSim::HazeFrames; i++)
        {
            _haze.push_back(HazeTexture(Formats::Fall3d::Haze(*bni, std::format("ZOOM{:04}", i))));
        }

        _backMesh = renderer.CreateDynamicMesh(BackQuads * QuadVertices);
        _radarMesh = renderer.CreateDynamicMesh(RadarVertices);
        _trailMesh = renderer.CreateDynamicMesh(TrailMissiles * FallMissile::TrailPoints * QuadVertices);
    }

    // A palette entry (768 RGB bytes); colour 0 is forced to black.
    std::shared_ptr<Palette> FallView::PaletteOf(const Formats::Bni& bni, const std::string& name)
    {
        const auto& bytes = *bni.Bytes();
        return Palette::FromRgb(std::span<const byte>(bytes.data() + bni.Entries().At(name).Offset, Palette::Size * 3));
    }

    FallView::Image FallView::MakeImage(const Formats::Texture& texture)
    {
        return Image{ _renderer.CreateIndexTexture(texture.Width, texture.Height, texture.Indices),
            Vector2(static_cast<float>(texture.Width), static_cast<float>(texture.Height)) };
    }

    // A haze frame as 8 stacked masks, one per blend offset b (1-8): index 1 where the
    // pixel's offset is b, so each level draws with its own alpha.
    int FallView::HazeTexture(std::span<const byte> haze)
    {
        auto size = Formats::Fall3d::HazeWidth * Formats::Fall3d::HazeRows;
        std::vector<byte> masks(size * HazeLevels);
        for (auto i = 0; i < size; i++)
        {
            auto b = haze[i];
            if (b > 0 && b <= HazeLevels)
            {
                masks[(b - 1) * size + i] = 1;
            }
        }

        return _renderer.CreateIndexTexture(Formats::Fall3d::HazeWidth, Formats::Fall3d::HazeRows * HazeLevels, masks);
    }

    // Queues the frame's 3D scene and returns its camera.
    Engine::Render::View FallView::Draw(const FallSim& sim)
    {
        SetEffects(sim);
        auto camera = sim.CameraPosition;
        auto width = _renderer.CanvasWidth();
        _back.clear();
        _backDraws.clear();
        if (sim.InIntro())
        {
            QueueIntro(sim, camera, width);
        }
        else
        {
            QueueGround(sim, camera, width, ViewArea(width));
        }

        _renderer.UpdateMesh(_backMesh, _back);
        for (const auto& draw : _backDraws)
        {
            _renderer.Draw(_backMesh, draw.First, QuadVertices, draw.Material, draw.Frame);
        }

        QueueModels(sim);
        QueueRadar(sim);
        QueueTrails(sim);
        auto view = Matrix4x4::CreateLookAt(camera, camera - Vector3::UnitZ(), Vector3::UnitY());
        auto fieldOfView = FieldOfView();
        auto projection = Matrix4x4::CreatePerspectiveFieldOfView(fieldOfView, _renderer.AspectRatio(), Near, Far);
        return Engine::Render::View{ view * projection, Matrix4x4::Identity(), camera,
            Engine::Render::ViewCamera{ -Vector3::UnitZ(), Vector3::UnitY(), projection, Near } };
    }

    // The vertical field of view: the original's 360 pixels high at 4:3 and wider, taller
    // on a narrower screen to keep the width (Kurt::CameraMath::KeepWidth).
    float FallView::FieldOfView() const
    {
        auto fieldOfView = 2.f * MathF::Atan(ViewCentreY / Focal);
        auto widened = Kurt::CameraMath::KeepWidth(RadiansToDegrees(fieldOfView), _renderer.AspectRatio());
        return widened == RadiansToDegrees(fieldOfView) ? fieldOfView : DegreesToRadians(widened);
    }

    // The view pixels the camera sees, centred on (width / 2, 180): a near-square foldable sees
    // above and below the 360 rows too.
    RectangleF FallView::ViewArea(float width) const
    {
        auto half = Vector2(_renderer.AspectRatio(), 1.f) * MathF::Tan(FieldOfView() / 2.f) * Focal;
        return RectangleF(width / 2.f - half.X, ViewCentreY - half.Y, 2.f * half.X, 2.f * half.Y);
    }

    // The effects over everything: towards white in the intro, then darkened.
    void FallView::DrawEffects(const FallSim& sim)
    {
        auto canvas = RectangleF(0.f, 0.f, _renderer.CanvasWidth(), Engine::Render::Renderer::CanvasHeight);
        if (sim.InIntro() && sim.Whiten < 1.f)
        {
            _renderer.FillRect(canvas, Vector4(1.f, 1.f, 1.f, 1.f - sim.Whiten));
        }

        if (sim.Dark < 1.f)
        {
            _renderer.FillRect(canvas, Vector4(0.f, 0.f, 0.f, 1.f - sim.Dark));
        }
    }

    // The fall's whitening and the red of death in the palette; colour 0 is whitened in
    // the first second only.
    void FallView::SetEffects(const FallSim& sim)
    {
        auto brightness = sim.InIntro() ? 1.f : sim.Whiten;
        auto zero = sim.Time < 1.f ? FallPalette::Zero::Whitened : FallPalette::Zero::Kept;
        if (_effects->Set(brightness, sim.Red, zero))
        {
            _renderer.UpdateTexture(_palette, Palette::Size, 1, _effects->Rgba);
        }
    }

    // Space as the background, the moon and the flattened earth rising and growing.
    void FallView::QueueIntro(const FallSim& sim, Vector3 camera, float width)
    {
        auto f = 1.f - sim.IntroLeft() / static_cast<float>(FallSim::IntroTicks);
        auto origin = Vector2((width - ViewSize().X) / 2.f, 0.f);
        Quad(camera, width, RectangleF(origin.X, origin.Y, ViewSize().X, ViewSize().Y), BackDepth, Plain(_space.Texture, _spacePalette));
        auto grow = [f](Vector2 scale) { return (scale.X + scale.Y * f) / SpriteUnit; };
        auto moon = origin + MoonStart() - Vector2(0.f, MoonRise * f);
        Sprite(camera, width, _moon, moon, Vector2(grow(MoonScale())), BackDepth - LayerStep, _spacePalette);
        auto earth = origin + EarthStart() - Vector2(0.f, EarthRise * f);
        Sprite(camera, width, _earth, earth, Vector2(grow(EarthScaleX()), grow(EarthScaleY())), BackDepth - 2.f * LayerStep, _spacePalette);
    }

    // The ground with the track, the haze over it, the minecrawler over both (fall.gd).
    void FallView::QueueGround(const FallSim& sim, Vector3 camera, float width, RectangleF area)
    {
        auto centreRow = FallGround::CentreRow(sim.Time);
        if (_ground->UpdateTrack(centreRow))
        {
            _renderer.UpdateTexture(_groundTexture, FallGround::Size, FallGround::Size, _ground->Indices);
        }

        auto [screen, texels] = FallGround::Visible(camera, centreRow, area);
        if (screen.Width > 0.f)
        {
            constexpr auto size = static_cast<float>(FallGround::Size);
            auto uv = RectangleF(texels.X / size, texels.Y / size, texels.Width / size, texels.Height / size);
            Quad(camera, width, screen, BackDepth, Plain(_groundTexture, _palette), uv);
        }

        if (!_crawler.empty())
        {
            auto [centre, scale] = FallGround::Crawler(camera, width);
            Sprite(camera, width, _crawler[ToInt(sim.CrawlerFrame) % static_cast<int>(_crawler.size())], centre, Vector2(scale), BackDepth - 3.f * LayerStep, _palette);
        }

        // Each blend offset b uses table (wind level + b): white by its alpha.
        auto level = sim.Wind >> FallSim::WindShift;
        for (auto b = 1; b <= HazeLevels; b++)
        {
            auto alpha = HazeBlend[Math::Clamp(level + b, 1, static_cast<int>(HazeBlend.size())) - 1] / BlendUnit;
            if (alpha <= 0.f)
            {
                continue;
            }

            auto material = Material(_haze[sim.HazeFrame], _whitePalette, Vector4(1.f, 1.f, 1.f, alpha), HazeLevels, Pass::Blended);
            Quad(camera, width, area, BackDepth - LayerStep, material, std::nullopt, b - 1);
        }
    }

    // An image centred on centre (view pixels), scaled.
    void FallView::Sprite(Vector3 camera, float width, const Image& image, Vector2 centre, Vector2 scale, float depth, int palette)
    {
        auto size = image.Size * scale;
        Quad(camera, width, RectangleF(centre.X - size.X / 2.f, centre.Y - size.Y / 2.f, size.X, size.Y), depth, Plain(image.Texture, palette));
    }

    // A quad covering screen (view pixels) at depth below the camera.
    void FallView::Quad(Vector3 camera, float width, RectangleF screen, float depth, const Material& material, std::optional<RectangleF> uv, int frame)
    {
        if (static_cast<int>(_back.size()) + QuadVertices > BackQuads * QuadVertices)
        {
            return;
        }

        auto t = uv.value_or(RectangleF(0.f, 0.f, 1.f, 1.f));
        auto corner = [&](float x, float y, float u, float v)
        {
            return Vertex(Vector3(camera.X + (x - width / 2.f) * depth / Focal, camera.Y - (y - ViewCentreY) * depth / Focal, camera.Z - depth),
                Vector2(u, v));
        };
        auto first = static_cast<int>(_back.size());
        _back.push_back(corner(screen.Left(), screen.Top(), t.Left(), t.Top()));
        _back.push_back(corner(screen.Right(), screen.Top(), t.Right(), t.Top()));
        _back.push_back(corner(screen.Right(), screen.Bottom(), t.Right(), t.Bottom()));
        _back.push_back(corner(screen.Left(), screen.Top(), t.Left(), t.Top()));
        _back.push_back(corner(screen.Right(), screen.Bottom(), t.Right(), t.Bottom()));
        _back.push_back(corner(screen.Left(), screen.Bottom(), t.Left(), t.Bottom()));
        _backDraws.push_back(BackDraw{ first, material, frame });
    }

    void FallView::QueueModels(const FallSim& sim)
    {
        if (sim.KurtVisible)
        {
            auto animation = sim.KurtPose == FallSim::KurtAnimation::Hit ? "KURT_HIT" : "KURTANIM";
            Models->Draw("KURT", KurtTurn() * Matrix4x4::CreateTranslation(sim.KurtPosition), animation, ToInt(sim.KurtFrame));
        }

        if (sim.BonesPosition)
        {
            Models->Draw("BONES", KurtTurn() * Matrix4x4::CreateTranslation(*sim.BonesPosition), "BONESANM", ToInt(sim.BonesFrame));
        }

        auto camera = sim.CameraPosition;
        for (const auto& missile : sim.Missiles())
        {
            if (NearCamera(missile->Position, camera))
            {
                continue;
            }

            Models->Draw("MISSILE", Orient(missile->Velocity) * Matrix4x4::CreateTranslation(missile->Position));
        }

        for (const auto& pickup : sim.Pickups())
        {
            if (NearCamera(pickup->Position, camera))
            {
                continue;
            }

            auto turn = Matrix4x4::CreateRotationZ(DegreesToRadians(pickup->Yaw));
            Models->Draw(pickup->Name, turn * Matrix4x4::CreateTranslation(pickup->Position));
            auto chute = pickup->Position + Vector3(0.f, 0.f, ChuteAbove);
            if (pickup->Chute && !NearCamera(chute, camera))
            {
                Models->Draw("CHUTE", turn * Matrix4x4::CreateTranslation(chute));
            }
        }

        // Explosions grow and play their texture's frames, over everything (sorted at camera z + 5).
        for (const auto& explosion : sim.Explosions())
        {
            auto frame = ToInt(explosion->Ticks);
            auto scale = MathF::Max(ExplosionGrowth * frame / FallSim::ExplosionTicks, ExplosionMinScale);
            auto world = Matrix4x4::CreateScale(scale) * ExplosionTurn() * Matrix4x4::CreateRotationZ(explosion->Yaw) * Matrix4x4::CreateTranslation(explosion->Position);
            Models->Draw("EXPLODE", world, "", 0, frame, Pass::OnTop);
        }
    }

    // Points the model's +y along a direction, z up (0x4123e8).
    Matrix4x4 FallView::Orient(Vector3 direction)
    {
        auto y = Vector3::Normalize(direction);
        auto x = Vector3::Cross(y, Vector3::UnitZ());
        x = x.LengthSquared() < 1e-12f ? Vector3::UnitX() : Vector3::Normalize(x);
        auto z = Vector3::Cross(x, y);
        return Matrix4x4(x.X, x.Y, x.Z, 0.f, y.X, y.Y, y.Z, 0.f, z.X, z.Y, z.Z, 0.f, 0.f, 0.f, 0.f, 1.f);
    }

    // The radar's beam (0x412f94): a fan from its base to ring 1, rings 1-3 at 1 − 2^−k of the
    // way to the spot with radius r (1 − 2^−k), ring 4 on the spot, and a cap on ring 4.
    //   base ◄── fan ── ring 1 ══ ring 2 ══ ring 3 ══ ring 4 (cap) ── spot
    void FallView::QueueRadar(const FallSim& sim)
    {
        if (!sim.RadarActive)
        {
            return;
        }

        auto camera = sim.CameraPosition;
        auto spot = sim.RadarSpot;
        auto basePoint = Vector3(RadarBaseFactor * camera.X, RadarBaseFactor * camera.Y, 0.f);
        std::array<std::array<Vector3, RadarSides>, RadarRings> rings{};
        for (auto k = 0; k < RadarRings; k++)
        {
            auto [centre, radius] = RadarRing(basePoint, spot, sim.RadarSpotRadius, k);
            for (auto m = 0; m < RadarSides; m++)
            {
                auto angle = DegreesToRadians(RadarFirstAngle * (m + 1));
                rings[k][m] = centre + Vector3(MathF::Cos(angle), MathF::Sin(angle), 0.f) * radius;
            }
        }

        std::vector<Vertex> vertices;
        vertices.reserve(RadarVertices);
        auto add = [&vertices](std::initializer_list<Vector3> points)
        {
            for (const auto& p : points)
            {
                vertices.push_back(Vertex(p, Vector2::Zero()));
            }
        };
        std::vector<int> bands = { 0 };
        for (auto m = 0; m < RadarSides; m++)
        {
            add({ basePoint, rings[0][m], rings[0][(m + 1) % RadarSides] });
        }

        for (auto k = 0; k < RadarRings - 1; k++)
        {
            bands.push_back(static_cast<int>(vertices.size()));
            for (auto m = 0; m < RadarSides; m++)
            {
                auto a = rings[k][m];
                auto b = rings[k][(m + 1) % RadarSides];
                auto c = rings[k + 1][m];
                auto d = rings[k + 1][(m + 1) % RadarSides];
                add({ a, b, c, b, d, c });
            }
        }

        bands.push_back(static_cast<int>(vertices.size()));
        const auto& last = rings[RadarRings - 1];
        for (auto m = 1; m < RadarSides - 1; m++)
        {
            add({ last[0], last[m], last[m + 1] });
        }

        bands.push_back(static_cast<int>(vertices.size()));
        _renderer.UpdateMesh(_radarMesh, vertices);
        for (auto c = 0; c < static_cast<int>(RadarAlpha.size()); c++)
        {
            auto colour = Material::Flat(_effects->Apply(Vector4(0.f, 1.f, 0.f, RadarAlpha[c] / BlendUnit)), Pass::Blended);
            _renderer.Draw(_radarMesh, bands[c], bands[c + 1] - bands[c], colour);
        }
    }

    // The centre and radius of the beam's ring (0-3): rings 1-3 halve the way left to the spot,
    // ring 4 is the spot (0x412af8).
    std::pair<Vector3, float> FallView::RadarRing(Vector3 basePoint, Vector3 spot, float radius, int ring)
    {
        auto f = ring == RadarRings - 1 ? 1.f : 1.f - MathF::Pow(2.f, static_cast<float>(-(ring + 1)));
        return { Vector3::Lerp(basePoint, spot, f), radius * f };
    }

    // The smoke trails (0x439454; set up by 0x43911c, sampled by 0x4392a4): a ribbon along the
    // last 32 points of each missile (oldest first), between its tail fins' tips as the missile was
    // turned at each point, widened by TrailWidth; each segment in one colour (TrailColour).
    //   tail ─────── white (215/255) ───────╴╴▶ last 8 segments: yellower, more opaque ─▶ missile
    //   width ×0 ◄──── tapering ────── ×1, ×1.05, ×1.1, ×1.2, ×1.25 (swell), ×1 at the missile
    void FallView::QueueTrails(const FallSim& sim)
    {
        std::vector<Vertex> vertices;
        auto camera = sim.CameraPosition;
        auto middle = (_trailEnds.first + _trailEnds.second) / 2.f;
        std::array<Vector3, FallMissile::TrailPoints> lefts{};
        std::array<Vector3, FallMissile::TrailPoints> rights{};
        const auto& missiles = sim.Missiles();
        auto shown = Math::Min(static_cast<int>(missiles.size()), TrailMissiles);
        for (auto mi = 0; mi < shown; mi++)
        {
            const auto& missile = *missiles[mi];
            const auto& trail = missile.Trail();
            auto count = Math::Min(static_cast<int>(trail.size()), FallMissile::TrailPoints);
            for (auto i = 0; i < count; i++)
            {
                // The fins' tips around their middle, as turned then, scaled by the point's age.
                auto turn = Orient(missile.TrailHeadings()[i]);
                auto centre = trail[i] + Vector3::TransformNormal(middle, turn);
                auto width = TrailWidth(count - 1 - i);
                lefts[i] = centre + Vector3::TransformNormal(_trailEnds.first - middle, turn) * width;
                rights[i] = centre + Vector3::TransformNormal(_trailEnds.second - middle, turn) * width;
            }

            for (auto k = 1; k < count; k++)
            {
                if (NearCamera(trail[k - 1], camera) || NearCamera(trail[k], camera))
                {
                    continue;
                }

                auto colour = TrailColour(k, count);
                const std::array<Vector3, 4> quad = { lefts[k - 1], rights[k - 1], rights[k], lefts[k] };
                for (auto q : { 0, 1, 2, 0, 2, 3 })
                {
                    vertices.push_back(Vertex(quad[q], Vector2::Zero(), colour));
                }
            }
        }

        if (vertices.empty())
        {
            return;
        }

        _renderer.UpdateMesh(_trailMesh, vertices);
        _renderer.Draw(_trailMesh, 0, static_cast<int>(vertices.size()), Material::Flat(_effects->Apply(Vector4::One()), Pass::Blended));
    }

    // A trail point's width factor by its age in ticks: the swell's (0-5), then down to 0 at
    // the 32nd.
    float FallView::TrailWidth(int age)
    {
        auto swell = static_cast<int>(TrailSwell.size());
        if (age < swell)
        {
            return TrailSwell[age];
        }

        auto taper = FallMissile::TrailPoints - swell;
        return 1.f - static_cast<float>(age - swell) / taper;
    }

    // Segment k (1 to count − 1, the last one at the missile) of a trail of count points: white
    // at 215/255, the last 8 with blue 255 down to 31 and alpha 215 up to 250 (8 steps).
    uint FallView::TrailColour(int k, int count)
    {
        auto step = k - (count - TrailYellowSegments);
        if (step < 0)
        {
            return Vertex::Rgba(ByteMax, ByteMax, ByteMax, TrailAlpha);
        }

        auto blue = (2040 - ByteMax * step) >> 3;
        auto alpha = ((TrailYellowSegments - step) * TrailAlpha + ByteMax * step) >> 3;
        return Vertex::Rgba(ByteMax, ByteMax, static_cast<byte>(blue), static_cast<byte>(alpha));
    }
}
