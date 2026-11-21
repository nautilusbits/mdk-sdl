#include "engine/platform/Display.h"

#include <algorithm>
#include <format>

#include <SDL3/SDL.h>

#include "core/DotNet.h"

namespace Mdk::Engine::Platform
{
    Os OsInfo::Current()
    {
#if defined(__ANDROID__)
        return Os::Android;
#elif defined(_WIN32)
        return Os::Windows;
#elif defined(__APPLE__)
        return Os::MacOs;
#else
        return Os::Linux;
#endif
    }

    std::string Resolution::ToString() const { return std::format("{}{}{}", Width, Separator, Height); }

    namespace
    {
        // int.TryParse with NumberStyles.None: digits only.
        bool TryParseDigits(const std::string& text, int& value)
        {
            if (text.empty())
            {
                return false;
            }

            long long result = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }

                result = result * 10 + (c - '0');
                if (result > INT32_MAX)
                {
                    return false;
                }
            }

            value = static_cast<int>(result);
            return true;
        }
    }

    bool Resolution::TryParse(const std::string& text, Resolution& size)
    {
        size = {};
        auto parts = Str::Split(text, Separator);
        int width = 0;
        int height = 0;
        if (parts.size() != 2
            || !TryParseDigits(parts[0], width)
            || !TryParseDigits(parts[1], height)
            || width <= 0 || height <= 0)
        {
            return false;
        }

        size = {width, height};
        return true;
    }

    std::vector<Resolution> Resolutions::Windowed(Resolution usable, Resolution current)
    {
        std::vector<Resolution> list;
        auto add = [&list](Resolution r)
        {
            if (std::find(list.begin(), list.end(), r) == list.end())
            {
                list.push_back(r);
            }
        };

        for (auto p : Presets)
        {
            if (p.Width <= usable.Width && p.Height <= usable.Height)
            {
                add(p);
            }
        }

        add(current);
        std::stable_sort(list.begin(), list.end(), [](Resolution a, Resolution b)
        {
            if (a.Area() != b.Area())
            {
                return a.Area() < b.Area();
            }

            return a.Width < b.Width;
        });
        return list;
    }

    std::vector<ScreenMode> Resolutions::Exclusive(std::span<const ScreenMode> modes)
    {
        // GroupBy keeps the groups in first-seen order; MaxBy keeps the first highest.
        std::vector<ScreenMode> list;
        for (const auto& m : modes)
        {
            auto same = std::find_if(list.begin(), list.end(), [&m](const ScreenMode& g) { return g.Size() == m.Size(); });
            if (same == list.end())
            {
                list.push_back(m);
                continue;
            }

            if (m.Refresh > same->Refresh)
            {
                *same = m;
            }
        }

        std::stable_sort(list.begin(), list.end(), [](const ScreenMode& a, const ScreenMode& b)
        {
            if (a.Size().Area() != b.Size().Area())
            {
                return a.Size().Area() < b.Size().Area();
            }

            return a.Width < b.Width;
        });
        return list;
    }

    Resolution Resolutions::Step(std::span<const Resolution> list, Resolution current, int step)
    {
        if (list.empty())
        {
            return current;
        }

        auto found = std::find(list.begin(), list.end(), current);
        if (found == list.end())
        {
            return list[0];
        }

        int index = static_cast<int>(found - list.begin());
        int count = static_cast<int>(list.size());
        return list[((index + step) % count + count) % count];
    }

    TimeSpan FrameLimiter::Remaining(TimeSpan elapsed, int limit)
    {
        if (limit <= Off)
        {
            return TimeSpan::zero();
        }

        TimeSpan frame(TicksPerSecond / limit);
        return elapsed >= frame ? TimeSpan::zero() : frame - elapsed;
    }

    void FrameLimiter::Wait(TimeSpan elapsed, int limit)
    {
        constexpr long long NanosecondsPerTick = 100;
        TimeSpan rest = Remaining(elapsed, limit);
        if (rest > TimeSpan::zero())
        {
            SDL_DelayPrecise(static_cast<Uint64>(rest.count() * NanosecondsPerTick));
        }
    }
}
