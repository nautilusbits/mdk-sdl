#include "engine/render/SunShadow.h"

namespace Mdk::Engine::Render
{
    Matrix4x4 SunShadow::Matrix(Vector3 sunDirection, Vector3 centre, float radius, uint size)
    {
        const auto direction = Vector3::Normalize(sunDirection);
        const auto up = MathF::Abs(direction.Z) > 0.99f ? Vector3::UnitY() : Vector3::UnitZ();
        const auto view = Matrix4x4::CreateLookAt(Vector3::Zero(), direction, up);

        // The centre in light space, on whole texels; the view looks down -z.
        const auto texel = 2.0f * radius / static_cast<float>(size);
        const auto at = Vector3::Transform(centre, view);
        const auto x = MathF::Round(at.X / texel) * texel;
        const auto y = MathF::Round(at.Y / texel) * texel;
        const auto projection = Matrix4x4::CreateOrthographicOffCenter(x - radius, x + radius, y - radius, y + radius,
            -at.Z - Reach, -at.Z + Reach);
        return view * projection;
    }
}
