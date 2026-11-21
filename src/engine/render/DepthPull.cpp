#include "engine/render/DepthPull.h"

#include "engine/render/Renderer.h"

namespace Mdk::Engine::Render
{
    Matrix4x4 DepthPull::Of(Vector3 eye, Primitive primitive, int layer)
    {
        const auto share = static_cast<float>(layer) * LayerShare + (primitive == Primitive::Lines ? LineShare : 0.0f);
        if (share == 0.0f)
        {
            return Matrix4x4::Identity();
        }

        // Matrix4x4.CreateScale(scale, centerPoint): scaled about the eye.
        const auto scale = 1.0f - share;
        auto result = Matrix4x4::CreateScale(scale);
        result.SetTranslation(eye * (1.0f - scale));
        return result;
    }
}
