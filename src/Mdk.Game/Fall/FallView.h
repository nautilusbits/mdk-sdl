#pragma once

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Drawing.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Fti.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Formats/TextureArchive.h"
#include "Mdk.Game/Fall/FallGround.h"
#include "Mdk.Game/Fall/FallHud.h"
#include "Mdk.Game/Fall/FallModels.h"
#include "Mdk.Game/Fall/FallPalette.h"
#include "Mdk.Game/Fall/FallSim.h"

namespace Mdk::Game::Mods
{
    class ModImages;
}

namespace Mdk::Game::Fall
{
    // Draws the fall (FallSim): a camera looking straight down with a focal
    // length of 250 pixels on the 360-high view (wider windows see more on the sides). The original's
    // 2D layers behind the models (the ground, the minecrawler, the haze; the intro's space, moon and
    // earth) are screen-aligned quads far below the camera, so the models stay in front of them.
    //   camera ──►  models (Kurt, Bones, missiles, pickups, chutes, explosions), radar beam, trails
    //      │
    //      ▼ 6970  minecrawler             (fall)
    //        6980  earth                   (intro)
    //        6990  haze        │ moon      (fall: white streaks by the wind level │ intro)
    //        7000  ground      │ space
    // The palette effects change the palette, as the original does (FallPalette); the
    // intro's whitening and the darkening go over everything as canvas fills.
    class FallView
    {
    public:
        std::shared_ptr<FallModels> Models;
        std::shared_ptr<FallHud> Hud;

        FallView(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Bni> bni, std::shared_ptr<Formats::TextureArchive> mti, int index,
            std::shared_ptr<Formats::Fti> fti, std::shared_ptr<Mods::ModImages> mods = nullptr);

        // The colour behind the scene: palette colour 0 with the effects.
        Vector4 Backdrop() const { return _effects->Colour(0); }

        // Queues the frame's 3D scene and returns its camera.
        Engine::Render::View Draw(const FallSim& sim);

        // The effects over everything: towards white in the intro, then darkened.
        void DrawEffects(const FallSim& sim);

        // Whether a model or trail point is too near the camera to draw.
        static bool NearCamera(Vector3 position, Vector3 camera) { return position.Z > camera.Z - NearClearance; }

        // The centre and radius of the beam's ring ring (0-3): rings 1-3 halve the way left to the
        // spot, ring 4 is the spot (0x412af8).
        static std::pair<Vector3, float> RadarRing(Vector3 basePoint, Vector3 spot, float radius, int ring);

        // A trail point's width factor by its age in ticks: the swell's (0-5), then down to 0 at
        // the 32nd.
        static float TrailWidth(int age);

        // Segment k (1 to count - 1, the last one at the missile) of a trail of count points:
        // white at 215/255, the last 8 with blue 255 down to 31 and alpha 215 up to 250 (8 steps).
        static uint TrailColour(int k, int count);

    private:
        static constexpr float Focal = 250.f;
        static constexpr float ViewHeight = 360.f;
        static constexpr float ViewCentreY = ViewHeight / 2.f;
        static constexpr float Near = 0.5f;
        static constexpr float Far = 8000.f;
        static constexpr float BackDepth = 7000.f;
        static constexpr float LayerStep = 10.f;
        static constexpr int BackQuads = 16;
        static constexpr int QuadVertices = 6;
        // Blend of each haze table towards white (0x490d1c, / 256), tables 1-24.
        static constexpr std::array<int, 24> HazeBlend =
            { 0, 0, 0, 0, 0, 0, 0, 0, 3, 6, 12, 18, 24, 48, 72, 96, 120, 144, 168, 192, 215, 230, 245, 255 };
        static constexpr int HazeLevels = 8;
        static constexpr float BlendUnit = 256.f;
        // The radar: rings 1-4 of 6 points, translucent green by these / 256 (approximated colours).
        static constexpr std::array<int, 5> RadarAlpha = { 48, 64, 80, 96, 128 };
        static constexpr int RadarRings = 4;
        static constexpr int RadarSides = 6;
        static constexpr float RadarBaseFactor = -4.f;
        static constexpr float RadarFirstAngle = 60.f;
        static constexpr int RadarVertices = 138;
        // A trail point's width by its age in ticks (0x491d64): the ribbon swells just behind the
        // missile, then tapers (TrailWidth).
        static constexpr std::array<float, 6> TrailSwell = { 1.f, 1.25f, 1.2f, 1.1f, 1.05f, 1.f };
        // The trail's colours (0x410874's translucent table): white at 215/255, and over the last
        // segments before the missile less and less blue (yellow) and more opaque.
        static constexpr byte TrailAlpha = 215;
        static constexpr int TrailYellowSegments = 8;
        static constexpr int TrailMissiles = 24;
        // The intro as f goes 0 to 1 (0x41106c): space (600 x 360) behind, the moon (128²) centred at
        // (300, 270 - 90 f) scaled (64 + 256 f) / 256, the earth (512²) centred at (300, 488 - 224 f)
        // scaled (300 + 128 f) / 256 by (100 + 42 f) / 256: a flattened disc rising from below.
        static Vector2 ViewSize() { return Vector2(600.f, ViewHeight); }
        static constexpr float SpriteUnit = 256.f;
        static Vector2 MoonStart() { return Vector2(300.f, 270.f); }
        static constexpr float MoonRise = 90.f;
        static Vector2 MoonScale() { return Vector2(64.f, 256.f); }
        static Vector2 EarthStart() { return Vector2(300.f, 488.f); }
        static constexpr float EarthRise = 224.f;
        static Vector2 EarthScaleX() { return Vector2(300.f, 128.f); }
        static Vector2 EarthScaleY() { return Vector2(100.f, 42.f); }
        // Kurt lies face down (+0x4c yaw 90°, +0x13c pitch -90°).
        static Matrix4x4 KurtTurn();
        static constexpr float ChuteAbove = 2.f;
        // Models and trails nearer below the camera aren't drawn: a missile past Kurt (5 units
        // above him, 5 below the camera) or a pickup dropped above him would fill the view.
        static constexpr float NearClearance = 5.f;
        // An explosion grows to twice its size over its frames.
        static constexpr float ExplosionGrowth = 2.f;
        // The explosion is pitched 90° (+0x13c), before its random yaw: the model is a fireball
        // thin along x, so this turns its face to the camera above. Unpitched, it was seen edge-on, a
        // sliver beside Kurt.
        static Matrix4x4 ExplosionTurn();
        static constexpr float ExplosionMinScale = 0.01f;

