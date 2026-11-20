#pragma once

// Bit operators for a [Flags] enum class: MDK_FLAGS(Keys) after its declaration.
//   Keys k = Keys::A | Keys::B;   if (HasFlag(k, Keys::A)) { ... }

#include <type_traits>

#define MDK_FLAGS(E)                                                                                      \
    constexpr E operator|(E a, E b) { return static_cast<E>(static_cast<std::underlying_type_t<E>>(a) |   \
                                                            static_cast<std::underlying_type_t<E>>(b)); } \
    constexpr E operator&(E a, E b) { return static_cast<E>(static_cast<std::underlying_type_t<E>>(a) &   \
                                                            static_cast<std::underlying_type_t<E>>(b)); } \
    constexpr E operator^(E a, E b) { return static_cast<E>(static_cast<std::underlying_type_t<E>>(a) ^   \
                                                            static_cast<std::underlying_type_t<E>>(b)); } \
    constexpr E operator~(E a) { return static_cast<E>(~static_cast<std::underlying_type_t<E>>(a)); }     \
    constexpr E& operator|=(E& a, E b) { return a = a | b; }                                              \
    constexpr E& operator&=(E& a, E b) { return a = a & b; }                                              \
    constexpr E& operator^=(E& a, E b) { return a = a ^ b; }                                              \
    constexpr bool HasFlag(E value, E flag) { return (value & flag) == flag; }                            \
    constexpr bool Any(E value) { return static_cast<std::underlying_type_t<E>>(value) != 0; }
