#pragma once

#include <optional>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;
}

namespace Mdk::Game
{
    // The camera of the scripts' cutscenes (0x477d94; godot-mdk script_runtime.gd
    // get_cutscene_camera): the runtime moves its point and turns it each tick, this looks
    // from there. Its yaw is 90° − the heading, its pitch positive down.
    //        yaw 0 ──► +Y (heading 90°)
    //   point ●──────────► forward = (sin yaw · cos pitch, cos yaw · cos pitch, −sin pitch)
    class CutsceneCamera
    {
    public:
        Vector3 Position;
        Vector3 Forward = Vector3::UnitY();
        Vector3 Up = Vector3::UnitZ();
        Vector3 Right = Vector3::UnitX();

        // Whether a cutscene holds the camera: from the runtime's first shot (the tick after
        // special_event), so it never looks from the origin.
        static bool Active(const Scripts::ScriptRuntime& scripts);

        // Takes the runtime's shot of this tick.
        void Update(const Scripts::ScriptRuntime& scripts);

        // A frame between ticks: `alpha` of the way from the last tick's shot to this one's; a cut at once.
        void Blend(float alpha);

        // The line of sight for a yaw (90° − heading) and a pitch (positive down), in degrees.
        static Vector3 ForwardOf(float yaw, float pitch);

        Engine::Render::View View(float aspect) const;

    private:
        static constexpr float Near = 0.5f;
        static constexpr float Far = 20000.f;
        // A shot turning more than this a tick (degrees) is a cut: frames show it at once.
        static constexpr float CutTurn = 45.f;

        // The runtime's shots of the last two ticks, for frames between them (Blend).
        struct Shot
        {
            Vector3 Point;
            float Yaw = 0;
            float Pitch = 0;

            bool operator==(const Shot&) const = default;
        };

        std::optional<Shot> _previous;
        Shot _shot;
        int _tick = -1;

        static bool IsCut(const Shot& from, const Shot& to);
    };
}
