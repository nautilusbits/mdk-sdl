#pragma once

#include <memory>

#include "Mdk.Engine/Diagnostics/LogRing.h"
#include "Mdk.Engine/Diagnostics/LogWriter.h"
#include "Mdk.Engine/Diagnostics/Profiler.h"
#include "Mdk.Game/DevTools/CommandHistory.h"
#include "Mdk.Game/DevTools/ICommandTarget.h"

namespace Mdk::Game::DevTools
{
    // What the developer tools keep for the whole session: the log the console shows (the
    // program's output), the frame timings, the console's history, whether the overlay is shown, and
    // god mode, noclip and onehit (every level's Kurt gets them).
    class DevSession
    {
    public:
        Engine::Diagnostics::LogRing& Log = Engine::Diagnostics::Log::Capture(Engine::Diagnostics::Log::Lines);
        Engine::Diagnostics::Profiler Profiler;
        std::shared_ptr<CommandHistory> History = std::make_shared<CommandHistory>();
        Switch Overlay = Switch::Off;
        Switch God = Switch::Off;
        Switch Noclip = Switch::Off;
        Switch OneHit = Switch::Off;

        Switch ToggleOverlay() { return Overlay = Overlay == Switch::On ? Switch::Off : Switch::On; }

        // A menu asked for the console (phones have no Grave key); the game opens it next frame.
        bool ConsoleAsked = false;
    };
}
