#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Mdk.Core/Drawing.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/Hud/SniperOverlay.h"
#include "Mdk.Game/Objects/ObjectView.h"
#include "Mdk.Game/Scripts/SniperRounds.h"

namespace Mdk::Game::Hud
{
    class HudView;
}

namespace Mdk::Game::Kurt
{
    class Kurt;
}

namespace Mdk::Game::Level
{
    class LevelData;
}

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;
}

namespace Mdk::Game
{
    class FrameBlend;

    // What sniper mode draws besides the world through the scope (godot-mdk sniper_overlay.gd,
    // strike_scene.gd): the rounds in flight, the round cameras and the loaded rounds as renderer
    // insets, the sniper screen's state for the HUD, and the full-screen strike.
    //   world (scope view) ──► round cameras (insets: the world from behind each round)
    //                     ──► HUD canvas (frame, mask, crosshair, zoom, ammo)
    //                     ──► clip (inset over the canvas: the loaded rounds' models)
    class SniperView
    {
    public:
        // Whether Esc was pressed during the strike.
        enum class StrikeSkip { No, Now };

        SniperView(Engine::Render::Renderer& renderer, Objects::ObjectView& objects, Level::LevelData& level, Hud::HudView& hud);

        // Each frame: the rounds in flight, then (in sniper mode) the sniper screen's insets
        // and state, as `blend` draws them between ticks and steps.
        void Draw(Kurt::Kurt& kurt, Scripts::ScriptRuntime& scripts, FrameBlend& blend, float delta);

        // The full-screen strike, if one plays: it advances (Esc skips it) and only its model is
        // drawn, over the sky. Returns its camera, or null.
        std::optional<Engine::Render::View> DrawStrike(Scripts::ScriptRuntime& scripts, StrikeSkip skip, float delta);

    private:
        // A loaded round's key: position, angles and scale.
        struct ClipKey
        {
            Vector3 Position;
            Vector3 Angles;
            float Scale = 0;
        };

        // Round cameras are 90° wide (0x461d80).
        static constexpr float RoundFieldWidth = 90.f;
        static constexpr float RoundNear = 0.5f;
        static constexpr float Far = 20000.f;
        // The loaded rounds (0x41eb10): round i at time clip timer + i (shown between 0 and 3, not at 0),
        // between keys of position, angles (z, y, x: Rz(a) Ry(-b) Rx(c)) and scale (0x490e4c), seen from
        // the origin along -Y (right +X, down +Z) with a focal length of 250 on the view.
        static constexpr std::array<ClipKey, 4> ClipKeys{{
            {Vector3(-225.f, -242.f, 65.f), Vector3(-30.f, 180.f, 90.f), 4.f},
            {Vector3(-203.f, -242.f, 107.f), Vector3(0.f, 180.f, 0.f), 9.f},
            {Vector3(-176.f, -242.f, 145.f), Vector3(0.f, 180.f, 0.f), 9.f},
            {Vector3(-154.f, -242.f, 174.f), Vector3(0.f, 180.f, 0.f), 9.f},
        }};
        static constexpr float ClipFocal = 250.f;
        static constexpr float ClipNear = 1.f;
        static constexpr float ClipFar = 1000.f;
        static constexpr std::array<const char*, 6> RoundModels{"SW_SHOT", "SW_HOME", "SW_SGREN", "SW_HGREN", "SW_LGREN", "SW_BONES"};

        Engine::Render::Renderer& _renderer;
        Objects::ObjectView& _objects;
        Level::LevelData& _level;
        Hud::HudView& _hud;
        std::unordered_map<std::string, Objects::ObjectView::Look> _looks;
        std::array<std::shared_ptr<Objects::MdkObject>, 3> _clip;
        // The round cameras of the frame (kept: no list per frame).
        std::vector<Scripts::SniperRounds::RoundCamera> _cameras;

        // The air strike's target test, only with the strike selected and the clip ready.
        static Hud::StrikeAim Aim(Kurt::Kurt& kurt, Scripts::ScriptRuntime& scripts);

        // A rectangle of the 600x360 view on the canvas.
        static RectangleF OnCanvas(RectangleF inView, float scale, float left);

        // A round camera watching its round: the world from behind it.
        void DrawRoundCamera(const Scripts::SniperRounds::RoundCamera& camera, RectangleF area);

        // The loaded rounds of the selected type between the clip's keys; they slide into the
        // chamber (key 0) after a shot (the clip timer at `clip`).
        void DrawClip(Kurt::Kurt& kurt, Scripts::ScriptRuntime& scripts, float clip, RectangleF area);

        // A loaded round's rotation, position and scale at clip time `t`.
        static std::tuple<Matrix4x4, Vector3, float> ClipPlace(float t);

        Objects::ObjectView::Look LookOf(const std::string& name);
    };
}
