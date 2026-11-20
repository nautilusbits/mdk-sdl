#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string_view>

#include "Mdk.Core/Numerics.h"

namespace Mdk::Formats
{
    class Model;
    class ModelAnimation;
}

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;

    // The full-screen strike (0x4398f0; a port of godot-mdk's strike_scene.gd): before Bones'
    // air strike (X_STRIKB) and Kurt's strike at the end of the game (X_STRIKD) the game stops and only
    // the sky and that model are drawn. The model plays its animation once while it turns at 45°/s; the
    // camera sits at its reference point 1 looking at its reference point 0 (both moved by the
    // animation), with a long lens (focal length 600 / 0.35265 pixels of the 360-high view). Esc skips
    // it; the music goes on.
    //
    //   game paused ──► [ sky + model, camera on the model's points ] ──► Finished ──► game resumes
    class StrikeScene
    {
    public:
        // Which strike: Bones' plane, or Kurt's (event 51).
        enum class Kind { Bones, Kurt };

        // The plane with Bones in it, or the plane only (the dive strike).
        enum class Plane { WithPilot, Only };

        // Vertical field of view in degrees.
        static const float FieldOfView;

        // C#'s (Vector3 Eye, Vector3 Target) tuple.
        struct View
        {
            Vector3 Eye;
            Vector3 Target;
        };

        StrikeScene();

        std::function<void()> Finished;

        // The model shown, posed and turned (drawn like an object).
        std::shared_ptr<Objects::MdkObject> Subject;

        bool Active = false;

        // The camera: its place and the point it looks at.
        View Camera() const;

        // Plays the strike of kind in Kurt's arena; it ends at once without its model.
        void Play(ScriptRuntime& runtime, Kind kind, Plane plane);

        // Advances by delta seconds; the animation's end finishes it.
        void Update(float delta);

        // Ends the scene (Esc).
        void Skip();

    private:
        static constexpr float TurnRate = 45.0f;
        static constexpr float Focal = 600.0f / 0.35265395f;
        static constexpr float ViewHeight = 360.0f;
        static constexpr int CameraPoint = 1;
        static constexpr int LookPoint = 0;
        static constexpr int NeededPoints = 2;
        // When Bones dives alone, only the plane's parts show (0x491ddc).
        static constexpr std::array<std::string_view, 7> PlaneParts = {"AWING", "CANOPY", "LEVER", "LEVER01", "LEVER02", "LEVER03", "OBJECT"};

        std::shared_ptr<Formats::ModelAnimation> _animation;
        float _frame = 0.0f;
        float _yaw = 0.0f;

        static int HiddenParts(const Formats::Model& model);

        void Show();

        // A reference point in the current frame: the animation moves them.
        Vector3 ReferencePoint(int index) const;
    };
}
