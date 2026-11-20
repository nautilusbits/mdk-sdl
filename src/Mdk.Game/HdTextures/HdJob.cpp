#include "Mdk.Game/HdTextures/HdJob.h"

#include <format>
#include <thread>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/DotNet.h"

namespace Mdk::Game::HdTextures
{
    std::shared_ptr<HdJob> HdJob::Start(const std::shared_ptr<Formats::MdkData>& data, const std::string& userFolder,
        const HdOptions& options)
    {
        auto job = std::shared_ptr<HdJob>(new HdJob());
        std::thread([job, data, userFolder, options] { job->Run(data, userFolder, options); }).detach();
        return job;
    }

    void HdJob::Run(const std::shared_ptr<Formats::MdkData>& data, const std::string& userFolder, const HdOptions& options)
    {
        try
        {
            HdGenerator::Run(data, userFolder, options, *Progress, _cancel.Token());
        }
        catch (const OperationCanceledException&)
        {
            Progress->Set(HdProgress::Stage::Cancelled, 0, 0);
        }
        catch (const std::exception& e)
        {
            Console::Error().WriteLine(std::format("HD textures failed: {}", e.what()));
            Progress->Set(HdProgress::Stage::Failed, 0, 0, Str::Split(e.what(), '\n')[0]);
        }
    }
}
