#include "game/CutsceneCamera.h"

#include "core/DotNet.h"
#include "game/Angles.h"
#include "game/FrameBlend.h"
#include "game/kurt/FollowCamera.h"
#include "game/scripts/ScriptRuntime.h"

namespace Mdk::Game
{
    using Kurt::CameraMath;
    using Kurt::FollowCamera;
    using Scripts::ScriptRuntime;

    bool CutsceneCamera::Active(const ScriptRuntime& scripts)
    {
        return scripts.Cutscene != 0 && scripts.CameraPoint != Vector3::Zero();
    }

    void CutsceneCamera::Update(const ScriptRuntime& scripts)
    {
        Position = scripts.CameraPoint;
        Forward = ForwardOf(scripts.CameraYaw, scripts.CameraPitch);
        Up = FollowCamera::UpOf(Forward);
        Right = Vector3::Normalize(Vector3::Cross(Forward, Vector3::UnitZ()));

        // A new tick: its shot follows the last one (none after ticks without the camera).
        if (scripts.TickCount() == _tick)
        {
            return;
        }

        auto shot = Shot{ scripts.CameraPoint, scripts.CameraYaw, scripts.CameraPitch };
        _previous = scripts.TickCount() == _tick + 1 ? _shot : shot;
        _shot = shot;
        _tick = scripts.TickCount();
    }

    void CutsceneCamera::Blend(float alpha)
    {
        if (!_previous.has_value() || IsCut(*_previous, _shot))
        {
            return;
        }

        auto from = *_previous;
        Position = FrameBlend::Lerp(from.Point, _shot.Point, alpha);
        Forward = ForwardOf(FrameBlend::LerpAngle(from.Yaw, _shot.Yaw, alpha), FrameBlend::LerpAngle(from.Pitch, _shot.Pitch, alpha));
        Up = FollowCamera::UpOf(Forward);
        Right = Vector3::Normalize(Vector3::Cross(Forward, Vector3::UnitZ()));
    }

    bool CutsceneCamera::IsCut(const Shot& from, const Shot& to)
    {
        return FrameBlend::Jumped(from.Point, to.Point, ScriptRuntime::Tick)
            || MathF::Abs(MathF::IEEERemainder(to.Yaw - from.Yaw, 360.f)) > CutTurn
            || MathF::Abs(MathF::IEEERemainder(to.Pitch - from.Pitch, 360.f)) > CutTurn;
    }

    Vector3 CutsceneCamera::ForwardOf(float yaw, float pitch)
    {
        auto y = DegreesToRadians(yaw);
        auto p = DegreesToRadians(pitch);
        return Vector3(MathF::Sin(y) * MathF::Cos(p), MathF::Cos(y) * MathF::Cos(p), -MathF::Sin(p));
    }

    Engine::Render::View CutsceneCamera::View(float aspect) const
    {
        return CameraMath::View(Position, Forward, Up, CameraMath::KeepWidth(FollowCamera::FieldOfView, aspect), aspect, Near, Far);
    }
}