        // An index texture and its size.
        struct Image
        {
            int Texture = 0;
            Vector2 Size{};
        };

        struct BackDraw
        {
            int First = 0;
            Engine::Render::Material Material;
            int Frame = 0;
        };

        Engine::Render::Renderer& _renderer;
        int _index;
        std::shared_ptr<FallPalette> _effects;
        int _palette = 0;
        int _spacePalette = 0;
        int _whitePalette = 0;
        std::unique_ptr<FallGround> _ground;
        int _groundTexture = 0;
        std::vector<Image> _crawler;
        Image _space;
        Image _moon;
        Image _earth;
        std::vector<int> _haze;
        int _backMesh = 0;
        int _radarMesh = 0;
        int _trailMesh = 0;
        // The trail ribbon's ends in the missile's model space (0x43911c): its leftmost and
        // rightmost vertices (by x), the tips of its tail fins.
        std::pair<Vector3, Vector3> _trailEnds;
        std::vector<Engine::Render::Vertex> _back;
        std::vector<BackDraw> _backDraws;

        // A palette entry (768 RGB bytes); colour 0 is forced to black.
        static std::shared_ptr<Formats::Palette> PaletteOf(const Formats::Bni& bni, const std::string& name);

        Image MakeImage(const Formats::Texture& texture);

        // A haze frame as 8 stacked masks, one per blend offset b (1-8): index 1 where the
        // pixel's offset is b, so each level draws with its own alpha.
        int HazeTexture(std::span<const byte> haze);

        // The vertical field of view: the original's 360 pixels high at 4:3 and wider, taller
        // on a narrower screen to keep the width (Kurt::CameraMath::KeepWidth).
        float FieldOfView() const;

        // The view pixels the camera sees, centred on (width / 2, 180): a near-square foldable
        // sees above and below the 360 rows too.
        RectangleF ViewArea(float width) const;

        // The fall's whitening and the red of death in the palette; colour 0 is whitened in
        // the first second only.
        void SetEffects(const FallSim& sim);

        // Space as the background, the moon and the flattened earth rising and growing.
        void QueueIntro(const FallSim& sim, Vector3 camera, float width);

        // The ground with the track, the haze over it, the minecrawler over both (fall.gd).
        void QueueGround(const FallSim& sim, Vector3 camera, float width, RectangleF area);

        static Engine::Render::Material Plain(int texture, int palette)
        {
            return Engine::Render::Material(texture, palette, Vector4::One(), 1, Engine::Render::Pass::DoubleSided);
        }

        // An image centred on centre (view pixels), scaled.
        void Sprite(Vector3 camera, float width, const Image& image, Vector2 centre, Vector2 scale, float depth, int palette);

        // A quad covering screen (view pixels) at depth below the camera.
        void Quad(Vector3 camera, float width, RectangleF screen, float depth, const Engine::Render::Material& material,
            std::optional<RectangleF> uv = std::nullopt, int frame = 0);

        void QueueModels(const FallSim& sim);

        // Points the model's +y along a direction, z up (0x4123e8).
        static Matrix4x4 Orient(Vector3 direction);

        // The radar's beam (0x412f94): a fan from its base to ring 1, rings 1-3 at
        // 1 - 2^-k of the way to the spot with radius r (1 - 2^-k), ring 4 on the spot, and a cap on ring 4.
        //   base ◄── fan ── ring 1 ══ ring 2 ══ ring 3 ══ ring 4 (cap) ── spot
        void QueueRadar(const FallSim& sim);

        // The smoke trails (0x439454; set up by 0x43911c, sampled by 0x4392a4): a ribbon along the
        // last 32 points of each missile (oldest first), between its tail fins' tips as the missile was
        // turned at each point, widened by TrailWidth; each segment in one colour (TrailColour).
        //   tail ─────── white (215/255) ───────╴╴▶ last 8 segments: yellower, more opaque ─▶ missile
        //   width ×0 ◄──── tapering ────── ×1, ×1.05, ×1.1, ×1.2, ×1.25 (swell), ×1 at the missile
        void QueueTrails(const FallSim& sim);
    };
}
