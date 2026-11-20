#pragma once

#include <memory>
#include <string>

#include "Mdk.Core/Cancellation.h"
#include "Mdk.Game/HdTextures/HdGenerator.h"

namespace Mdk::Formats
{
    class MdkData;
}

namespace Mdk::Game::HdTextures
{
    // A run of HdGenerator on a worker thread (detached; it holds the job) (the options' progress page): its progress, cancelled
    // on request; a failure ends it with its message.
    class HdJob
    {
    public:
        std::shared_ptr<HdProgress> Progress = std::make_shared<HdProgress>();

        static std::shared_ptr<HdJob> Start(const std::shared_ptr<Formats::MdkData>& data, const std::string& userFolder,
            const HdOptions& options);

        void Cancel() { _cancel.Cancel(); }

    private:
        CancellationTokenSource _cancel;

        HdJob() = default;

        void Run(const std::shared_ptr<Formats::MdkData>& data, const std::string& userFolder, const HdOptions& options);
    };
}
