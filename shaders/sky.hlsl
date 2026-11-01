// The sky: the original blits a panorama as a 2D backdrop. It scrolls horizontally with the yaw
// (wrap_width pixels for 360 degrees) and vertically with the pitch, horizon_row at eye level; above
// and below it the screen is filled with top and bottom colours. Drawn as one screen-filling
// triangle before the level, without depth. The enhanced look filters it.

#include "panorama.hlsli"

COMBINED_SAMPLER(2) Texture2D<float4> hd_sky_texture : register(t2, space2);
COMBINED_SAMPLER(2) SamplerState hd_sky_sampler : register(s2, space2);

cbuffer VertexUniforms : register(b0, space1)
{
    // Clip space to a world direction (the camera's rotation and projection, inverted).
    float4x4 clip_to_direction;
};

struct VertexOut
{
    float4 position : SV_Position;
    float3 direction : TEXCOORD0;
};

VertexOut vs_main(uint id : SV_VertexID)
{
    // (-1,-1), (3,-1), (-1,3): covers the screen.
    float2 clip = float2(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0);
    VertexOut output;
    output.position = float4(clip, 0.0, 1.0);
    float4 direction = mul(clip_to_direction, float4(clip, 1.0, 1.0));
    output.direction = direction.xyz / direction.w;
    return output;
}

// A pixel's colour; above and below the panorama, the top and bottom colours.
float3 sky_pixel(int column, int row, float scale)
{
    if (row < 0)
    {
        return palette_texture.Load(int3(top_colour, 0, 0)).rgb;
    }
    if (row >= panorama_height * scale)
    {
        return palette_texture.Load(int3(bottom_colour, 0, 0)).rgb;
    }

    if ((sampling & SAMPLING_HD) != 0)
    {
        uint width, height;
        hd_sky_texture.GetDimensions(width, height);
        int wrap = int(wrap_width * scale + 0.5);
        int wrapped = (column % wrap + wrap) % wrap;
        float2 uv = (float2(wrapped, row) + 0.5) / float2(width, height);
        return hd_sky_texture.SampleLevel(hd_sky_sampler, uv, 0.0).rgb;
    }

    return palette_texture.Load(int3(panorama_index(column, row), 0, 0)).rgb;
}

float4 ps_main(VertexOut input) : SV_Target
{
    float3 direction = normalize(input.direction);
    float scale = 1.0;
    if ((sampling & SAMPLING_HD) != 0)
    {
        uint width, height;
        hd_sky_texture.GetDimensions(width, height);
        scale = height / panorama_height;
    }

    // The enhanced look blends the 4 nearest pixels, each through the palette.
    if ((sampling & SAMPLING_LINEAR) != 0)
    {
        float2 position = panorama_position(direction, scale) - 0.5;
        int2 cell = int2(floor(position));
        float2 f = frac(position);
        float3 top = lerp(sky_pixel(cell.x, cell.y, scale), sky_pixel(cell.x + 1, cell.y, scale), f.x);
        float3 bottom = lerp(sky_pixel(cell.x, cell.y + 1, scale), sky_pixel(cell.x + 1, cell.y + 1, scale), f.x);
        return float4(lerp(top, bottom, f.y), 1.0);
    }

    float2 pixel = panorama_position(direction, scale);
    return float4(sky_pixel(int(pixel.x), int(floor(pixel.y)), scale), 1.0);
}
