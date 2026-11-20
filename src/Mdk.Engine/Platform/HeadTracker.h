#pragma once

#include <cstdint>

#include <SDL3/SDL.h>

#include "Mdk.Engine/Platform/HeadLook.h"
#include "Mdk.Engine/Platform/Input.h"

namespace Mdk::Engine::Platform
{
    // Whether the phone's gyroscope turns the view (a VR viewer: the head looks around).
    enum class HeadTracking { Off, On };

    // The device's gyroscope (SDL's sensors; phones), its turns added to Input's look through
    // HeadLook. Without a gyroscope it does nothing.
    class HeadTracker
    {
    public:
        HeadTracker() = default;
        ~HeadTracker() { Close(); }
        HeadTracker(const HeadTracker&) = delete;
        HeadTracker& operator=(const HeadTracker&) = delete;

        // Opens the gyroscope, or closes it.
        void Set(HeadTracking tracking);

        // Handles a sensor event (false for the others): the head's turn since the last reading,
        // while `looking` (play), for the window's way up.
        bool Event(const SDL_Event& e, Input& input, bool looking, SDL_Window* window);

    private:
        static constexpr float NanosecondsPerSecond = 1e9f;
        // A longer gap between readings (a pause) turns nothing.
        static constexpr float MaxStep = 0.1f;
        static constexpr int Axes = 3;

        static ScreenTurn Turn(SDL_Window* window);

        void Close();

        SDL_Sensor* _gyro = nullptr;
        std::uint64_t _lastReading = 0;
    };
}
