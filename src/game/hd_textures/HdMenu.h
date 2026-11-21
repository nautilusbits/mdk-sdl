#pragma once

#include <vector>

namespace Mdk::Game::HdTextures
{
    // The platform the game runs on, for what it can offer.
    enum class Host { Desktop, Android };

    // The options page's HD texture items: the job making them (their switch is the Mods page's).
    enum class HdItem { Make };

    // Which HD texture items the options page shows. Real-ESRGAN's ncnn-Vulkan builds are
    // desktop-only, so Android never makes them; it uses the mod made on a PC (imported with mods/).
    struct HdMenu
    {
        static Host Current()
        {
#if defined(__ANDROID__)
            return Host::Android;
#else
            return Host::Desktop;
#endif
        }

        static bool CanMake(Host host) { return host == Host::Desktop; }

        static std::vector<HdItem> Items(Host host)
        {
            if (CanMake(host))
            {
                return {HdItem::Make};
            }

            return {};
        }
    };
}
