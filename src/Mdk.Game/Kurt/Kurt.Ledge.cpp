#include "Mdk.Game/Kurt/Kurt.h"

#include "Mdk.Game/Angles.h"

// Ledges (damp_ledge_grab 0x469868, kurt.gd _grab_ledge, _update_climb; godot-mdk docs/gameplay.md
// "Vertical"): falling while running forward, Kurt grabs a flat top his hands cross and climbs onto it.
//        hands' path (4.6 above the feet, 1-3 ahead)
//   ─ ─ ─ ─ ─ ─●─ ─ ─►   crosses the top ──► its edge towards Kurt, faced within 30°,
//          ┌────┴──────   room above it ──► HANG: 1 before the edge, 4.6 below it
//   Kurt ► │  wall          K_HANG, 2 ticks a frame, climbs by the tables (≈ 4.3 up, 1.35 on)

namespace Mdk::Game::Kurt
{
    using Collision::ArenaSpace;

    bool Kurt::GrabLedge(Vector3 before, float forward)
    {
        if (forward <= 0.f || VerticalSpeed > LedgeSpeed || Health == 0 || Current == State::Dead || Current == State::Knocked
            || Current == State::GetUp)
        {
            return false;
        }

        auto facing = Facing();
        auto hands = Vector3(0.f, 0.f, LedgeHeight);
        for (auto i = 1; i <= LedgeReach; i++)
        {
            auto ahead = facing * static_cast<float>(i);
            auto top = _space.FloorCrossing(before + hands + ahead, Feet + hands + ahead);
            if (!top)
            {
                continue;
            }

            return MathF::Abs(top->Normal().Z) >= LedgeFlat && HangFrom(*top, ahead);
        }

        return false;
    }

    bool Kurt::HangFrom(const ArenaSpace::Surface& top, Vector3 ahead)
    {
        auto found = ArenaSpace::Edge(top, top.Point - ahead);
        if (!found)
        {
            return false;
        }

        auto [edge, direction] = *found;
        auto yaw = RadiansToDegrees(MathF::Atan2(direction.Y, direction.X)) + EdgeToYaw;
        auto turn = MathF::Abs(Wrap180(yaw - Yaw));
        auto facing = Facing();
        auto room = edge + Vector3(0.f, 0.f, LedgeRoomLift);
        if (turn > LedgeAngle || !_space.Free(room - facing * HangBack, room + facing * LedgeRoomPast, LedgeRoom()))
        {
            return false;
        }

        Yaw = yaw;
        Feet = edge - facing * HangBack - Vector3(0.f, 0.f, LedgeHeight);
        ForwardSpeed = 0.f;
        StrafeSpeed = 0.f;
        VerticalSpeed = 0.f;
        OnFloor = false;
        StopFiring();
        _mixer.Stop(ChuteOnSound);
        ChuteOpen = false;
        _climbTicks = 0;
        SetState(State::Hang);
        return true;
    }

    void Kurt::UpdateClimb(float delta)
    {
        StateTime += delta;
        auto ticks = ToInt(StateTime * Ticks) - _climbTicks;
        auto facing = Facing();
        for (auto i = 0; i < ticks; i++)
        {
            _climbTicks++;
            auto k = (_climbTicks + 1) >> 1;
            if (k < ClimbSteps)
            {
                Feet += Vector3(0.f, 0.f, (ClimbHeights[k] - ClimbHeights[k - 1]) * ClimbScale);
                Feet -= facing * ((ClimbBack[k] - ClimbBack[k - 1]) * ClimbScale);
            }
        }

        VerticalSpeed = 0.f;
        auto frames = _frameCount(State::Hang);
        if (_climbTicks >= frames * TicksPerClimbFrame - 1)
        {
            SetState(State::Still);
            return;
        }

        AnimationFrame = MathF::Min(static_cast<float>(_climbTicks / TicksPerClimbFrame), static_cast<float>(frames - 1));
    }
}
