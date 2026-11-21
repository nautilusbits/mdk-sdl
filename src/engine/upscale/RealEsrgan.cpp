#include "engine/upscale/RealEsrgan.h"

#include <deque>
#include <filesystem>
#include <format>
#include <mutex>
#include <vector>

#include "core/DotNet.h"
#include "core/FileStream.h"
#include "core/Files.h"
#include "core/Process.h"
#include "core/Sha256.h"
#include "core/Uri.h"
#include "core/ZipFile.h"
#include "engine/platform/HttpDownload.h"

namespace Mdk::Engine::Upscale
{
    // The tool's last lines (a percentage per tile; the last ones tell why it failed).
    class RealEsrgan::ToolLines
    {
    public:
        void Keep(const std::optional<std::string>& data)
        {
            if (!data)
            {
                return;
            }

            std::lock_guard lock(_lock);
            _lines.push_back(*data);
            if (_lines.size() > KeptLines)
            {
                _lines.pop_front();
            }
        }

        std::string ToString()
        {
            std::lock_guard lock(_lock);
            return Str::Join("\n", std::vector<std::string>(_lines.begin(), _lines.end()));
        }

    private:
        std::mutex _lock;
        std::deque<std::string> _lines;
    };

    std::string RealEsrgan::ExeIn(const std::string& folder)
    {
#if defined(_WIN32)
        return Path::Combine(folder, std::string(ToolName) + ".exe");
#else
        return Path::Combine(folder, ToolName);
#endif
    }

    bool RealEsrgan::IsInstalled(const std::string& folder, const UpscalerSource& source)
    {
        auto marker = Path::Combine(folder, Marker);
        return File::Exists(ExeIn(folder)) && File::Exists(marker) && Str::Trim(File::ReadAllText(marker)) == source.Sha256;
    }

    RealEsrgan RealEsrgan::Install(const std::string& folder, const UpscalerSource& source, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel)
    {
        if (IsInstalled(folder, source))
        {
            return RealEsrgan(ExeIn(folder));
        }

        // A release of the user's needs its checksum too (settings.cfg).
        if (source.Sha256.empty())
        {
            throw InvalidDataException(std::format("upscaler_sha256 needed for {}", source.Url));
        }

        Directory::CreateDirectory(folder);
        auto download = Path::Combine(folder, Download);
        try
        {
            auto hash = Fetch(source.Url, download, progress, cancel);
            if (hash != source.Sha256)
            {
                throw InvalidDataException(std::format("{}: SHA-256 {}, expected {}", source.Url, hash, source.Sha256));
            }

            ZipFile::ExtractToDirectory(download, folder, ZipFile::Existing::Overwrite);
        }
        catch (...)
        {
            File::Delete(download);
            throw;
        }

        File::Delete(download);

#if !defined(_WIN32)
        std::filesystem::permissions(FileStream::NativePath(ExeIn(folder)), std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
#endif

        File::WriteAllText(Path::Combine(folder, Marker), source.Sha256);
        return RealEsrgan(ExeIn(folder));
    }

    // Saves a URL's file (http(s), file or a local path); returns its SHA-256 (hex).
    std::string RealEsrgan::Fetch(const std::string& url, const std::string& path, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel)
    {
        auto uri = Uri::TryCreate(url);
        if (uri && !uri->IsFile())
        {
            Platform::HttpDownload::Save(url, path, progress, cancel);
            auto saved = FileStream::OpenRead(path);
            return Convert::ToHexStringLower(Sha256::HashData(saved));
        }

        auto localPath = uri ? uri->LocalPath() : url;
        auto local = FileStream::OpenRead(localPath);
        return Copy(local, File::Length(localPath), path, progress, cancel);
    }

    // Copies a stream into a file, hashing it.
    std::string RealEsrgan::Copy(std::istream& source, long long total, const std::string& path, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel)
    {
        auto file = FileStream::Create(path);
        Sha256 hash;
        std::vector<byte> buffer(CopyBuffer);
        long long done = 0;
        while (true)
        {
            source.read(reinterpret_cast<char*>(buffer.data()), CopyBuffer);
            auto read = static_cast<size_t>(source.gcount());
            if (read == 0)
            {
                break;
            }

            cancel.ThrowIfCancellationRequested();
            file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(read));
            if (!file)
            {
                throw IOException(std::format("{}: write failed", path));
            }

            hash.AppendData(std::span<const byte>(buffer.data(), read));
            done += static_cast<long long>(read);
            if (progress)
            {
                progress(done, total);
            }
        }

        return Convert::ToHexStringLower(hash.GetHashAndReset());
    }

    int RealEsrgan::Factor(UpscaleModel model, int wanted)
    {
        return model == UpscaleModel::General ? NativeFactor : wanted;
    }

    std::string RealEsrgan::NameOf(UpscaleModel model)
    {
        return model == UpscaleModel::General ? "realesrgan-x4plus" : "realesr-animevideov3";
    }

    void RealEsrgan::Run(const std::string& input, const std::string& output, UpscaleModel model, int factor, const std::function<void(int)>& done, const CancellationToken& cancel) const
    {
        Directory::CreateDirectory(output);
        auto expected = static_cast<int>(Directory::GetFiles(input, "*.png").size());
        ProcessStartInfo info;
        info.FileName = _exe;
        info.ArgumentList = {"-i", input, "-o", output, "-n", NameOf(model), "-s", std::to_string(factor), "-f", "png"};
        info.WorkingDirectory = Path::GetDirectoryName(_exe);
        info.RedirectStandardError = true;
        info.RedirectStandardOutput = true;
        info.CreateNoWindow = true;

        // Before the process: its readers write here until it's gone.
        ToolLines lines;
        auto process = Process::Start(info);
        if (process == nullptr)
        {
            throw InvalidOperationException(std::format("{} didn't start", _exe));
        }

        process->ErrorDataReceived = [&lines](const std::optional<std::string>& data) { lines.Keep(data); };
        process->OutputDataReceived = [&lines](const std::optional<std::string>& data) { lines.Keep(data); };
        process->BeginErrorReadLine();
        process->BeginOutputReadLine();

        // The game quitting doesn't leave the tool running.
        auto stop = AppDomain::AddProcessExit([&process] { process->Kill(ProcessTree::Entire); });
        try
        {
            Wait(*process, output, done, cancel);
        }
        catch (...)
        {
            AppDomain::RemoveProcessExit(stop);
            throw;
        }

        AppDomain::RemoveProcessExit(stop);

        auto written = static_cast<int>(Directory::GetFiles(output, "*.png").size());
        if (done)
        {
            done(written);
        }

        if (process->ExitCode() == 0 && written == expected)
        {
            return;
        }

        throw InvalidOperationException(std::format("{} failed (exit {}, {} of {} images; a Vulkan GPU is needed):\n{}", ToolName, process->ExitCode(), written, expected, lines.ToString()));
    }

    // Waits for the tool, counting its images; kills it when cancelled.
    void RealEsrgan::Wait(Process& process, const std::string& output, const std::function<void(int)>& done, const CancellationToken& cancel)
    {
        while (!process.WaitForExit(PollMilliseconds))
        {
            if (cancel.IsCancellationRequested())
            {
                process.Kill(ProcessTree::Entire);
                process.WaitForExit();
                cancel.ThrowIfCancellationRequested();
            }

            if (done)
            {
                done(static_cast<int>(Directory::GetFiles(output, "*.png").size()));
            }
        }

        process.WaitForExit();
    }
}
