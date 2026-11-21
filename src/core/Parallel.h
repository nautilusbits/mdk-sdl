#pragma once

// System.Threading.Tasks.Parallel.For: the indices shared out to a thread per core.
//   Parallel::For(0, count, [&](int i) { images[i] = Load(paths[i]); });

#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Mdk
{
    struct Parallel
    {
        // body(i) for every i in [first, last); the first exception thrown is rethrown after all stop.
        static void For(int first, int last, const std::function<void(int)>& body)
        {
            if (last <= first)
            {
                return;
            }

            const auto cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
            const auto workers = std::min(cores, last - first);
            std::atomic<int> next{first};
            std::exception_ptr error;
            std::mutex errorLock;
            const auto work = [&]
            {
                for (auto i = next++; i < last; i = next++)
                {
                    try
                    {
                        body(i);
                    }
                    catch (...)
                    {
                        std::lock_guard guard(errorLock);
                        if (!error)
                        {
                            error = std::current_exception();
                        }

                        next = last;
                    }
                }
            };

            std::vector<std::thread> threads;
            for (int w = 1; w < workers; w++)
            {
                threads.emplace_back(work);
            }

            work();
            for (auto& thread : threads)
            {
                thread.join();
            }

            if (error)
            {
                std::rethrow_exception(error);
            }
        }
    };
}
