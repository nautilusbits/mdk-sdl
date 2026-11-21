// The enhanced look's ambient occlusion (screen.hlsli), fading in the haze, into its own target:
// noisy (12 samples a pixel), the post pass blurs it.

#include "screen.hlsli"

float4 ps_main(VertexOut input) : SV_Target
{
    // The sky (nothing in the depth buffer) has no occlusion.
    float2 uv = input.uv;

    // At half the frame's sides (occlusion.w 2) a texel's centre is between four pixels: the one
    // below right's centre instead, as the depth buffer holds it.
    if (occlusion.w > 1.0)
    {
        uv = (floor(uv * screen.xy) + 0.5) * screen.zw;
    }
    if (depth_at(uv) >= FAR)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    float3 p = point_at(uv);
    float clear = exp(-occlusion.z * length(p));
    float shade = lerp(1.0, ambient_occlusion(uv, p, input.position.xy), clear);
    return float4(shade, shade, shade, 1.0);
}
