#pragma once

// Files built into the executable (C#'s embedded resources): compiled shaders, the icon.

#include <cstdint>
#include <span>
#include <string_view>

namespace Mdk::Embedded
{
    // Empty span when the name isn't embedded.
    std::span<const uint8_t> Find(std::string_view name);
}
