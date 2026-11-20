#include "Mdk.Engine/Platform/HeadTracker.h"

#include "Mdk.Core/DotNet.h"

namespace Mdk::Engine::Platform
{
    void HeadTracker::Set(HeadTracking tracking)
    {
        if ((tracking == HeadTracking::On) == (_gyro != nullptr))
        {
            return;
        }

        Close();
        if (tracking == HeadTracking::Off || !SDL_InitSubSystem(SDL_INIT_SENSOR))
        {
            return;
        }

        int count = 0;
        SDL_SensorID* sensors = SDL_GetSensors(&count);
        for (int i = 0; i < count && _gyro == nullptr; i++)
        {
            if (SDL_GetSensorTypeForID(sensors[i]) == SDL_SENSOR_GYRO)
            {
                _gyro = SDL_OpenSensor(sensors[i]);
            }
        }

        SDL_free(sensors);
    }

    bool HeadTracker::Event(const SDL_Event& e, Input& input, bool looking, SDL_Window* window)
    {
        if (static_cast<SDL_EventType>(e.type) != SDL_EVENT_SENSOR_UPDATE)
        {
            return false;
        }

        if (_gyro == nullptr)
        {
            return true;
        }

        // The sensor's own clock when it has one, else the event's.
        std::uint64_t reading = e.sensor.sensor_timestamp != 0 ? e.sensor.sensor_timestamp : e.sensor.timestamp;
        float seconds = _lastReading == 0 ? 0.0f : MathF::Min(static_cast<float>(reading - _lastReading) / NanosecondsPerSecond, MaxStep);
        _lastReading = reading;
        if (!looking || seconds <= 0.0f)
        {
            return true;
        }

        const float* data = e.sensor.data;
        Vector3 rate(data[0], data[1], data[Axes - 1]);
        input.AddLook(HeadLook::FromGyro(rate, seconds, Turn(window)));
        return true;
    }

    ScreenTurn HeadTracker::Turn(SDL_Window* window)
    {
        return SDL_GetCurrentDisplayOrientation(SDL_GetDisplayForWindow(window)) == SDL_ORIENTATION_LANDSCAPE_FLIPPED
                   ? ScreenTurn::LandscapeFlipped
                   : ScreenTurn::Landscape;
    }

    void HeadTracker::Close()
    {
        if (_gyro == nullptr)
        {
            return;
        }

        SDL_CloseSensor(_gyro);
        _gyro = nullptr;
        _lastReading = 0;
    }
}
