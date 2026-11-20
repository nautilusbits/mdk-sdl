// SDL_GPU's resource bindings beyond HLSL registers. SPIR-V (Vulkan): a texture and its sampler
// are one combined sampler, fragment resources in set 2 at the register's index:
//   COMBINED_SAMPLER(1) Texture2D t : register(t1, space2);  COMBINED_SAMPLER(1) SamplerState s : register(s1, space2);
// DXIL and MSL (from the SPIR-V) need nothing more.

#pragma once

#ifdef __spirv__
#define COMBINED_SAMPLER(index) [[vk::combinedImageSampler]][[vk::binding(index, 2)]]
#else
#define COMBINED_SAMPLER(index)
#endif
