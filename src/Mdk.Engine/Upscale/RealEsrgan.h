#pragma once

#include <functional>
#include <istream>
#include <string>

#include "Mdk.Core/Cancellation.h"
#include "Mdk.Engine/Upscale/UpscalerSource.h"

namespace Mdk
{
    class Process;
}

namespace Mdk::Engine::Upscale
{
    // The upscaler's network: Real-ESRGAN x4plus (photos, 4x only), or the lighter
    // animevideov3 (drawings, 2x to 4x).
    enum class UpscaleModel { General, Anime };

    // Real-ESRGAN's portable upscaler (realesrgan-ncnn-vulkan, BSD-3-Clause, with its models),
    // downloaded once from its release (UpscalerSource) into a folder of the user's,
    // checked against the archive's SHA-256. It runs on any Vulkan GPU (integrated ones too, slower);
    // without one it fails (its CPU path is far too slow to be offered).
    //   Install: release zip ─► SHA-256 as given? ─► extract ─► folder/realesrgan-ncnn-vulkan[.exe], models/
    //   Run:     folder of PNGs ─► the tool (-n model -s factor) ─► folder of PNGs (same names)
    class RealEsrgan
    {
    public:
        // Whether the folder holds the source's release, checked when it was extracted.
        static bool IsInstalled(const std::string& folder, const UpscalerSource& source);

        // The tool in folder, downloaded first when missing (progress: bytes so far, total or 0).
        static RealEsrgan Install(const std::string& folder, const UpscalerSource& source, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel);

        // The scale the network makes for a wanted one: x4plus only 4x (the caller shrinks).
        static int Factor(UpscaleModel model, int wanted);

        // The tool's name of a model.
        static std::string NameOf(UpscaleModel model);

        // Upscales every PNG of input into output by factor (Factor); done gets the count of
        // images written so far. Throws when the tool fails or leaves images out.
        void Run(const std::string& input, const std::string& output, UpscaleModel model, int factor, const std::function<void(int)>& done, const CancellationToken& cancel) const;

    private:
        static constexpr const char* ToolName = "realesrgan-ncnn-vulkan";
        // The archive while it's downloaded and checked.
        static constexpr const char* Download = "download.zip";
        // Written after a checked extraction: its content is the archive's SHA-256.
        static constexpr const char* Marker = "installed.sha256";
        static constexpr int PollMilliseconds = 200;
        static constexpr int CopyBuffer = 1 << 16;
        static constexpr int NativeFactor = 4;
        // The tool's last lines of output kept for an error.
        static constexpr size_t KeptLines = 20;

        class ToolLines;

        explicit RealEsrgan(std::string exe) : _exe(std::move(exe)) {}

        static std::string ExeIn(const std::string& folder);
        static std::string Fetch(const std::string& url, const std::string& path, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel);
        static std::string Copy(std::istream& source, long long total, const std::string& path, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel);

        static void Wait(Process& process, const std::string& output, const std::function<void(int)>& done, const CancellationToken& cancel);

        std::string _exe;
    };
}
