#pragma once

// The C# enums' member names, in order, for the settings file and the command line (C#'s
// Enum.Parse and ToString; see Mdk.Core/EnumNames.h).

#include <array>
#include <string_view>

namespace Mdk::Game::Flow::Names
{
    inline constexpr std::array<std::string_view, 31> Key{
        "Forward", "Back", "StrafeLeft", "StrafeRight", "TurnLeft", "TurnRight", "Jump", "Turbo", "Fire",
        "UseItem", "ItemNext", "ItemPrevious", "Item1", "Item2", "Item3", "Item4", "Item5",
        "Up", "Down", "Escape", "Screenshot", "Fly", "Sniper", "ZoomIn", "ZoomOut",
        "RollLeft", "RollRight", "Teleport", "QuickSave", "QuickLoad", "AlwaysRun"};
    inline constexpr std::array<std::string_view, 10> MenuKey{
        "Up", "Down", "Left", "Right", "Accept", "Back", "Backspace", "Delete", "Home", "End"};
    inline constexpr std::array<std::string_view, 9> TouchPart{
        "Fire", "Jump", "Use", "Sniper", "Item", "ZoomIn", "ZoomOut", "Pause", "Stick"};
    inline constexpr std::array<std::string_view, 2> ModState{"On", "Off"};
    inline constexpr std::array<std::string_view, 3> TouchButtons{"Auto", "Off", "On"};
    inline constexpr std::array<std::string_view, 3> Fullscreen{"Off", "Desktop", "Exclusive"};
    inline constexpr std::array<std::string_view, 3> VSync{"On", "Off", "Adaptive"};
    inline constexpr std::array<std::string_view, 4> GpuBackend{"Auto", "Direct3D12", "Vulkan", "Metal"};
    inline constexpr std::array<std::string_view, 3> Difficulty{"Easy", "Normal", "Hard"};
    inline constexpr std::array<std::string_view, 2> Graphics{"Original", "Enhanced"};
    inline constexpr std::array<std::string_view, 3> AntiAliasing{"Off", "X2", "X4"};
    inline constexpr std::array<std::string_view, 9> Stereo{
        "Off", "Sbs", "SbsFull", "CrossView", "Interlaced", "InterlacedReversed", "Tab", "TabReversed", "Vr"};
    inline constexpr std::array<std::string_view, 2> HeadTracking{"Off", "On"};
    inline constexpr std::array<std::string_view, 2> EffectQuality{"Full", "Mobile"};
    inline constexpr std::array<std::string_view, 2> HdModel{"General", "Anime"};
}
