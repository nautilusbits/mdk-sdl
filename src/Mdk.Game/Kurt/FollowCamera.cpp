#include "Mdk.Game/Kurt/FollowCamera.h"

#include <array>

#include "Mdk.Game/Angles.h"
#include "Mdk.Game/FrameBlend.h"
#include "Mdk.Game/Kurt/CameraRoll.h"
#include "Mdk.Game/Kurt/Scope.h"

namespace Mdk::Game::Kurt
{
    void FollowCamera::Update(Kurt& kurt, float arenaPitch, Engine::Platform::Input& input, float delta)
    {
        if (kurt.TopDownHeight.has_value())
        {
            LookDown(kurt, *kurt.TopDownHeight);
            return;
        }

        _arenaPitch = MathF::Lerp(arenaPitch, _arenaPitch, MathF::Pow(PitchEase, delta * Kurt::Ticks));
        // Through the scope the mouse turns the scope instead.
        if (!kurt.Sniping)
        {
            _lookOffset += input.MouseY() * MouseDegrees;
        }

        UpdateAirTime(kurt, delta);
        UpdateClimb(kurt.Feet.Z, delta);
        if (kurt.Rising)
        {
            // The end of a level lifts Kurt: no tilt in the air (end_level.gd).
            _airPitch = 0.f;
        }
        else if (_airTime == 0.f)
        {
            _airPitch = MathF::Max(_airPitch - AirPitchDecay * delta, 0.f);
        }
        else
        {
            _airPitch = MathF::Max(_airPitch, MathF::Min(_airTime * AirPitchRate, AirPitchMax));
        }

        float autoPitch = _arenaPitch + _airPitch - ClimbPitch * _climb + kurt.CameraTilt;
        _lookOffset = ClampLook(_lookOffset, autoPitch);
        float pitch = ViewPitch(autoPitch + _lookOffset);
        Vector3 facing = kurt.Facing();
        Position = Point(kurt.Feet, facing, pitch, kurt.CameraPivot);
        if (_space != nullptr && !kurt.Sniping && !kurt.Frozen && kurt.Current != Kurt::State::Dead)
        {
            Position = Clear(kurt, *_space, Position, delta);
        }

        Orient(facing, pitch);
        UpdateShake(delta);
        Turn(ShakeOffset * DegreesToRadians(FieldOfView) / ViewHeight, DegreesToRadians(kurt.CameraRoll->Roll));

        Aim aim{autoPitch, kurt.CameraPivot, kurt.CameraRoll->Roll};
        _previousAim = !_previousAim.has_value() ? aim : _aim;
        _aim = aim;
    }

    // The view's pitch (radians) for degrees down, short of straight up or
    // down: at 90° the view's up has no way to point, and the float cosine's sign turned it over.
    float FollowCamera::ViewPitch(float degrees)
    {
        return DegreesToRadians(Math::Clamp(degrees, -ViewPitchLimit, ViewPitchLimit));
    }

    // Looks along facing, pitch (radians) down.
    void FollowCamera::Orient(Vector3 facing, float pitch)
    {
        Forward = Vector3::Normalize(facing * MathF::Cos(pitch) - Vector3::UnitZ() * MathF::Sin(pitch));
        Up = Vector3::Normalize(Vector3::Cross(Vector3::Cross(Forward, Vector3::UnitZ()), Forward));
        Right = Vector3::Normalize(Vector3::Cross(Forward, Vector3::UnitZ()));
    }

    // A frame's mouse look up and down, at once rather than with the next step (screens
    // above 60 Hz).
    void FollowCamera::MouseLook(Kurt& kurt, Engine::Platform::Input& input)
    {
        if (kurt.TopDownHeight.has_value() || kurt.Sniping)
        {
            return;
        }

        _lookOffset = ClampLook(_lookOffset + input.TakeMouseY() * MouseDegrees, _aim.Pitch);
    }

