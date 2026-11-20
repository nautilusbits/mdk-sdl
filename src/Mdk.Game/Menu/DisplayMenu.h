#pragma once

#include <vector>

#include "Mdk.Game/HdTextures/HdMenu.h"

namespace Mdk::Game::Menu
{
    // The Display page's items.
    enum class DisplayItem { Mode, Resolution, Scale, VSync, FrameLimit, Backend, Gamma };

    // Which Display items a platform shows: a phone is always fullscreen on Vulkan, so it has
    // no display mode, resolution or backend.
    struct DisplayMenu
    {
        static std::vector<DisplayItem> Items(HdTextures::Host host)
        {
            if (host == HdTextures::Host::Android)
            {
                return {DisplayItem::Scale, DisplayItem::VSync, DisplayItem::FrameLimit, DisplayItem::Gamma};
            }

            return {DisplayItem::Mode, DisplayItem::Resolution, DisplayItem::Scale, DisplayItem::VSync,
                DisplayItem::FrameLimit, DisplayItem::Backend, DisplayItem::Gamma};
        }
    };
}
