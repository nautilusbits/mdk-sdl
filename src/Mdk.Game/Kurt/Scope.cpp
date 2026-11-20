#include "Mdk.Game/Kurt/Scope.h"

#include "Mdk.Game/Angles.h"

namespace Mdk::Game::Kurt
{
    // Entering sniper mode: level, zoomed out, an empty clip.
    void Scope::Reset()
    {
        Pitch = 0.f;
        Zoom = 1.f;
        ClipRounds = 0;
        ClipTime = 0.f;
        _lookSpeed = Vector2::Zero();
        _zoomSpeed = 0.f;
        _wheelTicks = 0.f;
        _sinceShot = FLT_MAX;
    }

    // The turn (right positive) and tilt (down positive) keys, and the mouse (degrees at
    // zoom 1), turn the view. Returns the yaw change in degrees.
    float Scope::Look(float turn, float tilt, Vector2 mouse, Turbo turbo, float delta)
    {
        _lookSpeed.X = Accelerate(_lookSpeed.X, turn, turbo, delta);
        _lookSpeed.Y = Accelerate(_lookSpeed.Y, tilt, turbo, delta);
        float scale = Zoom * LookScale * delta;
        Pitch = Math::Clamp(Pitch + _lookSpeed.Y * scale + mouse.Y * Zoom, -PitchLimit, PitchLimit);
        return -(_lookSpeed.X * scale + mouse.X * Zoom);
    }

    float Scope::Accelerate(float speed, float input, Turbo turbo, float delta)
    {
        if (input == 0.f)
        {
            float friction = MathF::Abs(speed) > LookSpeed ? LookFrictionFast : LookFrictionSlow;
            return MoveToward(speed, 0.f, friction * delta);
        }

        float max = turbo == Turbo::On ? LookSpeedTurbo : LookSpeed;
        if (speed * input < 0.f)
        {
            speed = 0.f;
        }

        float acceleration = turbo == Turbo::On ? LookAccelerationTurbo : LookAcceleration;
        return Math::Clamp(speed + input * acceleration * delta, -max, max);
    }

    // Wheel notches (positive away from the user: zoom in) hold a zoom key for a while.
    void Scope::Wheel(float notches)
    {
        if (notches == 0.f)
        {
            return;
        }

        float direction = notches > 0.f ? -1.f : 1.f;
        float ticks = WheelTicks * MathF::Abs(notches);
        _wheelTicks = direction != _wheelDirection ? ticks : _wheelTicks + ticks;
        _wheelDirection = direction;
    }

    // The zoom (0x4687a4): direction -1 zooms in, 1 out. Zooming out
    // multiplies it by 1 + v each tick, zooming in divides it, between 1 and the limit.
    Scope::Zooming Scope::UpdateZoom(float direction, float delta)
    {
        float ticks = delta * Kurt::Ticks;
        if (_wheelTicks > 0.f)
        {
            _wheelTicks -= ticks;
            if (direction == 0.f)
            {
                direction = _wheelDirection;
            }
        }

        if (direction != 0.f)
        {
            _zoomSpeed = Math::Clamp(_zoomSpeed + direction * ZoomAcceleration * ticks, -ZoomMaxSpeed, ZoomMaxSpeed);
        }
        else
        {
            _zoomSpeed = MoveToward(_zoomSpeed, 0.f, ZoomDecay * ticks);
        }

        if (_zoomSpeed > 0.f)
        {
            Zoom = MathF::Min(Zoom * MathF::Pow(1.f + _zoomSpeed, ticks), 1.f);
        }
        else if (_zoomSpeed < 0.f)
        {
            // Past a lost lock's limit, the zoom stays: snapping back to it each time the lock
            // flickers jerks the view (a target at the lock square's edge leaves it zooming in).
            Zoom = MathF::Max(Zoom / MathF::Pow(1.f - _zoomSpeed, ticks), MathF::Min(ZoomLimit, Zoom));
        }

        bool moving = direction != 0.f && (Zoom < 1.f || direction < 0.f) && (Zoom > ZoomLimit || direction > 0.f);
        return moving ? Zooming::Moving : Zooming::Still;
    }

    // The clip (0x41eb10) and firing (0x461e88): while the timer runs, rounds load one per
    // step up to 3 (only as many as there's ammo for, except normal bullets); a shot needs the timer at
    // 0, 5 ticks since the last one, and fire to find a free round slot.
    Scope::Clip Scope::UpdateClip(Inventory& inventory, Trigger trigger, const std::function<bool(int)>& fire, float delta)
    {
        Clip result = Clip::None;
        _sinceShot += delta;
        int type = inventory.SelectedAmmo;
        int stock = type == Bullets ? EndlessStock : inventory.Ammo()[type - 1];
        if (ClipTime > 0.f && ClipRounds < Math::Min(ClipSize, stock))
        {
            ClipRounds++;
            result |= Clip::Loaded;
        }

        if (ClipRounds == 0 && ClipTime <= 0.f)
        {
            if (type != Bullets && stock <= 0)
            {
                inventory.SelectAmmo(Bullets);
            }

            ClipTime = ReloadTime;
        }

        ClipTime = MathF::Max(ClipTime - ClipDecay * delta, 0.f);
        if (trigger == Trigger::Released || ClipTime > 0.f || _sinceShot < ShotGap || ClipRounds <= 0 || !fire(type))
        {
            return result;
        }

        _sinceShot = 0.f;
        ClipRounds--;
        inventory.UseAmmo(type);
        ClipTime += ShotTime;
        if (ClipRounds == 0)
        {
            ClipTime = ReloadTime;
        }

        return result | Clip::Fired;
    }

    // The next (step 1) or previous (-1) ammo type with rounds
    // (0x46c900); the clip reloads.
    void Scope::SelectAmmo(Inventory& inventory, int step)
    {
        int type = inventory.NextAmmo(step);
        if (type == inventory.SelectedAmmo)
        {
            return;
        }

        inventory.SelectAmmo(type);
        ClipRounds = 0;
        ClipTime = ReloadTime;
    }

    // The eye for Kurt's feet and facing at pitch (a frame between steps).
    Vector3 Scope::Eye(Vector3 feet, Vector3 facing, float pitch)
    {
        Vector3 eye = feet + Vector3::UnitZ() * EyeHeight;
        if (pitch > 0.f)
        {
            eye += facing * EyeForward * (1.f - MathF::Cos(DegreesToRadians(pitch)));
        }

        return eye;
    }

    // A direction from a yaw and a pitch (degrees, positive down).
    Vector3 Scope::Direction(float yaw, float pitch)
    {
        float y = DegreesToRadians(yaw);
        float p = DegreesToRadians(pitch);
        return Vector3(MathF::Cos(y) * MathF::Cos(p), MathF::Sin(y) * MathF::Cos(p), -MathF::Sin(p));
    }

    // Where a point shows on the 640x480 screen, seen from eye along
    // forward; null behind the eye.
    std::optional<Vector2> Scope::ToScreen(Vector3 eye, Vector3 forward, float zoom, Vector3 point)
    {
        Vector3 right = Vector3::Normalize(Vector3::Cross(forward, Vector3::UnitZ()));
        Vector3 up = Vector3::Cross(right, forward);
        Vector3 offset = point - eye;
        float depth = Vector3::Dot(offset, forward);
        if (depth <= 0.f)
        {
            return std::nullopt;
        }

        float focal = Focal / zoom;
        return Centre() + Vector2(Vector3::Dot(offset, right), -Vector3::Dot(offset, up)) * focal / depth;
    }
}
