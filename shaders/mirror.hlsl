// Mirrors (MIRRLOW...MIRRHIGH, D3D 0x471908): no reflection; the triangle shows the mirror
// panorama where the sky behind it would be, row_shift rows lower or higher. Rows wrap.

#include "panorama.hlsli"

cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 view_projection;
};

struct VertexIn
{
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

struct VertexOut
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
};

VertexOut vs_main(VertexIn input)
{
    VertexOut output;
    output.position = mul(view_projection, float4(input.position, 1.0));
    output.world = input.position;
    return output;
}

float4 ps_main(VertexOut input) : SV_Target
{
    float2 pixel = panorama_pixel(normalize(input.world - camera_position.xyz));
    int row = int(fmod(fmod(pixel.y + row_shift, panorama_height) + panorama_height, panorama_height));
    int index = panorama_index(int(pixel.x), row);
    return float4(palette_texture.Load(int3(index, 0, 0)).rgb, 1.0);
}
