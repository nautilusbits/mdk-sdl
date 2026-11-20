#pragma once

#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"

namespace Mdk::Formats
{
    class Model;
}

namespace Mdk::Game::Kurt
{
    class Kurt;
    class Scope;
}

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game
{
    // Whether frames are drawn between the fixed steps; tests' frames are steps, drawn as they are.
    enum class Blending { Off, On };

    // Frames between the fixed steps (screens above 60 Hz): Kurt is drawn between his last two
    // steps, the objects between their last two ticks, by the time left over. Kurt on a platform or in
    // the XE is drawn with it; what he rides otherwise (the XD2, the board) is drawn with him. A jump
    // (a teleport, faster than MaxSpeed) is drawn where it ends.
    //   steps   │─────1/60─────│─────1/60─────│   Kurt:    previous ──α──► current, α = left over / step
    //   ticks   │────────────1/30─────────────│   objects: last tick ──α──► now, α = since the tick / tick
    //                                  ▲ frame
    class FrameBlend
    {
    public:
        using PoseParts = std::vector<std::vector<Vector3>>;

        // Moving faster than this (u/s; the lift: 750) is a jump.
        static constexpr float MaxSpeed = 1500.f;

        explicit FrameBlend(Blending blending) : _blending(blending) {}

        // How far a frame is from the last tick to the next (0-1; tests: 1, the tick).
        float Tick() const { return _blending == Blending::Off ? 1.f : _tick; }

        // The scope's zoom, tilt and clip timer as drawn.
        float Zoom = 1.f;
        float ScopePitch = 0;
        float ClipTime = 0;

        // Kurt's feet and yaw as drawn.
        Vector3 Feet;
        float Yaw = 0;

        // Kurt after a step: on or in `platform`, riding `driven`, the scripts at `tick`.
        void Record(Kurt::Kurt& kurt, const std::shared_ptr<Objects::MdkObject>& platform,
            const std::shared_ptr<Objects::MdkObject>& driven, int tick);

        // The mouse turned Kurt by `degrees` between steps: drawn at once.
        void Turn(float degrees);

        // A frame: `step` and `tick` of the way from the last step and tick to the next (0-1).
        void Update(Kurt::Kurt& kurt, float step, float tick);

        // An object's transform as drawn.
        Matrix4x4 TransformOf(Objects::MdkObject& obj);

        // An object's place as drawn (tests' traces).
        Vector3 PositionOf(Objects::MdkObject& obj);

        // An object's pose as drawn: between its animation's frames (a scratch pose per
        // model, kept: no allocation per frame).
        const PoseParts& PoseOf(Objects::MdkObject& obj);

        // Where a frame draws a point moving with an object (a rope's end on it).
        Vector3 Carried(Objects::MdkObject& obj, Vector3 point);

        // From `from` to `to` by `t`; 1 gives `to` exactly.
        static Vector3 Lerp(Vector3 from, Vector3 to, float t) { return t >= 1.f ? to : Vector3::Lerp(from, to, t); }
        static float Lerp(float from, float to, float t) { return t >= 1.f ? to : MathF::Lerp(from, to, t); }

        // Degrees, the short way round: 350 to 10 goes through 0.
        static float LerpAngle(float from, float to, float t);

        // Whether a move within `seconds` is too fast to be drawn on the way.
        static bool Jumped(Vector3 from, Vector3 to, float seconds)
        {
            return Vector3::DistanceSquared(from, to) > MaxSpeed * seconds * MaxSpeed * seconds;
        }

    private:
        // Kurt after a step, and what carries him there (its place and yaw, now and a step before).
        struct Pose
        {
            Vector3 Feet;
            float Yaw = 0;
            int Teleports = 0;
            std::shared_ptr<Objects::MdkObject> Carrier;
            Vector3 CarrierAt;
            float CarrierYaw = 0;
            Vector3 CarrierWas;
            float CarrierYawWas = 0;

            bool operator==(const Pose&) const = default;
        };

        // The scope after a step: on or not, its zoom, tilt and clip timer.
        struct Sight
        {
            bool Sniping = false;
            float Zoom = 0;
            float Pitch = 0;
            float Clip = 0;

            bool operator==(const Sight&) const = default;
        };

        // Per model, the pose drawn and its blended parts.
        struct Scratch
        {
            PoseParts Pose;
            PoseParts Parts;
        };

        Blending _blending;
        std::optional<Pose> _previous;
        Pose _current;
        Sight _sightWas;
        Sight _sight;
        float _tick = 1.f;
        // The scripts' tick count at the last step.
        int _ticks = 0;
        // What Kurt rides that follows him (drawn moved with him).
        std::shared_ptr<Objects::MdkObject> _driven;
        Vector3 _lag;
        // Keyed by the model (C#: by reference).
        std::unordered_map<const Formats::Model*, Scratch> _poses;

        // The scope between two steps: the zoom and the clip timer on the way, the tilt with
        // the mouse's since the step. Entering it, a shot or a reload (the timer up) are drawn at once.
        static std::tuple<float, float, float> ScopeAt(const Kurt::Scope& scope, const Sight& from, const Sight& to, float step);

        // Kurt between two steps; on a carrier at either (landing, leaving, or his fall
        // missing it a step), his place and yaw on it: it moves at ticks, he at steps.
        static std::pair<Vector3, float> KurtAt(const Pose& from, const Pose& to, float step, float tick);
    };
}
