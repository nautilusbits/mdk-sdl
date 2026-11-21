#pragma once

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/Dictionary.h"
#include "core/Numerics.h"
#include "engine/render/Renderer.h"
#include "formats/Bni.h"
#include "formats/Palette.h"
#include "formats/SpriteAnimation.h"
#include "formats/Texture.h"
#include "game/collision/ArenaSpace.h"
#include "game/hd_textures/HdImage.h"
#include "game/kurt/Kurt.h"

namespace Mdk::Game::Mods
{
    class ModImages;
}

namespace Mdk::Game::Kurt
{
    // Draws Kurt like the original: a sprite frame at a fixed size on screen (one pixel is
    // 1/360 of the view's height), its hotspot 101 pixels above his projected feet, depth-tested at
    // the feet's depth. The original draws no depth: Kurt covers the floor he stands on. Here he's
    // brought nearer the camera, the same on screen, in front of the floors (at most half a unit
    // above his feet) the eye's rays to his frame cross: a downhill, the edge of a slope he walks
    // down. The rows from just above the feet down (the shadow) also come 2 units nearer, over
    // uneven floor. The enhanced look takes mods' images of its frames (ModImages).
    //      ┌──────┐  frame: in front of the floors its rays cross
    //      │  ☺   │
    //      │ /|\  │  hotspot + (0, 101) ─► the feet
    //      ├──────┤  ─┐ from half a unit above the feet: also 2 units nearer
    //      └──────┘  ─┘ (the shadow)
    class KurtSprite
    {
        friend struct KurtSpriteTestsProbe;

    public:
        // Kurt's frames kept in the level's archives: the snowboard's (LEVEL4S.SNI), the
        // slide's (LEVEL6S.SNI).
        static const std::vector<std::string>& LevelAnimations();

        // The animations Kurt is drawn with: TRAVSPRT.BNI's, then the levels' (the HD textures' export).
        static std::vector<std::string> Drawn();

        KurtSprite(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Bni> sprites, std::shared_ptr<Formats::Palette> palette,
            Engine::Render::Shading shading, std::shared_ptr<Mods::ModImages> mods = nullptr);

        // A frame's mod image (HD textures too): the enhanced look's sprites only.
        static std::optional<HdTextures::HdImage> Upscaled(Mods::ModImages* mods, Engine::Render::Shading shading, const std::string& name,
            const Formats::Texture& image, const Formats::Palette& palette);

        // A frame's name in mods: the animation's, then its number (K_RUN_0).
        static std::string FrameName(const std::string& animation, int frame);

        int FrameCount(Kurt::State state) const;

        // Adds an animation of the level's (null: none there).
        void Add(const std::shared_ptr<Formats::SpriteAnimation>& animation);

        // Frames of an animation of the level's or of TRAVSPRT.BNI (0 when missing).
        int FrameCount(const std::string& name) const;

        // Queues Kurt's frame for the camera at eye looking along forward with up, the view
        // fieldOfView degrees high, his feet at feet (a frame between steps); the space's floors
        // he stands above don't hide him.
        void Draw(const Kurt& kurt, Vector3 feet, Vector3 eye, Vector3 forward, Vector3 up, float fieldOfView, Collision::ArenaSpace& space);

        // The scale towards the eye bringing a frame at the feet's depth in front of a floor at
        // floorDepth (1: none).
        static float InFront(float depth, std::optional<float> floorDepth)
        {
            return floorDepth.has_value() ? Math::Clamp(*floorDepth / depth * (1.f - FloorMargin), MinFloorShare, 1.f) : 1.f;
        }

        // The scale towards the eye bringing the feet's rows ShadowPull nearer than the feet's
        // depth (8 behind the follow camera: 0.75), and in front of a floor at floorDepth.
        static float Nearer(float depth, std::optional<float> floorDepth)
        {
            return MathF::Min(MathF::Max(MinDepthShare, (depth - ShadowPull) / depth), InFront(depth, floorDepth));
        }

        // The frame's horizontal scale: -1 mirrors it. K_ROLLL and K_ROLLR are the same file,
        // a roll to the right: mirrored for the left.
        static float Mirror(const Kurt& kurt) { return kurt.Current == Kurt::State::RollLeft ? -1.f : 1.f; }

