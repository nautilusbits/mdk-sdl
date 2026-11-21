#include "game/SoakKeys.h"

#include <algorithm>

namespace Mdk::Game
{
    using Engine::Platform::Input;

    const std::vector<SoakKeys::Key>& SoakKeys::All()
    {
        static const std::vector<Key> all = []
        {
            std::vector<Key> keys;
            for (auto key : Moves)
            {
                if (std::find(keys.begin(), keys.end(), key) == keys.end())
                {
                    keys.push_back(key);
                }
            }

            keys.insert(keys.end(), Turns.begin(), Turns.end());
            keys.insert(keys.end(), Presses.begin(), Presses.end());
            keys.push_back(Key::Turbo);
            keys.push_back(Key::Fire);
            keys.push_back(Key::Jump);
            return keys;
        }();
        return all;
    }

    void SoakKeys::Hold(Input& input, float time)
    {
        if (time >= _end)
        {
            Roll(time);
        }

        for (auto key : All())
        {
            auto down = _held.contains(key) || (key == _press && time < _start + PressTime);
            input.Hold(key, down ? Input::State::Down : Input::State::Up);
        }
    }

    void SoakKeys::PressMenu(Input& input, float time)
    {
        if (time < _nextMenu)
        {
            return;
        }

        _nextMenu = time + MenuPeriod;
        input.Press(MenuKeys[_random.Next(static_cast<int>(MenuKeys.size()))]);
    }

    void SoakKeys::Roll(float time)
    {
        _held.clear();
        _start = time;
        _end = time + MinAction + static_cast<float>(_random.NextDouble()) * (MaxAction - MinAction);

        // An index past the array: no move, no turn.
        auto move = _random.Next(static_cast<int>(Moves.size()) + 1);
        if (move < static_cast<int>(Moves.size()))
        {
            _held.insert(Moves[move]);
        }

        auto turn = _random.Next(static_cast<int>(Turns.size()) + 1);
        if (turn < static_cast<int>(Turns.size()))
        {
            _held.insert(Turns[turn]);
        }

        Maybe(Key::Turbo, TurboChance);
        Maybe(Key::Fire, FireChance);
        Maybe(Key::Jump, JumpChance);
        _press = _random.NextDouble() < PressChance ? std::optional<Key>(Presses[_random.Next(static_cast<int>(Presses.size()))]) : std::nullopt;
    }

    void SoakKeys::Maybe(Key key, double chance)
    {
        if (_random.NextDouble() < chance)
        {
            _held.insert(key);
        }
    }
}