    // A frame between steps: the camera placed again for Kurt as drawn (feet,
    // yaw), its pitch, pivot and roll alpha of the way between
    // the last two steps', the mouse's look as it is now. (The camera's clearance moved Kurt, not
    // the camera: it's where his feet put it.) The next step places it anew.
    void FollowCamera::Blend(const Kurt& kurt, Vector3 feet, float yaw, float alpha)
    {
        float yawRadians = DegreesToRadians(yaw);
        Vector3 facing(MathF::Cos(yawRadians), MathF::Sin(yawRadians), 0.f);
        if (kurt.TopDownHeight.has_value())
        {
            Position = feet + Vector3::UnitZ() * *kurt.TopDownHeight;
            Forward = -Vector3::UnitZ();
            Up = facing;
            Right = Vector3(facing.Y, -facing.X, 0.f);
            return;
        }

        if (!_previousAim.has_value())
        {
            return;
        }

        const Aim& from = *_previousAim;
        float pitch = ViewPitch(FrameBlend::Lerp(from.Pitch, _aim.Pitch, alpha) + _lookOffset);
        Position = Point(feet, facing, pitch, FrameBlend::Lerp(from.Pivot, _aim.Pivot, alpha));
        Orient(facing, pitch);
        Turn(ShakeOffset * DegreesToRadians(FieldOfView) / ViewHeight, DegreesToRadians(FrameBlend::Lerp(from.Roll, _aim.Roll, alpha)));
    }

    // Turns the view by the shake (radians across and up), then rolls it: a positive roll
    // turns its up towards its right.
    void FollowCamera::Turn(Vector2 shake, float roll)
    {
        if (shake != Vector2::Zero())
        {
            Forward = Vector3::Normalize(Forward + Right * MathF::Tan(shake.X) + Up * MathF::Tan(shake.Y));
            Right = Vector3::Normalize(Vector3::Cross(Forward, Up));
            Up = Vector3::Normalize(Vector3::Cross(Right, Forward));
        }

        if (roll == 0.f)
        {
            return;
        }

        Vector3 up = Up;
        Vector3 right = Right;
        Up = up * MathF::Cos(roll) + right * MathF::Sin(roll);
        Right = right * MathF::Cos(roll) - up * MathF::Sin(roll);
    }

    // A new random offset each tick while the shake is above 1; it drains by 0.25 a tick.
    void FollowCamera::UpdateShake(float delta)
    {
        if (_shake <= 0.f)
        {
            ShakeOffset = Vector2::Zero();
            return;
        }

        _shakeTime += delta * Kurt::Ticks;
        while (_shakeTime >= 1.f)
        {
            _shakeTime -= 1.f;
            ShakeOffset = Vector2::Zero();
            if (_shake > ShakeMin)
            {
                float amplitude = _shake * ShakeScale;
                // Two statements: C# evaluates the arguments left to right.
                float x = Random(amplitude);
                float y = Random(amplitude);
                Vector2 offset(x, y);
                ShakeOffset = Vector2::Round(Vector2::Clamp(offset, -ShakeLimit(), ShakeLimit()));
            }

            _shake -= ShakeDrain;
            if (_shake < ShakeDrain)
            {
                _shake = 0.f;
            }
        }
    }

    float FollowCamera::Random(float amplitude)
    {
        return (_random.NextSingle() * 2.f - 1.f) * amplitude;
    }

    // The camera's clearance (camera_clearance 0x417ee8): a wall between Kurt's head and the
    // camera pushes Kurt d · n away from it (d: how far the camera is past it, n: its horizontal
    // normal towards him) if there's floor there (or half of d to a side); then the solid objects in
    // the way push him by what they cut off. The camera moves as far as Kurt did.
    Vector3 FollowCamera::Clear(Kurt& kurt, Collision::ArenaSpace& arenas, Vector3 camera, float delta)
    {
        Vector3 head = kurt.Feet + Vector3::UnitZ() * HeadHeight;
        if (auto hit = arenas.Sight(head, camera, SightBox()))
        {
            auto& [point, plane] = *hit;
            Vector2 normal(plane.Normal.X, plane.Normal.Y);
            if (plane.Distance(head) < 0.f)
            {
                normal = -normal;
            }

            float d = Vector2::Distance(Vector2(camera.X, camera.Y), Vector2(point.X, point.Y));
            if (std::optional<Vector2> pushed = Push(kurt, arenas, normal * d, Vector2(normal.Y, -normal.X) * (d * PushSide)))
            {
                Vector2 push = *pushed;
                // Eased in (MaxPushSpeed): what's left is pushed on the next steps.
                float limit = MaxPushSpeed * delta;
                if (push.Length() > limit)
                {
                    push = Vector2::Normalize(push) * limit;
                }

                camera += kurt.Shove(Vector3(push, 0.f));
            }
        }

        if (!ClipView)
        {
            return camera;
        }

        Vector3 end = ClipView(kurt.Feet + Vector3::UnitZ() * HeadHeight, camera);
        // A solid object (a door) behind Kurt shoves him on by up to ~8 units in one step: eased in too.
        Vector3 offset = end - camera;
        float clipLimit = MaxPushSpeed * delta;
        if (offset.Length() > clipLimit)
        {
            offset = Vector3::Normalize(offset) * clipLimit;
        }

        return camera + kurt.Shove(offset);
    }

