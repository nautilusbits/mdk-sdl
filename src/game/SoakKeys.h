#pragma once

#include <array>
#include <limits>
#include <optional>
#include <unordered_set>
#include <vector>

#include "core/DotNet.h"
#include "engine/platform/Input.h"

namespace Mdk::Game
{
    // The soak test's player (--soak=seed): random but seeded keys, a new action every
    // 0.2-1.5 s: a move, a turn, turbo, fire, jump, and sometimes a press (use, select, sniper); in
    // the menus a menu key every 0.3 s. The same seed and game times give the same keys.
    //   time ──► action over? ──► roll the next one ──► hold its keys (a press: its first 0.1 s)
    class SoakKeys
    {
    public:
        explicit SoakKeys(int seed) : _random(seed) {}

        // Holds the keys of the action at game time `time`.
        void Hold(Engine::Platform::Input& input, float time);

        // A random menu key every 0.3 s of game time `time`.
        void PressMenu(Engine::Platform::Input& input, float time);

    private:
        using Key = Engine::Platform::Key;
        using MenuKey = Engine::Platform::MenuKey;

        static constexpr float MinAction = 0.2f;
        static constexpr float MaxAction = 1.5f;
        static constexpr float PressTime = 0.1f;
        static constexpr double TurboChance = 0.3;
        static constexpr double FireChance = 0.4;
        static constexpr double JumpChance = 0.1;
        static constexpr double PressChance = 0.25;
        static constexpr std::array<Key, 5> Moves{Key::Forward, Key::Forward, Key::Back, Key::StrafeLeft, Key::StrafeRight};
        static constexpr std::array<Key, 2> Turns{Key::TurnLeft, Key::TurnRight};
        static constexpr std::array<Key, 6> Presses{Key::UseItem, Key::ItemNext, Key::ItemPrevious, Key::Sniper, Key::ZoomIn, Key::ZoomOut};
        // Moves without repeats, the turns, the presses, then turbo, fire and jump.
        static const std::vector<Key>& All();
        // The menus' keys, "accept" and "down" more often (to get on through the menus).
        static constexpr std::array<MenuKey, 8> MenuKeys{MenuKey::Up, MenuKey::Down, MenuKey::Down, MenuKey::Left,
            MenuKey::Right, MenuKey::Accept, MenuKey::Accept, MenuKey::Back};
        static constexpr float MenuPeriod = 0.3f;

        Random _random;
        std::unordered_set<Key> _held;
        std::optional<Key> _press;
        float _start = -std::numeric_limits<float>::infinity();
        float _end = -std::numeric_limits<float>::infinity();
        float _nextMenu = 0;

        void Roll(float time);

        void Maybe(Key key, double chance);
    };
}
