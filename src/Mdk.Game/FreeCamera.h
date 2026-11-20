#pragma once

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Engine/Render/Renderer.h"

namespace Mdk::Game
{
    // A flying camera for looking around a level (MDK coordinates, Z up; yaw 90 faces +Y).
    class FreeCamera
    {
    public:
        static constexpr float FieldOfView = 60.f;

        Vector3 Position;
        // Degrees.
        float Yaw = 0;
        float Pitch = 0;

        Vector3 Forward() const;

        Vector3 Right() const;

        void Update(Engine::Platform::Input& input, float delta);

        Engine::Render::View View(float aspect) const;

    private:
        static constexpr float Speed = 60.f;
        static constexpr float TurboFactor = 4.f;
        static constexpr float MouseDegrees = 0.15f;
        static constexpr float PitchLimit = 89.f;
        static constexpr float Near = 0.5f;
        static constexpr float Far = 20000.f;

        static float Axis(Engine::Platform::Input& input, Engine::Platform::Key positive, Engine::Platform::Key negative);
    };
}
