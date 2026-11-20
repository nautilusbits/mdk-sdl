#pragma once

#include <string>
#include <vector>

#include "Mdk.Engine/Diagnostics/Profiler.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/DevTools/ICommandTarget.h"

namespace Mdk::Game::DevTools
{
    // The program's memory: the GC heap, the working set and the collections per generation.
    struct MemorySample
    {
        long long GcHeap = 0;
        long long WorkingSet = 0;
        std::vector<int> Collections;

        bool operator==(const MemorySample&) const = default;

        // C++ has no GC: the heap is 0 and there are no collections; the working set is the process's.
        static MemorySample Now();
    };

    // What the debug overlay shows of a frame: timings, draws, memory, the GPU and display
    // (System), and the screen's own lines (a menu's name, the level's Kurt, the stream's segment).
    struct OverlaySample
    {
        const Engine::Diagnostics::Profiler* Profiler = nullptr;
        Engine::Render::RenderStats Render;
        MemorySample Memory;
        std::vector<std::string> Screen;
        // The GPU's and the display's lines (TechInfo.Overlay).
        std::vector<std::string> System;
    };

    // The debug overlay's lines; the last are the screen's (a level's here).
    //   FPS 60 (16.7 ms)
    //   scene 0.3  render 5.2  physics 0.4  scripts 1.1  audio 0.2 ms
    //   draw calls 120, triangles 4500, GPU wait 4.1 ms   (render includes the wait: vsync, the GPU)
    //   GC heap 12.0 MB, working set 80.0 MB, collections 3/2/1
    //   GPU NVIDIA GeForce RTX 3060, Vulkan
    //   window 1280x960 windowed, target 1280x960, B8G8R8A8_UNORM, VSYNC
    //   Kurt 1.00 -2.50 30.00 yaw 270 Run
    //   level 6 arena ARENA_2, objects 42
    struct OverlayText
    {
        static std::vector<std::string> Lines(const OverlaySample& sample);

        // A level's lines: Kurt, his state, the level, his arena and the objects.
        static std::vector<std::string> Level(const Placement& kurt, int objects, const std::string& state);

    private:
        static constexpr double Megabyte = 1024.0 * 1024.0;

        // value.ToString(format) for the overlay's formats ("0.0" and the like).
        static std::string Format(double value, const std::string& format);
    };
}
