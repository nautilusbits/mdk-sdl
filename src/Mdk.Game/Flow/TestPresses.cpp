#include "Mdk.Game/Flow/TestPresses.h"

#include <algorithm>
#include <cstring>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/EnumNames.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Game/Flow/EnumNames.h"

namespace Mdk::Game::Flow
{
    TestPresses TestPresses::Parse(const std::string& text)
    {
        std::vector<Press> pending;
        for (const auto& item : Str::Split(text, Separator, Str::SplitOptions::RemoveEmpty))
        {
            auto p = Str::SplitCount(item, At, 2);
            pending.push_back(Press{p[0], p.size() > 1 ? Str::ParseFloat(p[1]) : 0.f});
        }

        return TestPresses(std::move(pending));
    }

    void TestPresses::Apply(Engine::Platform::Input& input, float time)
    {
        std::vector<Press> due;
        std::copy_if(_pending.begin(), _pending.end(), std::back_inserter(due), [time](const Press& p) { return p.Time <= time; });
        for (const auto& press : due)
        {
            _pending.erase(std::find(_pending.begin(), _pending.end(), press));
            if (Str::StartsWith(press.Name, MenuPrefix))
            {
                Engine::Platform::MenuKey menuKey{};
                if (EnumNames::TryParse(std::string_view(press.Name).substr(std::strlen(MenuPrefix)), Names::MenuKey, menuKey))
                {
                    input.Press(menuKey);
                }

                continue;
            }

            Engine::Platform::Key key{};
            if (EnumNames::TryParse(press.Name, Names::Key, key))
            {
                input.Press(key);
            }
        }
    }
}
