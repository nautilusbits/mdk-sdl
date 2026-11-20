#include "Mdk.Game/FreeCamera.h"

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/Angles.h"
#include "Mdk.Game/Kurt/FollowCamera.h"

namespace Mdk::Game
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;

    Vector3 FreeCamera::Forward() const
    {
        auto yaw = DegreesToRadians(Yaw);
        auto pitch = DegreesToRadians(Pitch);
        return Vector3(MathF::Cos(yaw) * MathF::Cos(pitch), MathF::Sin(yaw) * MathF::Cos(pitch), MathF::Sin(pitch));
    }

    Vector3 FreeCamera::Right() const
    {
        return Vector3::Normalize(Vector3::Cross(Forward(), Vector3::UnitZ()));
    }

    void FreeCamera::Update(Input& input, float delta)
    {
        // The flying camera never snipes: the scope's bindings don't apply.
        input.Scoped = false;
        Yaw -= input.MouseX() * MouseDegrees;
        Pitch = Math::Clamp(Pitch - input.MouseY() * MouseDegrees, -PitchLimit, PitchLimit);

        auto forward = Forward();
        auto right = Right();
        auto move = Vector3::Zero();
        move += Axis(input, Key::Forward, Key::Back) * forward;
        move += Axis(input, Key::StrafeRight, Key::StrafeLeft) * right;
        move += Axis(input, Key::Up, Key::Down) * Vector3::UnitZ();
        auto speed = input.IsDown(Key::Turbo) ? Speed * TurboFactor : Speed;
        Position += move * speed * delta;
    }

    float FreeCamera::Axis(Input& input, Key positive, Key negative)
    {
        return (input.IsDown(positive) ? 1.f : 0.f) - (input.IsDown(negative) ? 1.f : 0.f);
    }

    Engine::Render::View FreeCamera::View(float aspect) const
    {
        return Kurt::CameraMath::View(Position, Forward(), Vector3::UnitZ(), Kurt::CameraMath::KeepWidth(FieldOfView, aspect), aspect, Near, Far);
    }
}