    // The push, or a push to a side, with a floor under it (in the air: the push), or null.
    std::optional<Vector2> FollowCamera::Push(Kurt& kurt, Collision::ArenaSpace& arenas, Vector2 push, Vector2 side)
    {
        if (!kurt.OnFloor)
        {
            return push;
        }

        for (Vector2 offset : std::array<Vector2, 3>{push, push + side, push - side})
        {
            Vector3 at = kurt.Feet + Vector3(offset, 0.f);
            if (arenas.Crosses(at + Vector3::UnitZ() * PushFloorRange, at - Vector3::UnitZ() * PushFloorRange))
            {
                return offset;
            }
        }

        return std::nullopt;
    }

    // The bomber's view (0x4183f0): straight down from height above
    // Kurt, his heading at the top.
    void FollowCamera::LookDown(const Kurt& kurt, float height)
    {
        Position = kurt.Feet + Vector3::UnitZ() * height;
        Forward = -Vector3::UnitZ();
        Up = kurt.Facing();
        Right = kurt.Right();
    }

    // Air time (0x573a48): it starts once Kurt falls faster than 16 u/s and ends when he
    // rises or stands still on a floor. Running downhill loses the floor for single steps without
    // tilting the view.
    void FollowCamera::UpdateAirTime(const Kurt& kurt, float delta)
    {
        float vz = kurt.VerticalSpeed;
        if (_airTime == 0.f)
        {
            _airTime = vz < FallStartSpeed ? delta : 0.f;
            return;
        }

        bool landed = vz == 0.f && kurt.OnFloor;
        _airTime = vz > 0.f || landed ? 0.f : _airTime + delta;
    }

    // The smoothed rise of the feet per tick (camera_update 0x4174d0), measured once a
    // tick as the original's frames at 30 per second: Kurt's 60 steps a second go down a slope
    // every other step, which would shake the view.
    void FollowCamera::UpdateClimb(float feetZ, float delta)
    {
        if (!_lastFeetZ.has_value())
        {
            _lastFeetZ = feetZ;
        }

        _climbTime += delta * Kurt::Ticks;
        if (_climbTime < 1.f)
        {
            return;
        }

        float ticks = MathF::Floor(_climbTime);
        _climbTime -= ticks;
        float rise = Math::Clamp((feetZ - *_lastFeetZ) / ticks, -ClimbClamp, ClimbClamp);
        _lastFeetZ = feetZ;
        _climb = Climb(_climb, rise, ticks);
    }

    // One step of the climb: eased towards the rise; going the other way it also comes
    // back by 0.02 per tick, without passing the rise.
    float FollowCamera::Climb(float climb, float rise, float ticks)
    {
        float keep = MathF::Pow(ClimbKeep, ticks);
        climb = rise * (1.f - keep) + climb * keep;
        if (climb < 0.f && rise >= 0.f)
        {
            return MathF::Min(climb + ClimbReturn * ticks, rise);
        }

        if (climb > 0.f && rise <= 0.f)
        {
            return MathF::Max(climb - ClimbReturn * ticks, rise);
        }

        return climb;
    }

    // The camera's place for the feet and pitch (radians).
    Vector3 FollowCamera::Point(Vector3 feet, Vector3 facing, float pitch, float pivot)
    {
        float distance = Distance;
        float back = -distance * MathF::Cos(pitch);
        if (pitch > 0.f)
        {
            back += DownBack * (1.f - MathF::Cos(pitch));
        }
        else if (pitch < DegreesToRadians(CloseUpPitch))
        {
            distance = Distance * (RadiansToDegrees(pitch) + CloseUpBias) / CloseUpRange;
            back = -distance * MathF::Cos(pitch);
        }

        return feet + facing * back + Vector3::UnitZ() * (pivot + distance * MathF::Sin(pitch));
    }

