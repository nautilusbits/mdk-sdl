#include "game/SniperTest.h"

#include <vector>

#include "core/DotNet.h"
#include "engine/platform/Input.h"
#include "game/kurt/Kurt.h"
#include "game/kurt/Scope.h"
#include "game/scripts/ScriptRuntime.h"
#include "game/scripts/StrikeScene.h"
#include "game/Viewer.h"

namespace Mdk::Game
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Scripts::ScriptRuntime;
    using Scripts::StrikeScene;

    void SniperTest::Step(Kurt::Kurt& kurt, ScriptRuntime& scripts, Input& input, float time)
    {
        if (_options.Strike.has_value() && !_struck && time >= StrikeStart)
        {
            _struck = true;
            scripts.PlayStrikeScene(StrikeScene::Kind::Bones, *_options.Strike);
        }

        if (!_options.Sniper.has_value())
        {
            return;
        }

        auto setup = *_options.Sniper;
        if (!_entered.has_value())
        {
            if (!kurt.OnFloor || time < SettleTime)
            {
                return;
            }

            kurt.EnterSniper();
            kurt.Scope->Zoom = setup.X;
            kurt.Scope->Pitch = setup.Y;
            _entered = time;
        }

        auto since = time - *_entered;
        input.Hold(Key::ZoomIn, since < _options.Zoom ? Input::State::Down : Input::State::Up);
        auto shot = _options.SniperFire && since >= ShotStart && since < ShotStart + ShotTime;
        input.Hold(Key::Fire, _options.Fire || shot ? Input::State::Down : Input::State::Up);
    }

    Vector2 SniperTest::Setup(const std::string& text)
    {
        std::vector<float> values;
        for (const auto& v : Str::Split(text, ',', Str::SplitOptions::RemoveEmpty))
        {
            values.push_back(Str::ParseFloat(v));
        }

        return Vector2(values.size() > 0 ? values[0] : 1.f, values.size() > 1 ? values[1] : 0.f);
    }
}
