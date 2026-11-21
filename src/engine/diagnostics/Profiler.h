#pragma once

#include <array>
#include <span>

namespace Mdk::Engine::Diagnostics
{
    // The parts of a frame the profiler times: the game's draws (scene), the renderer's frame
    // (render), Kurt and the cameras (physics), the scripts, the sounds.
    enum class Section { Scene, Render, Physics, Scripts, Audio };

    constexpr int SectionCount = 5;

    // Times the sections of each frame and the frames themselves, averaged over the last frames
    // (the debug overlay shows them).
    //   { auto m = profiler.Measure(Section::Scripts); ... }   // adds to this frame's scripts time
    //   profiler.EndFrame();                                  // averages, starts the next frame
    class Profiler
    {
    public:
        // Times its scope into `section`.
        class Measurement
        {
        public:
            Measurement(Profiler& profiler, Section section);
            ~Measurement();
            Measurement(const Measurement&) = delete;
            Measurement& operator=(const Measurement&) = delete;

        private:
            Profiler& _profiler;
            Section _section;
            long long _start;
        };

        Profiler();

        [[nodiscard]] Measurement Measure(Section section) { return {*this, section}; }

        // Adds `ticks` (the stopwatch's) to this frame's section.
        void Add(Section section, long long ticks) { _ticks[static_cast<int>(section)] += ticks; }

        // The frame is over: its times go into the averages.
        void EndFrame();

        // A section's average time per frame.
        float Milliseconds(Section section) const { return _average[static_cast<int>(section)]; }

        // A section's time in the last ended frame.
        float LastMilliseconds(Section section) const { return _last[static_cast<int>(section)]; }

        // Copies each section's time in the last ended frame (by Section).
        void CopyLast(std::span<float> sections) const;

        // The last ended frame's time.
        float LastFrameMilliseconds() const { return _lastFrameMilliseconds; }

        // The average frame's time and the frames per second.
        float FrameMilliseconds() const { return _frame; }

        float FramesPerSecond() const;

    private:
        // Each frame weighs this much in the averages (about the last 10 frames).
        static constexpr float Smoothing = 0.1f;
        static constexpr double MillisecondsPerSecond = 1000.0;

        static float Milliseconds(long long ticks);

        std::array<long long, SectionCount> _ticks{};
        std::array<float, SectionCount> _average{};
        std::array<float, SectionCount> _last{};
        long long _frameStart;
        float _frame = 0;
        bool _started = false;
        float _lastFrameMilliseconds = 0;
    };
}
