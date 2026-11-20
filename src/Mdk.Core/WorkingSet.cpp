#include "Mdk.Core/WorkingSet.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <cstdio>
#include <unistd.h>
#endif

namespace Mdk
{
    long long WorkingSet()
    {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS counters{};
        if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        {
            return 0;
        }

        return static_cast<long long>(counters.WorkingSetSize);
#elif defined(__APPLE__)
        mach_task_basic_info info{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        {
            return 0;
        }

        return static_cast<long long>(info.resident_size);
#else
        // /proc/self/statm: total pages, then resident pages.
        auto* file = std::fopen("/proc/self/statm", "r");
        if (file == nullptr)
        {
            return 0;
        }

        long long total = 0;
        long long resident = 0;
        auto read = std::fscanf(file, "%lld %lld", &total, &resident);
        std::fclose(file);
        if (read != 2)
        {
            return 0;
        }

        return resident * sysconf(_SC_PAGESIZE);
#endif
    }
}