    private:
        // Kurt's frames are drawn this many pixels above his feet.
        static constexpr int FeetOffset = 101;
        static constexpr float ViewHeight = 360.f;
        static constexpr int QuadVertices = 6;
        // A frame's two quads: the body, and the feet with the shadow nearer still.
        static constexpr int FrameVertices = 2 * QuadVertices;
        // The nearer rows start this high above the feet (world units), over floor bumps.
        static constexpr float FeetRise = 0.5f;
        // The nearer rows are this much nearer (world units), at most down to a share of the depth.
        static constexpr float ShadowPull = 2.f;
        static constexpr float MinDepthShare = 0.5f;
        // In front of a floor the frame goes this share nearer than it, at most down to a share
        // of the depth; the floor counts when the feet are at most FloorBelow under its plane.
        static constexpr float FloorMargin = 0.02f;
        static constexpr float MinFloorShare = 0.1f;
        static constexpr float FloorBelow = 0.5f;
        // The chute is painted in Kurt's frames (damp_animate 0x4646a4): K_CHUTE 0-4 open it,
        // then K_CHUTEC sways back and forth.
        static constexpr const char* ChuteOpening = "K_CHUTE";
        static constexpr const char* ChuteSway = "K_CHUTEC";
        static constexpr int ChuteOpenedFrame = 5;
        // K_CHUTE's frame shown once when the chute closes in the air.
        static constexpr int ChuteClosedFrame = 7;
        // The chain gun's muzzle flash, drawn behind Kurt (a little farther from the camera).
        static constexpr const char* MuzzleFlash = "K_MUZZF";
        static constexpr float MuzzleBehind = 0.05f;
        // The 1996 demo's frames: the helmet (played backwards when it comes off), backing up.
        static constexpr const char* Helmet = "K_HELM";
        static constexpr const char* BackUp = "K_BCKUP";

        enum class Repeat { Once, Loop };

        struct StateAnimation
        {
            std::string Name;
            KurtSprite::Repeat Repeat = Repeat::Once;
        };

        static const Mdk::Dictionary<Kurt::State, StateAnimation>& Animations();

        // Where a frame goes: the feet, and the world vectors of one sprite pixel right and up.
        struct Quad
        {
            Vector3 Feet{};
            Vector3 Right{};
            Vector3 Up{};
        };

        // A frame scaled towards the Eye: nearer, the same on screen. The rows from Rise pixels
        // above the feet down by Feet, the others by Body.
        struct Pull
        {
            Vector3 Eye{};
            float Body = 0;
            float Feet = 0;
            float Rise = 0;
        };

        Engine::Render::Renderer& _renderer;
        std::shared_ptr<Formats::Bni> _sprites;
        int _palette = 0;
        Engine::Render::Shading _shading;
        std::shared_ptr<Formats::Palette> _colours;
        std::shared_ptr<Mods::ModImages> _mods;
        int _mesh = 0;
        int _muzzleMesh = 0;
        std::map<std::pair<std::string, int>, int> _textures;
        // Frames from the level's archives (the board's K_SURF and K_SURFJ).
        std::unordered_map<std::string, std::shared_ptr<Formats::SpriteAnimation>> _extra;

        // Decodes the frames' mod images on every core before they're asked for one by one
        // (the HD textures' Kurt: 360 files a level's load).
        void Prefetch(const std::vector<std::shared_ptr<Formats::SpriteAnimation>>& animations);

        // Decodes an animation's frames and puts them on the GPU, HD when made (a level's load).
        void Preload(Formats::SpriteAnimation& animation);

        Engine::Render::Material MaterialOf(const std::string& name, int frame, const Formats::Texture& image);

        std::shared_ptr<Formats::SpriteAnimation> Animation(const std::string& name) const;

        // Draws a frame with its hotspot 101 pixels above the feet, moved by (dx, dy) pixels (y down),
        // brought nearer by the pull.
        void DrawFrame(int mesh, const std::string& name, int frame, const Quad& at, int dx, int dy, const Pull& pull);

        // The nearest depth of a floor the eye's rays to a frame's corners and middle (its
        // top, rise pixels above the feet, its bottom) cross, among those the feet
        // stand above (drawn before Kurt in the original), or null.
        std::optional<float> FloorDepth(Collision::ArenaSpace& space, Vector3 eye, Vector3 forward, const Quad& at, const std::string& name,
            int frame, float rise);

        // A frame's pixel (px, py) in the world, its feet's pixel (ax, ay) on Kurt's feet.
        static Vector3 Corner(const Quad& at, float ax, float ay, float px, float py)
        {
            return at.Feet + at.Right * (px - ax) + at.Up * (ay - py);
        }

        // The quad of a frame's rows top to bottom, scaled towards the eye. Returns its vertex
        // count (0 when empty).
        static int Rows(std::span<Engine::Render::Vertex> into, const Quad& at, float ax, float ay, int w, int h, float top, float bottom,
            Vector3 eye, float scale);

        // The animation and frame of Kurt's state; the chute opens, then sways.
        std::pair<std::string, int> Pick(const Kurt& kurt) const;

        int Texture(const std::string& name, int frame, const Formats::Texture& image);
    };
}
