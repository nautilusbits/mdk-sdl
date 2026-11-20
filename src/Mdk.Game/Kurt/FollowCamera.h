#pragma once

#include <functional>
#include <optional>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"

namespace Mdk::Game::Kurt
{
    // The third-person camera behind Kurt (camera_update). Its pitch (positive looks
    // down) is the arena's (DTI), eased in, plus the mouse's look offset, a tilt in the air and
    // -40 × the smoothed rise of the feet per tick (climbing looks up, falling down). It rolls with
    // Kurt's moves (CameraRoll) and shakes (0x573aa8). It never gets closer: a wall or a
    // solid object between Kurt's head and the camera pushes Kurt away instead (camera_clearance).
    //        camera ●                   wall │    camera ●         wall │  camera ●
    //                 \  pitch               │  ◄ Kurt      ──►         │     ◄ ◄ Kurt (pushed d · n)
    //   pivot 4.5 ─────● Kurt's feet + up
    class FollowCamera
    {
    public:
        // Vertical field of view in degrees (the Godot port's, matching the original's view).
        static constexpr float FieldOfView = 71.5f;

        explicit FollowCamera(Collision::ArenaSpace* space = nullptr) : _space(space) {}

        Vector3 Position{};
        Vector3 Forward = Vector3::UnitY();
        Vector3 Up = Vector3::UnitZ();
        Vector3 Right = Vector3::UnitX();
        // The solid objects of Kurt's arena that block the view (flag 0x1000000): where the line
        // of sight first meets one of their parts, or its end.
        std::function<Vector3(Vector3, Vector3)> ClipView;
        // The shake's offset this tick (pixels of the 360-high view).
        Vector2 ShakeOffset{};

        // The mouse's look to start with (degrees, positive down; --look). C#'s init-only Look.
        void SetLook(float value) { _lookOffset = value; }

        // Shakes the screen at least this much (0x467f7c, opcode 135).
        void RaiseShake(float amount) { _shake = MathF::Max(_shake, amount); }

        void Update(Kurt& kurt, float arenaPitch, Engine::Platform::Input& input, float delta);

        // A frame's mouse look up and down, at once rather than with the next step (screens
        // above 60 Hz).
        void MouseLook(Kurt& kurt, Engine::Platform::Input& input);

        // A frame between steps: the camera placed again for Kurt as drawn (feet, yaw), its pitch,
        // pivot and roll alpha of the way between the last two steps', the mouse's look as it is
        // now. (The camera's clearance moved Kurt, not the camera: it's where his feet put it.) The
        // next step places it anew.
        void Blend(const Kurt& kurt, Vector3 feet, float yaw, float alpha);

        // One step of the climb: eased towards the rise; going the other way it also comes
        // back by 0.02 per tick, without passing the rise.
        static float Climb(float climb, float rise, float ticks);

        Engine::Render::View View(float aspect) const;

        // Sniper mode: the view from Kurt's eye through the scope.
        static Engine::Render::View SniperView(const Kurt& kurt, float aspect);

        // Sniper mode's view from eye (a frame between steps).
        static Engine::Render::View SniperView(Vector3 eye, Vector3 forward, float zoom, float aspect);

        // The camera's up for a line of sight (no roll).
        static Vector3 UpOf(Vector3 forward)
        {
            return Vector3::Normalize(Vector3::Cross(Vector3::Cross(forward, Vector3::UnitZ()), forward));
        }

    private:
        static constexpr float Distance = 8.f;
        static constexpr float MouseDegrees = 0.15f;
        // The mouse's look stops at this pitch (the original's keys: -60° to 90°, back to 0 once
        // let go). Further down Kurt's sprite lies flat under the camera and floors ahead hide it;
        // further up the camera sinks to his feet, through the floor on a slope.
        static constexpr float LookUpLimit = -30.f;
        static constexpr float LookDownLimit = 60.f;
        // The view's pitch stays short of straight up or down: past it the view turns over.
        static constexpr float ViewPitchLimit = 89.f;
        static constexpr float AirPitchRate = 0.667f * Kurt::Ticks;
        static constexpr float AirPitchMax = 40.f;
        static constexpr float AirPitchDecay = 40.f;
        // Falling faster than this (u/s) starts the air time.
        static constexpr float FallStartSpeed = -16.f;
        // The arena pitch eases in by 0.85·old + 0.15·new per tick.
        static constexpr float PitchEase = 0.85f;
        static constexpr float DefaultPitch = 4.f;
        // Looking far up brings the camera closer: its distance shrinks below -20°.
        static constexpr float CloseUpPitch = -20.f;
        static constexpr float CloseUpRange = 80.f;
        static constexpr float CloseUpBias = 100.f;
        // Looking down lifts the camera back towards Kurt by up to 5.
        static constexpr float DownBack = 5.f;
        static constexpr float Near = 0.5f;
        static constexpr float Far = 20000.f;
        // The climb (0x490db8): the rise per tick, clamped to ±0.5, eased in by 0.97·old +
        // 0.03·new per tick, back towards a rise of the other sign by 0.02 per tick; 40° per unit.
        static constexpr float ClimbClamp = 0.5f;
        static constexpr float ClimbKeep = 0.97f;
        static constexpr float ClimbReturn = 0.02f;
        static constexpr float ClimbPitch = 40.f;
        // The camera's clearance (0x417ee8): the line of sight from the head (feet + 5.5) is a
        // box of ±0.1; where Kurt is pushed needs a floor within 4 above or below his feet, else half
        // the push to a side along the wall.
        static constexpr float HeadHeight = 5.5f;
        static Vector3 SightBox() { return Vector3(0.1f); }
        static constexpr float PushFloorRange = 4.f;
        static constexpr float PushSide = 0.5f;
        // The camera's push on Kurt, and a solid object's shove, move him at most this fast
        // (u/s), eased in over the next steps: all at once, a door behind him "fast-forwarded" him through
        // it, far more visibly than in the original. float.MaxValue pushes all at once.
        static constexpr float MaxPushSpeed = 40.f;
        // Screen shake: each tick above 1 the view turns by up to ±1.64 × the shake in pixels of
        // the 360-high view (at most ±19 across, ±59 up and down); it drains by 0.25 per tick.
        static constexpr float ShakeScale = 16384.f * 0.0001f;
        static Vector2 ShakeLimit() { return Vector2(19.f, 59.f); }
        static constexpr float ShakeDrain = 0.25f;
        static constexpr float ShakeMin = 1.f;
        static constexpr float ViewHeight = 360.f;

