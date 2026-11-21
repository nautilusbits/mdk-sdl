// The display's gamma (Renderer.Gamma.cs): the finished frame brightened or darkened before it's
// shown, the blacks and whites kept: colour^(1 / gamma).
//
//   frame ─► "gamma" ─► graded frame ─► swapchain, or the screenshot

#include "bindings.hlsli"

struct VertexOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOut vs_main(uint id : SV_VertexID)
{
    // (-1,-1), (3,-1), (-1,3): covers the screen.
    float2 clip = float2(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0);
    VertexOut output;
    output.position = float4(clip, 0.0, 1.0);
    output.uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    return output;
}

COMBINED_SAMPLER(0) Texture2D<float4> frame_texture : register(t0, space2);
COMBINED_SAMPLER(0) SamplerState frame_sampler : register(s0, space2);

cbuffer GammaUniforms : register(b0, space3)
{
    // x: 1 / gamma.
    float4 gamma;
};

float4 ps_main(VertexOut input) : SV_Target
{
    float3 colour = frame_texture.SampleLevel(frame_sampler, input.uv, 0.0).rgb;
    return float4(pow(colour, gamma.x), 1.0);
}
