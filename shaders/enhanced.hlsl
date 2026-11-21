// The enhanced look's surfaces: filtered texels, lit, shadowed and hazy. Surfaces and sprites
// sample their colour texture (RGBA8 premultiplied, mipmapped, a layer per frame: Renderer.Colours.cs)
// trilinear and anisotropic; the canvas filters its index texture (palette_filtered.hlsli).
//
//   mode 1 lit:    albedo x exposure x (hemisphere + sun x N.L x shadow + point lights), tone mapped, then the
//                  haze; edges cut at half cover. The hemisphere: the sky's light from above, the
//                  ground's from below. The tone curve (Tonemap.cs) keeps light up to its knee and
//                  rolls brighter light off towards white.
//   mode 2 sprite: albedo, then the haze; edges cut at half cover; no light
//   mode 3 canvas: the 2D canvas: albedo, its edges blended by their cover
//
// Point lights (muzzle flashes, explosions, fires; PointLights.cs) fade with (1 - d^2 / r^2)^2.
// Light is added in linear colour (the palette is sRGB), on the colour textures: smooth, no palette
// steps; the result is dithered (dither.hlsli) so 8 bits don't band it. Normals are flat: the
// triangle's plane, from the world position's screen derivatives, turned to the camera.
// SDL_GPU register spaces: vertex uniforms space1, fragment resources space2, fragment uniforms space3.

#include "bindings.hlsli"
#include "dither.hlsli"

#define MODE_LIT 1
#define MODE_SPRITE 2
#define MODE_CANVAS 3
// The canvas's images replaced by mods: their colour texture, blended like the canvas.
#define MODE_IMAGE 4
#define GAMMA 2.2
#define HALF_COVER 0.5

cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 transform;
    float4x4 world;
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
    float3 world_position : TEXCOORD1;
};

VertexOut vs_main(VertexIn input)
{
    VertexOut output;
    output.position = mul(transform, float4(input.position, 1.0));
    output.uv = input.uv;
    output.world_position = mul(world, float4(input.position, 1.0)).xyz;
    return output;
}

COMBINED_SAMPLER(0) Texture2D<float> index_texture : register(t0, space2);
COMBINED_SAMPLER(0) SamplerState index_sampler : register(s0, space2);
COMBINED_SAMPLER(1) Texture2D<float4> palette_texture : register(t1, space2);
COMBINED_SAMPLER(1) SamplerState palette_sampler : register(s1, space2);
COMBINED_SAMPLER(2) Texture2D<float> shadow_map : register(t2, space2);
COMBINED_SAMPLER(2) SamplerState shadow_sampler : register(s2, space2);
COMBINED_SAMPLER(3) Texture2DArray<float4> colour_texture : register(t3, space2);
COMBINED_SAMPLER(3) SamplerState colour_sampler : register(s3, space2);

#include "palette_filtered.hlsli"

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 colour;
    int textured;
    int frame_count;
    int frame;
    int mode;
    // World to the shadow map's clip space.
    float4x4 sun_matrix;
    // xyz: the way the sunlight goes; w: the exposure.
    float4 sun;
    // rgb: the sunlight (linear).
    float4 sun_colour;
    // rgb: the light from the sky above and from the ground below (linear); sky.w: the tone
    // curve's knee.
    float4 sky;
    float4 ground;
    // xyz: the camera.
    float4 camera;
    // rgb: the haze's colour (sRGB); a: its density per unit.
    float4 haze;
    // x: the shadow map's size; y: depth bias; z: normal offset (units); w: 1 with shadows.
    float4 shadow;
};

#define MAX_LIGHTS 16

// The frame's point lights, pushed once per frame (Renderer.PushLights).
cbuffer PointLights : register(b1, space3)
{
    // x: how many.
    float4 light_count;
    // Per light: xyz its position, w its radius; then rgb its colour (linear).
    float4 lights[MAX_LIGHTS * 2];
};

float3 to_linear(float3 c)
{
    return pow(max(c, 0.0), GAMMA);
}

float3 to_srgb(float3 c)
{
    return pow(max(c, 0.0), 1.0 / GAMMA);
}

