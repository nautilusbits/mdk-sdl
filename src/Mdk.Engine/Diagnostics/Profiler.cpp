#include "Mdk.Engine/Diagnostics/Profiler.h"

#include <algorithm>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Engine::Diagnostics
{
    Profiler::Measurement::Measurement(Profiler& profiler, Section section)
        : _profiler(profiler), _section(section), _start(StopwatchTicks())
    {
    }

    Profiler::Measurement::~Measurement() { _profiler.Add(_section, StopwatchTicks() - _start); }

    Profiler::Profiler() : _frameStart(StopwatchTicks()) {}

    void Profiler::EndFrame()
    {
        long long now = StopwatchTicks();
        float frame = Milliseconds(now - _frameStart);
        _frameStart = now;
        float weight = _started ? Smoothing : 1.0f;
        _started = true;
        _frame += (frame - _frame) * weight;
        _lastFrameMilliseconds = frame;
        for (size_t i = 0; i < _ticks.size(); i++)
        {
            _last[i] = Milliseconds(_ticks[i]);
            _average[i] += (_last[i] - _average[i]) * weight;
            _ticks[i] = 0;
        }
    }

    void Profiler::CopyLast(std::span<float> sections) const
    {
        std::copy(_last.begin(), _last.end(), sections.begin());
    }

    float Profiler::FramesPerSecond() const
    {
        return _frame > 0.0f ? static_cast<float>(MillisecondsPerSecond / _frame) : 0.0f;
    }

    float Profiler::Milliseconds(long long ticks)
    {
        return static_cast<float>(ticks * MillisecondsPerSecond / StopwatchFrequency);
    }
}
