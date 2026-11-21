#pragma once

#include <string>
#include <vector>

namespace Mdk::Engine::Platform
{
    class Input;
}

namespace Mdk::Game::Flow
{
    // Keys pressed once at given game times (--press=QuickSave@1,Menu.Accept@1.5; tests): game
    // keys (Key) by name, menu keys (MenuKey) by name after "Menu.".
    class TestPresses
    {
    public:
        static TestPresses Parse(const std::string& text);

        // Presses the keys due at `time`, each once.
        void Apply(Engine::Platform::Input& input, float time);

    private:
        static constexpr char Separator = ',';
        static constexpr char At = '@';
        static constexpr const char* MenuPrefix = "Menu.";

        struct Press
        {
            std::string Name;
            float Time = 0;

            bool operator==(const Press&) const = default;
        };

        std::vector<Press> _pending;

        explicit TestPresses(std::vector<Press> pending) : _pending(std::move(pending)) {}
    };
}
