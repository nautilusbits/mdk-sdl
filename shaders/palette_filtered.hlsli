// The enhanced look's texels: bilinear filtering by hand. Each of the four nearest texels goes
// through the palette first (indices can't be blended), index 0 transparent; animated textures
// keep to their frame (frames stacked vertically). Out of the texture: wrapped, or transparent
// (the canvas's images). The includer declares index_texture and palette_texture.
//
//   cell -- f.x --> cell + (1,0)
//    |
//   f.y      colour = the four, weighted
//    v
//   cell + (0,1)       cell + (1,1)

#pragma once

#define EDGE_WRAP 0
#define EDGE_CLEAR 1

float4 palette_texel(int2 cell, int2 frame_size, int frame, int edge)
{
    if (edge == EDGE_CLEAR && (cell.x < 0 || cell.y < 0 || cell.x >= frame_size.x || cell.y >= frame_size.y))
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    int2 wrapped = int2((cell.x % frame_size.x + frame_size.x) % frame_size.x,
        (cell.y % frame_size.y + frame_size.y) % frame_size.y + frame * frame_size.y);
    int index = int(round(index_texture.Load(int3(wrapped, 0)) * 255.0));
    return index == 0 ? float4(0.0, 0.0, 0.0, 0.0) : float4(palette_texture.Load(int3(index, 0, 0)).rgb, 1.0);
}

// The filtered colour at uv (0-1 over one frame), premultiplied by its coverage (alpha).
float4 palette_filtered(float2 uv, int frame_count, int frame, int edge)
{
    uint width, height;
    index_texture.GetDimensions(width, height);
    int count = max(frame_count, 1);
    int2 frame_size = int2(int(width), int(height) / count);
    int current = count > 1 ? frame : 0;

    float2 position = uv * float2(frame_size) - 0.5;
    int2 cell = int2(floor(position));
    float2 f = frac(position);
    float4 top = lerp(palette_texel(cell, frame_size, current, edge),
        palette_texel(cell + int2(1, 0), frame_size, current, edge), f.x);
    float4 bottom = lerp(palette_texel(cell + int2(0, 1), frame_size, current, edge),
        palette_texel(cell + int2(1, 1), frame_size, current, edge), f.x);
    return lerp(top, bottom, f.y);
}
