#include "Mdk.Game/RideTest.h"

#include <algorithm>
#include <format>
#include <vector>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/DotNet.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Scripts/Bomber.h"
#include "Mdk.Game/Scripts/Rides.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Mdk.Game/Viewer.h"

namespace Mdk::Game
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Scripts::ScriptRuntime;

    TeleportTarget TeleportTarget::Parse(const std::string& text)
    {
        auto parts = Str::Split(text, ',');
        std::vector<float> v;
        for (size_t i = 1; i < parts.size(); i++)
        {
            v.push_back(Str::ParseFloat(parts[i]));
        }

        return TeleportTarget{ parts[0], Vector3(v[0], v[1], v[2]) };
    }

    void RideTest::Step(Kurt::Kurt& kurt, ScriptRuntime& scripts, Input& input, float time)
    {
        if (time < _options.Delay)
        {
            return;
        }

        if (!_started)
        {
            _started = true;
            Start(scripts);
        }

        if (_options.Bomber.has_value())
        {
            StepBomber(kurt, scripts, input, time, *_options.Bomber);
        }
    }

    void RideTest::Report(ScriptRuntime& scripts)
    {
        if (auto ridden = scripts.Rides->Ridden)
        {
            Console::WriteLine(std::format("riding {}", ridden->TypeName));
        }

        if (auto bomber = scripts.Rides->Bomber)
        {
            Console::WriteLine(std::format("bomber view {:.1f}, locked {}, bombs {}", bomber->ViewHeight, bomber->Locked() ? "true" : "false",
                bomber->BombsLeft));
        }
    }

    void RideTest::Start(ScriptRuntime& scripts)
    {
        if (_options.Teleport.has_value())
        {
            scripts.TeleportKurt(_options.Teleport->Arena, _options.Teleport->Position, TeleportYaw);
        }

        if (_options.Kill.has_value())
        {
            if (auto killed = scripts.FindObjectNamed(*_options.Kill))
            {
                scripts.Kill(*killed);
            }
        }

        if (_options.Ride.has_value())
        {
            if (auto walker = scripts.FindObjectNamed(*_options.Ride))
            {
                scripts.Rides->RideWalker(*walker);
            }
        }
    }

    void RideTest::StepBomber(Kurt::Kurt& kurt, ScriptRuntime& scripts, Input& input, float time, BomberTest test)
    {
        if (!_called && time >= _options.Delay + BomberCallDelay)
        {
            _called = true;
            scripts.HitGroup(BomberArena, BomberCallGroup, 1, ScriptRuntime::HitChainGun, 0);
        }

        if (_called && !_boarded)
        {
            auto found = std::find_if(scripts.Objects.begin(), scripts.Objects.end(),
                [](const auto& o) { return o->TypeName == "XE" && (o->Flags & Scripts::Rides::FlagRideable) != 0; });
            if (found != scripts.Objects.end())
            {
                auto& xe = **found;
                _boarded = true;
                auto top = scripts.GetWorldBounds(xe).Max.Z;
                auto feet = xe.Position;
                feet.Z = top + AboveXe;
                kurt.Teleport(feet, kurt.Yaw);
            }
        }

        if (test != BomberTest::Drop || scripts.Rides->Bomber == nullptr || scripts.Rides->Bomber->Locked())
        {
            return;
        }

        _fireSteps = _fireSteps < 0 ? FireSteps : Math::Max(_fireSteps - 1, 0);
        input.Hold(Key::Fire, _fireSteps > 0 ? Input::State::Down : Input::State::Up);
    }
}