        // The pitch but the mouse's (degrees), the pivot and the roll of the last two steps,
        // for frames between them (Blend).
        struct Aim
        {
            float Pitch = 0;
            float Pivot = 0;
            float Roll = 0;

            bool operator==(const Aim&) const = default;
        };

        Collision::ArenaSpace* _space;
        float _arenaPitch = DefaultPitch;
        float _lookOffset = 0;
        float _airPitch = 0;
        float _airTime = 0;
        float _climb = 0;
        std::optional<float> _lastFeetZ;
        float _climbTime = 0;
        float _shake = 0;
        float _shakeTime = 0;
        Mdk::Random _random;
        std::optional<Aim> _previousAim;
        Aim _aim;

        // The mouse's look (degrees) kept from turning the view past the look limits; the auto
        // pitch (arena, air, climb) alone may go past them.
        static float ClampLook(float look, float autoPitch)
        {
            return Math::Clamp(look, MathF::Min(LookUpLimit - autoPitch, 0.f), MathF::Max(LookDownLimit - autoPitch, 0.f));
        }

        // The view's pitch (radians) for degrees down, short of straight up or down: at 90° the
        // view's up has no way to point, and the float cosine's sign turned it over.
        static float ViewPitch(float degrees);

        // Looks along facing, pitch (radians) down.
        void Orient(Vector3 facing, float pitch);

        // Turns the view by the shake (radians across and up), then rolls it: a positive roll
        // turns its up towards its right.
        void Turn(Vector2 shake, float roll);

        // A new random offset each tick while the shake is above 1; it drains by 0.25 a tick.
        void UpdateShake(float delta);

        float Random(float amplitude);

        // The camera's clearance (camera_clearance 0x417ee8): a wall between Kurt's head and the
        // camera pushes Kurt d · n away from it (d: how far the camera is past it, n: its horizontal
        // normal towards him) if there's floor there (or half of d to a side); then the solid objects in
        // the way push him by what they cut off. The camera moves as far as Kurt did.
        Vector3 Clear(Kurt& kurt, Collision::ArenaSpace& arenas, Vector3 camera, float delta);

        // The push, or a push to a side, with a floor under it (in the air: the push), or null.
        static std::optional<Vector2> Push(Kurt& kurt, Collision::ArenaSpace& arenas, Vector2 push, Vector2 side);

        // The bomber's view (0x4183f0): straight down from height above Kurt, his heading at the top.
        void LookDown(const Kurt& kurt, float height);

        // Air time (0x573a48): it starts once Kurt falls faster than 16 u/s and ends when he
        // rises or stands still on a floor. Running downhill loses the floor for single steps without
        // tilting the view.
        void UpdateAirTime(const Kurt& kurt, float delta);

        // The smoothed rise of the feet per tick (camera_update 0x4174d0), measured once a
        // tick as the original's frames at 30 per second: Kurt's 60 steps a second go down a slope
        // every other step, which would shake the view.
        void UpdateClimb(float feetZ, float delta);

        // The camera's place for the feet and pitch (radians).
        static Vector3 Point(Vector3 feet, Vector3 facing, float pitch, float pivot);
    };

    // View matrices for a camera (MDK coordinates, Z up).
    struct CameraMath
    {
        // A vertical field of view that keeps a 4:3 view's width on a narrower screen (a
        // near-square foldable sees as far left and right, more above and below); wider screens see
        // more at the sides at the same height.
        //   aspect ≥ 4:3: fov (Hor+)    aspect < 4:3: 2 atan(tan(fov / 2) × (4/3) / aspect) (Vert-)
        static float KeepWidth(float fieldOfView, float aspect);

        static Engine::Render::View View(Vector3 position, Vector3 forward, Vector3 up, float fieldOfView, float aspect, float nearZ, float farZ);

        // Sniper mode's view (0x57428c): the focal length is 384 / zoom pixels of the 480-high
        // screen, and the frustum is shifted down so that the scope's centre (y 279) is on the line of
        // sight.
        //   y 0   ┌───────────┐  top    =  279 · near / focal
        //         │     +     │  ◄── line of sight, y 279
        //   y 480 └───────────┘  bottom = -201 · near / focal
        static Engine::Render::View ScopeView(Vector3 eye, Vector3 forward, float zoom, float aspect, float nearZ, float farZ);

        // The scope's off-centre projection for a window of aspect.
        static Matrix4x4 ScopeProjection(float zoom, float aspect, float nearZ, float farZ);

    private:
        // The original's screen: 4:3.
        static constexpr float OriginalAspect = 4.f / 3.f;
    };
}
