#pragma once

#include <optional>
#include <string>

namespace Mdk::Game
{
    // --frames=folder,count[,every]: after the wait, every `Every`th frame (60 a second) is saved as
    // frame000.bmp... until `Count` are, then the game quits (the README's animation: 4 gives 15
    // frames a second).
    struct FrameDump
    {
        std::string Folder;
        int Count = 0;
        int Every = 0;

        bool operator==(const FrameDump&) const = default;

        static FrameDump Parse(const std::string& text);

        // The file of the frame drawn `step` frames after the wait, or null when that one isn't saved.
        std::optional<std::string> PathOf(int step) const;

        // The last frame was saved at this step.
        bool IsLast(int step) const { return step / Every >= Count - 1; }

    private:
        static constexpr int DefaultEvery = 4;
    };
}
