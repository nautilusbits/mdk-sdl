// The level's panorama, shared by the sky and the mirrors. MDK coordinates: Z up, the azimuth
// counted from +Y towards +X.

#include "bindings.hlsli"

#define TAU 6.28318530718

COMBINED_SAMPLER(0) Texture2D<float> panorama_texture : register(t0, space2);
COMBINED_SAMPLER(0) SamplerState panorama_sampler : register(s0, space2);
COMBINED_SAMPLER(1) Texture2D<float4> palette_texture : register(t1, space2);
COMBINED_SAMPLER(1) SamplerState palette_sampler : register(s1, space2);

cbuffer PanoramaUniforms : register(b0, space3)
{
    float wrap_width;
    float horizon_row;
    float panorama_offset;
    float panorama_height;
    int top_colour;
    int bottom_colour;
    // Mirrors: rows lower (MIRRLOW) or higher (MIRRHIGH) than the sky behind them.
    float row_shift;
    // The sky's sampling: nearest pixels (0) or filtered (1, the enhanced look).
    int sampling;
    float4 camera_position;
};

#define SAMPLING_LINEAR 1

// Where in the panorama a direction looks (columns and rows, not rounded).
float2 panorama_position(float3 direction)
{
    float pixels_per_radian = wrap_width / TAU;
    float azimuth = atan2(direction.x, direction.y);
    float elevation = asin(clamp(direction.z, -1.0, 1.0));
    float column = fmod(fmod(azimuth * pixels_per_radian + panorama_offset, wrap_width) + wrap_width, wrap_width);
    return float2(column, horizon_row - elevation * pixels_per_radian);
}

// Column and row of the panorama seen in a direction.
float2 panorama_pixel(float3 direction)
{
    float2 position = panorama_position(direction);
    return float2(position.x, floor(position.y));
}

int panorama_index(int column, int row)
{
    int wrapped = (column % int(wrap_width) + int(wrap_width)) % int(wrap_width);
    return int(round(panorama_texture.Load(int3(wrapped, row, 0)) * 255.0));
}