    Engine::Render::View FollowCamera::View(float aspect) const
    {
        return CameraMath::View(Position, Forward, Up, CameraMath::KeepWidth(FieldOfView, aspect), aspect, Near, Far);
    }

    // Sniper mode: the view from Kurt's eye through the scope.
    Engine::Render::View FollowCamera::SniperView(const Kurt& kurt, float aspect)
    {
        return SniperView(kurt.SniperEye(), kurt.SniperForward(), kurt.Scope->Zoom, aspect);
    }

    // Sniper mode's view from eye (a frame between steps).
    Engine::Render::View FollowCamera::SniperView(Vector3 eye, Vector3 forward, float zoom, float aspect)
    {
        return CameraMath::ScopeView(eye, forward, zoom, aspect, Near, Far);
    }

    // A vertical field of view that keeps a 4:3 view's width on a narrower screen (a
    // near-square foldable sees as far left and right, more above and below); wider screens see
    // more at the sides at the same height.
    //   aspect ≥ 4:3: fov (Hor+)    aspect < 4:3: 2 atan(tan(fov / 2) × (4/3) / aspect) (Vert-)
    float CameraMath::KeepWidth(float fieldOfView, float aspect)
    {
        if (aspect >= OriginalAspect || aspect <= 0.f)
        {
            return fieldOfView;
        }

        float half = MathF::Tan(DegreesToRadians(fieldOfView) / 2.f) * OriginalAspect / aspect;
        return RadiansToDegrees(2.f * MathF::Atan(half));
    }

    Engine::Render::View CameraMath::View(Vector3 position, Vector3 forward, Vector3 up, float fieldOfView, float aspect, float nearZ, float farZ)
    {
        Matrix4x4 view = Matrix4x4::CreateLookAt(position, position + forward, up);
        Matrix4x4 projection = Matrix4x4::CreatePerspectiveFieldOfView(DegreesToRadians(fieldOfView), aspect, nearZ, farZ);

        // The sky only turns with the camera: its rotation alone, inverted.
        Matrix4x4 rotation = Matrix4x4::CreateLookAt(Vector3::Zero(), forward, up);
        Matrix4x4 clipToDirection;
        Matrix4x4::Invert(rotation * projection, clipToDirection);
        return Engine::Render::View{view * projection, clipToDirection, position, Engine::Render::ViewCamera{forward, up, projection, nearZ}};
    }

    // Sniper mode's view (0x57428c): the focal length is 384 / zoom pixels of the 480-high
    // screen, and the frustum is shifted down so that the scope's centre (y 279) is on the line of
    // sight.
    //   y 0   ┌───────────┐  top    =  279 · near / focal
    //         │     +     │  ◄── line of sight, y 279
    //   y 480 └───────────┘  bottom = -201 · near / focal
    Engine::Render::View CameraMath::ScopeView(Vector3 eye, Vector3 forward, float zoom, float aspect, float nearZ, float farZ)
    {
        Vector3 up = FollowCamera::UpOf(forward);
        Matrix4x4 view = Matrix4x4::CreateLookAt(eye, eye + forward, up);
        Matrix4x4 projection = ScopeProjection(zoom, aspect, nearZ, farZ);
        Matrix4x4 rotation = Matrix4x4::CreateLookAt(Vector3::Zero(), forward, up);
        Matrix4x4 clipToDirection;
        Matrix4x4::Invert(rotation * projection, clipToDirection);
        return Engine::Render::View{view * projection, clipToDirection, eye, Engine::Render::ViewCamera{forward, up, projection, nearZ}};
    }

    // The scope's off-centre projection for a window of aspect.
    Matrix4x4 CameraMath::ScopeProjection(float zoom, float aspect, float nearZ, float farZ)
    {
        float unit = nearZ * zoom / Scope::Focal;
        float halfWidth = Scope::ScreenHeight / 2.f * aspect * unit;
        float top = Scope::Centre().Y * unit;
        float bottom = -(Scope::ScreenHeight - Scope::Centre().Y) * unit;
        return Matrix4x4::CreatePerspectiveOffCenter(-halfWidth, halfWidth, bottom, top, nearZ, farZ);
    }
}
