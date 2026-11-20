// The enhanced look's post-processing, over the lit scene: the ambient occlusion (occlusion.hlsl)
// blurred over 4 x 4 pixels on the same surface, then a little glow (the scene's blurred mips,
// screen-blended), dithered (dither.hlsli).
//
//   scene (mips) --+------------- colour x occlusion --+- screen -> frame
//   occlusion -----' 4 x 4 blur, same plane     glow --'

#include "screen.hlsli"
#include "dither.hlsli"

// How far off a pixel's plane a neighbour may be, per unit of distance, and still be blurred in.
#define SAME_SURFACE 0.02

COMBINED_SAMPLER(2) Texture2D<float4> occlusion_texture : register(t2, space2);
COMBINED_SAMPLER(2) SamplerState occlusion_sampler : register(s2, space2);

// The occlusion averaged over the 4 x 4 of its texels around (one of each noise turn), leaving out
// those off p's plane (another surface: no shade across edges). A texel is occlusion.w pixels a
// side: 1, or 2 (EffectQuality.Mobile: drawn at half the frame's sides).
float blurred_occlusion(float2 uv, float3 p, float2 pixel)
{
    float3 n = normal_at(uv, p);
    float reach = SAME_SURFACE * length(p);
    int step = max(int(occlusion.w), 1);
    int2 at = int2(pixel) / step;
    int2 last = (int2(screen.xy) + step - 1) / step - 1;
    float sum = 0.0;
    float count = 0.0;
    [unroll] for (int y = -NOISE_TILE / 2; y < NOISE_TILE / 2; y++)
    {
        [unroll] for (int x = -NOISE_TILE / 2; x < NOISE_TILE / 2; x++)
        {
            int2 q = clamp(at + int2(x, y), int2(0, 0), last);
            float2 quv = (float2(q) + 0.5) * screen.zw * step;
            if (depth_at(quv) >= FAR || abs(dot(point_at(quv) - p, n)) > reach)
            {
                continue;
            }

            sum += occlusion_texture.Load(int3(q, 0)).r;
            count += 1.0;
        }
    }

    return count > 0.0 ? sum / count : occlusion_texture.Load(int3(at, 0)).r;
}

float4 ps_main(VertexOut input) : SV_Target
{
    float2 uv = input.uv;
    float3 colour = to_linear(scene_texture.SampleLevel(scene_sampler, uv, 0.0).rgb);

    // The sky (nothing in the depth buffer) has no occlusion.
    if (depth_at(uv) < FAR)
    {
        colour *= blurred_occlusion(uv, point_at(uv), input.position.xy);
    }

    float3 blurred = to_linear((scene_texture.SampleLevel(scene_sampler, uv, glow.w).rgb
        + scene_texture.SampleLevel(scene_sampler, uv, glow.w + 2.0).rgb) * 0.5);
    float3 shine = saturate((blurred * glow.x + max(blurred - glow.y, 0.0)) * glow.z);
    colour = 1.0 - (1.0 - colour) * (1.0 - shine);
    return float4(dither(to_srgb(colour), input.position.xy), 1.0);
}
