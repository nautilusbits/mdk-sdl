#pragma once

#include <optional>
#include <string>

namespace Mdk::Android
{
    // The setup screen (Java: SetupActivity) as the game calls it from SDL's thread: shown over
    // the game, waited for.
    struct SetupActivity
    {
        // Options' "Import from folder" (SDL's thread): returns once it's done or cancelled.
        static void Reimport(const std::optional<std::string>& inPlace);

        // Why the game stopped (SDL's thread): returns once it's read.
        static void Stopped(const std::string& message);
    };
}
