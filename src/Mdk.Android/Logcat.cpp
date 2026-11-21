#include "Mdk.Android/Logcat.h"

#include <android/log.h>

namespace Mdk::Android
{
    namespace
    {
        constexpr const char* Tag = "MDK";
    }

    void Logcat::Write(std::string_view text)
    {
        _inner.Write(text);
        std::lock_guard guard(_lock);
        for (auto c : text)
        {
            // A line ends at \n; \r is dropped.
            if (c == '\r')
            {
                continue;
            }

            if (c != '\n')
            {
                _line += c;
                continue;
            }

            __android_log_write(ANDROID_LOG_INFO, Tag, _line.c_str());
            _line.clear();
        }
    }

    void Logcat::Install()
    {
        Console::SetOut(new Logcat(Console::Out()));
        Console::SetError(new Logcat(Console::Error()));
    }
}