// The tone curve per channel: kept up to the knee, then 1 - (1 - knee) e^(-(x - knee) / (1 - knee)).
float3 tonemap(float3 c, float knee)
{
    float room = 1.0 - knee;
    float3 shoulder = 1.0 - room * exp(-(c - knee) / room);
    return lerp(c, shoulder, step(knee, c));
}

// The point lights' light on a face.
float3 point_lights(float3 position, float3 normal)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    int count = int(light_count.x);
    [loop] for (int i = 0; i < count; i++)
    {
        float4 at = lights[i * 2];
        float3 to = at.xyz - position;
        float d2 = dot(to, to);
        float r2 = at.w * at.w;
        if (d2 >= r2)
        {
            continue;
        }

        float fade = 1.0 - d2 / r2;
        float facing = saturate(dot(normal, to * rsqrt(max(d2, 1e-4))));
        sum += lights[i * 2 + 1].rgb * facing * fade * fade;
    }

    return sum;
}

// How much sunlight reaches a point: 2 x 2 shadow map texels compared, then blended; fades out
// towards the map's edge.
float sunlight(float3 position, float3 normal)
{
    if (shadow.w == 0.0)
    {
        return 1.0;
    }

    float4 clip = mul(sun_matrix, float4(position + normal * shadow.z, 1.0));
    float2 ndc = clip.xy / clip.w;
    float edge = max(abs(ndc.x), abs(ndc.y));
    if (edge >= 1.0 || clip.z >= 1.0)
    {
        return 1.0;
    }

    float2 texel = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * shadow.x - 0.5;
    int2 cell = int2(floor(texel));
    float2 f = frac(texel);
    int last = int(shadow.x) - 1;
    float depth = clip.z - shadow.y;
    float lit[4];
    [unroll] for (int i = 0; i < 4; i++)
    {
        int2 at = clamp(cell + int2(i % 2, i / 2), 0, last);
        lit[i] = depth <= shadow_map.Load(int3(at, 0)) ? 1.0 : 0.0;
    }

    float light = lerp(lerp(lit[0], lit[1], f.x), lerp(lit[2], lit[3], f.x), f.y);
    const float fade_width = 0.1;
    return lerp(light, 1.0, saturate((edge - (1.0 - fade_width)) / fade_width));
}

float4 ps_main(VertexOut input) : SV_Target
{
    // Derivatives before any discard.
    float3 position = input.world_position;
    float3 normal = normalize(cross(ddx(position), ddy(position)));
    if (dot(normal, camera.xyz - position) < 0.0)
    {
        normal = -normal;
    }

    // Sampled before any discard (mips need the derivatives); the layer is the frame.
    float4 texel = float4(1.0, 1.0, 1.0, 1.0);
    float4 colours = colour_texture.Sample(colour_sampler, float3(input.uv, frame_count > 1 ? frame : 0));
    if (textured != 0)
    {
        texel = mode == MODE_CANVAS ? palette_filtered(input.uv, frame_count, frame, EDGE_CLEAR) : colours;
    }

    // The canvas blends its edges; the rest cut them where half covered (premultiplied -> straight).
    if (mode == MODE_CANVAS || mode == MODE_IMAGE)
    {
        return float4(texel.a > 0.0 ? texel.rgb / texel.a : texel.rgb, texel.a) * colour;
    }

    if (texel.a < HALF_COVER)
    {
        discard;
    }

    float3 albedo = to_linear(texel.rgb / texel.a * colour.rgb);
    float3 lit = albedo;
    if (mode == MODE_LIT)
    {
        float facing = max(dot(normal, -sun.xyz), 0.0);
        float3 hemisphere = lerp(ground.rgb, sky.rgb, normal.z * 0.5 + 0.5);
        float3 light = hemisphere + sun_colour.rgb * facing * sunlight(position, normal) + point_lights(position, normal);
        lit = tonemap(albedo * sun.w * light, sky.w);
    }

    float fog = 1.0 - exp(-haze.a * length(position - camera.xyz));
    return float4(dither(to_srgb(lerp(lit, to_linear(haze.rgb), fog)), input.position.xy), colour.a);
}
