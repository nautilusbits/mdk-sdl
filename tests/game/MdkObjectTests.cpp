#include <doctest.h>

#include "game/objects/MdkObject.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace Mdk;
using namespace Mdk::Formats;
using namespace Mdk::Game::Objects;

// Found by flaky test runs: every object shares one part bounds cache, and test classes
// running in parallel corrupted it ("concurrent update was performed on this collection").
namespace
{
    constexpr int Threads = 8;
    constexpr int ObjectsPerThread = 2000;
}

// Objects of different models on many threads at once all get their bounds.
TEST_CASE("MdkObjectTests.PartBoundsAreThreadSafe")
{
    // doctest asserts aren't thread-safe: count the misses, check once.
    std::atomic<int> wrong = 0;
    std::vector<std::thread> threads;
    for (int t = 0; t < Threads; t++)
    {
        threads.emplace_back([&wrong]
        {
            for (int i = 0; i < ObjectsPerThread; i++)
            {
                auto part = std::make_shared<Model::Part>();
                part->Vertices = {Vector3::Zero(), Vector3(static_cast<float>(i))};
                auto model = std::make_shared<Model>();
                model->PartList = {part};

                auto object = std::make_shared<MdkObject>();
                object->Model = model;
                const auto& bounds = object->PartBounds();
                if (!bounds[0].has_value() || bounds[0]->Max != Vector3(static_cast<float>(i)))
                {
                    wrong++;
                }
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    CHECK(wrong == 0);
}
