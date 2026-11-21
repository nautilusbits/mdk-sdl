#include "game/dev_tools/DevUi.h"

#include "core/DotNet.h"
#include "engine/platform/Input.h"
#include "engine/render/Renderer.h"
#include "game/dev_tools/ConsoleView.h"
#include "game/dev_tools/DevSession.h"
#include "game/dev_tools/DevTextView.h"
#include "game/dev_tools/OverlayText.h"
#include "game/dev_tools/OverlayView.h"
#include "game/hud/FontView.h"

namespace Mdk::Game::DevTools
{
    using Engine::Platform::RawKey;

    ConsoleState DevUi::Update(Engine::Platform::Input& input, float elapsed)
    {
        if (input.WasPressed(RawKey::F3))
        {
            _toggleOverlay();
        }

        auto grave = input.WasPressed(RawKey::Grave);
        if (_console->Update(input, elapsed) == ConsoleState::Closed && !grave)
        {
            return ConsoleState::Closed;
        }

        input.Withhold();
        return ConsoleState::Open;
    }

    void DevUi::RunOnce(const std::optional<std::string>& lines)
    {
        if (!lines || _optionRan)
        {
            return;
        }

        _optionRan = true;
        _console->Toggle();
        _console->Run(*lines);
    }

    DevUiView::DevUiView(Engine::Render::Renderer& renderer, std::shared_ptr<Hud::FontView> font)
        : _renderer(renderer)
    {
        _text = std::make_shared<DevTextView>(renderer, std::move(font));
        _overlay = std::make_shared<OverlayView>(*_text);
        _console = std::make_shared<ConsoleView>(*_text);
    }

    DevUiView::~DevUiView() = default;

    void DevUiView::Draw(DevUi& dev, DevSession& session, const std::function<std::vector<std::string>()>& screen)
    {
        if (session.Overlay == Switch::On)
        {
            auto seconds = static_cast<double>(StopwatchTicks() - _made) / StopwatchFrequency;
            if (_lines.empty() || seconds >= Refresh)
            {
                OverlaySample sample{&session.Profiler, _renderer.Stats(), MemorySample::Now(), screen()};
                sample.System = System;
                _lines = OverlayText::Lines(sample);
                _made = StopwatchTicks();
            }

            _overlay->Draw(_lines, ConsoleView::Bottom(dev.Console()));
        }

        _console->Draw(dev.Console(), session.Log);
    }
}
