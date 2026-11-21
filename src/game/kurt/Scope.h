#pragma once

#include <cfloat>
#include <functional>
#include <optional>

#include "core/DotNet.h"
#include "core/Flags.h"
#include "core/Numerics.h"
#include "game/kurt/Inventory.h"
#include "game/kurt/Kurt.h"

namespace Mdk::Game::Kurt
{
    // Sniper mode's scope (0x573a60, controls 0x467384; godot-mdk kurt.gd and docs/gameplay.md
    // "Sniper mode"): looking around, the zoom, the clip of 3 rounds and the scope's projection.
    //   turn/tilt keys ──► look speeds ──► yaw, pitch (±50°)     slower when zoomed in
    //   zoom keys, wheel ──► zoom speed ──► zoom (1 ... limit)    focal length 384 / zoom
    //   clip timer: 3 ──(4/s)──► 0: loaded, fire ──► +1 (a quarter of a second) ... empty ──► 3
    class Scope
    {
    public:
        enum class Turbo { Off, On };

        // Whether the zoom moves this step (the ZOOM sound loops meanwhile).
        enum class Zooming { Still, Moving };

        // What the clip did this step.
        enum class Clip { None = 0, Loaded = 1, Fired = 2 };

        // The zoom key's state, and fire's.
        enum class Trigger { Released, Held };

        // Zoom (0x57391c, the inverse of the magnification): 1 on entering, down to 0.25 (4x).
        static constexpr float ZoomMin = 0.25f;
        static constexpr float PitchLimit = 50.f;
        static constexpr int ClipSize = 3;
        // Normal bullets: as many as wanted.
        static constexpr int Bullets = 0;
        // Bones' air strike, the last ammo type.
        static constexpr int Strike = 5;
        // The scope's projection (0x57428c): a 384x280 viewport at (127, 139) of the 640x480 screen,
        // centred on (319, 279); the focal length is 384 / zoom pixels.
        static constexpr float Focal = 384.f;
        static Vector2 Centre() { return Vector2(319.f, 279.f); }
        static constexpr float ScreenHeight = 480.f;

        // Degrees, positive looks down (0x573918).
        float Pitch = 0;
        float Zoom = 1.f;
        // How far the zoom goes: ZoomMin, or less on a far locked target.
        float ZoomLimit = ZoomMin;
        int ClipRounds = 0;
        float ClipTime = 0;

        // Entering sniper mode: level, zoomed out, an empty clip.
        void Reset();

        // The turn (right positive) and tilt (down positive) keys, and the mouse (degrees at
        // zoom 1), turn the view. Returns the yaw change in degrees.
        float Look(float turn, float tilt, Vector2 mouse, Turbo turbo, float delta);

        // Wheel notches (positive away from the user: zoom in) hold a zoom key for a while.
        void Wheel(float notches);

        // The zoom (0x4687a4): direction -1 zooms in, 1 out. Zooming out multiplies it by
        // 1 + v each tick, zooming in divides it, between 1 and the limit.
        Zooming UpdateZoom(float direction, float delta);

        // The clip (0x41eb10) and firing (0x461e88): while the timer runs, rounds load one per
        // step up to 3 (only as many as there's ammo for, except normal bullets); a shot needs the timer at
        // 0, 5 ticks since the last one, and fire to find a free round slot.
        Clip UpdateClip(Inventory& inventory, Trigger trigger, const std::function<bool(int)>& fire, float delta);

        // The next (step 1) or previous (-1) ammo type with rounds (0x46c900); the clip reloads.
        void SelectAmmo(Inventory& inventory, int step);

        // The eye for Kurt's feet and facing.
        Vector3 Eye(Vector3 feet, Vector3 facing) const { return Eye(feet, facing, Pitch); }

        // The eye for Kurt's feet and facing at pitch (a frame between steps).
        static Vector3 Eye(Vector3 feet, Vector3 facing, float pitch);

        // The line of sight for a yaw (degrees) and this pitch.
        Vector3 Forward(float yaw) const { return Direction(yaw, Pitch); }

        // A direction from a yaw and a pitch (degrees, positive down).
        static Vector3 Direction(float yaw, float pitch);

        // Where a point shows on the 640x480 screen, seen from eye along forward; null behind the eye.
        static std::optional<Vector2> ToScreen(Vector3 eye, Vector3 forward, float zoom, Vector3 point);

        // Whether a screen box overlaps the lock square around the crosshair.
        static bool Locks(Vector2 min, Vector2 max)
        {
            return min.X <= Centre().X + LockHalf && max.X >= Centre().X - LockHalf && min.Y <= Centre().Y + LockHalf && max.Y >= Centre().Y - LockHalf;
        }

        // The zoom limit on a locked target of this height at this distance.
        static float LockLimit(float height, float distance)
        {
            return MathF::Min(ZoomMin, LockZoomFactor * MathF::Max(height, LockMinHeight) / MathF::Max(distance, 1.f));
        }

    private:
        // Looking around: degrees per second (0.4/0.6 per tick², at most 4/6 per tick), scaled by
        // zoom x 0.416667.
        static constexpr float LookAcceleration = 0.4f * Kurt::Ticks * Kurt::Ticks;
        static constexpr float LookAccelerationTurbo = 0.6f * Kurt::Ticks * Kurt::Ticks;
        static constexpr float LookSpeed = 4.f * Kurt::Ticks;
        static constexpr float LookSpeedTurbo = 6.f * Kurt::Ticks;
        static constexpr float LookFrictionSlow = 1.0667f * Kurt::Ticks * Kurt::Ticks;
        static constexpr float LookFrictionFast = 1.6f * Kurt::Ticks * Kurt::Ticks;
        static constexpr float LookScale = 0.416667f;
        // The zoom speed grows by 0.01 per tick up to 0.15 while a zoom key is held and decays by 0.015.
        static constexpr float ZoomAcceleration = 0.01f;
        static constexpr float ZoomMaxSpeed = 0.15f;
        static constexpr float ZoomDecay = 0.015f;
        // A wheel notch holds the zoom key for this many ticks (the Godot port's).
        static constexpr float WheelTicks = 6.f;
        // The clip timer (0x5743eb) drops by 4 per second; a shot adds 1, an empty clip sets 3.
        static constexpr float ClipDecay = 4.f;
        static constexpr float ShotTime = 1.f;
        static constexpr float ReloadTime = 3.f;
        // A shot needs 5 ticks since the last one.
        static constexpr float ShotGap = 5.f / Kurt::Ticks;
        // Normal bullets never run out.
        static constexpr int EndlessStock = 999;
        // The target lock's square around the crosshair (0x43b65c), pixels.
        static constexpr float LockHalf = 32.f;
        // The zoom limit on a locked target: 0.375 x its height (at least 10) / distance (0x4678b0).
        static constexpr float LockZoomFactor = 0.375f;
        static constexpr float LockMinHeight = 10.f;
        // The eye: 4 above the feet, and forward by 5 (1 - cos pitch) looking down.
        static constexpr float EyeHeight = 4.f;
        static constexpr float EyeForward = 5.f;

        Vector2 _lookSpeed{};
        float _zoomSpeed = 0;
        float _wheelTicks = 0;
        float _wheelDirection = 0;
        float _sinceShot = FLT_MAX;

        static float Accelerate(float speed, float input, Turbo turbo, float delta);

        static float MoveToward(float from, float to, float step)
        {
            return MathF::Abs(to - from) <= step ? to : from + MathF::Sign(to - from) * step;
        }
    };

    MDK_FLAGS(Scope::Clip)
}
