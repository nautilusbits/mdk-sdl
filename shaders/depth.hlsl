// The enhanced look's depth passes: the sun's (shadow map) and the camera's (ambient occlusion).
// Depth only; palette index 0 is a hole, as in palette.hlsl.

#include "bindings.hlsli"

cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 transform;
};

struct VertexIn
{
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

struct VertexOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOut vs_main(VertexIn input)
{
    VertexOut output;
    output.position = mul(transform, float4(input.position, 1.0));
    output.uv = input.uv;
    return output;
}

COMBINED_SAMPLER(0) Texture2D<float> index_texture : register(t0, space2);
COMBINED_SAMPLER(0) SamplerState index_sampler : register(s0, space2);

// palette.hlsl's uniforms (the colour unused).
cbuffer FragmentUniforms : register(b0, space3)
{
    float4 colour;
    int textured;
    int frame_count;
    int frame;
    int unused;
};

void ps_main(VertexOut input)
{
    if (textured == 0)
    {
        return;
    }

    float2 uv = input.uv;
    if (frame_count > 1)
    {
        uv.y = (frac(uv.y) + frame) / frame_count;
    }

    if (int(round(index_texture.Sample(index_sampler, uv) * 255.0)) == 0)
    {
        discard;
    }
}
