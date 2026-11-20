// Dithering of the enhanced look's 8-bit targets: smooth light (falloffs, haze, occlusion) would
// show steps of one level; noise of half a level either way, different per pixel (interleaved
// gradient noise, Jimenez 2014), breaks them up.

#pragma once

#define LEVELS 255.0

float3 dither(float3 c, float2 pixel)
{
    float noise = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    return c + (noise - 0.5) / LEVELS;
}
