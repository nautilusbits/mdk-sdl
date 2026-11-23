#include "game/hd_textures/HdGenerator.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <unordered_set>

#include "core/Console.h"
#include "core/DotNet.h"
#include "core/Files.h"
#include "core/Parallel.h"
#include "engine/render/ColourMips.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "formats/Dti.h"
#include "formats/MdkData.h"
#include "formats/Palette.h"
#include "formats/Png.h"
#include "formats/Texture.h"
#include "game/audio/SoundBank.h"
#include "game/level/LevelData.h"
#include "game/mods/CanvasExport.h"
#include "game/mods/ModCatalog.h"

namespace Mdk::Game::HdTextures
{
    using Engine::Upscale::RealEsrgan;
    using Mods::CanvasExport;
    using Mods::ModImages;
    using Mods::ModInfo;

    namespace
    {
        // sources.DistinctBy(s => s.Key): the first of each key, in order.
        std::vector<HdSource> DistinctByKey(const std::vector<HdSource>& sources)
        {
            std::unordered_set<std::string> keys;
            std::vector<HdSource> distinct;
            for (const auto& s : sources)
            {
                if (keys.insert(s.Key).second)
                {
                    distinct.push_back(s);
                }
            }

            return distinct;
        }

        // The sources the cache lacks (no entry, or its file gone), each key once over the run.
        void AddMissing(std::vector<HdSource>& todo, std::unordered_set<std::string>& keys, const std::vector<HdSource>& sources,
            const HdManifest& manifest, const std::string& folder)
        {
            for (const auto& s : sources)
            {
                const auto* e = manifest.Entries.Find(s.Key);
                if (e != nullptr && File::Exists(Path::Combine(folder, e->File)))
                {
                    continue;
                }

                if (keys.insert(s.Key).second)
                {
                    todo.push_back(s);
                }
            }
        }
    }

    // ---- HdProgress ----

    void HdProgress::Set(Stage stage, long long done, long long total, const std::string& message)
    {
        std::lock_guard guard(_lock);
        _stage = stage;
        _done = done;
        _total = total;
        _message = message;
    }

    std::tuple<HdProgress::Stage, long long, long long, std::string> HdProgress::Read()
    {
        std::lock_guard guard(_lock);
        return {_stage, _done, _total, _message};
    }

    std::string HdProgress::Describe()
    {
        const auto [stage, done, total, message] = Read();
        switch (stage)
        {
            case Stage::Download:
                return std::format("Downloading {}/{} MB", done >> 20, total >> 20);
            case Stage::Read:
                return std::format("Reading level {}", done);
            case Stage::Export:
                return std::format("Exporting {}/{}", done, total);
            case Stage::Upscale:
                return std::format("Upscaling {}/{}", done, total);
            case Stage::Save:
                return std::format("Saving {}/{}", done, total);
            case Stage::Done:
                return std::format("Done: {} made", done);
            case Stage::Cancelled:
                return "Cancelled";
            default:
                return std::format("Failed: {}", std::ssize(message) > MaxMessage ? message.substr(0, MaxMessage) : message);
        }
    }

    bool HdProgress::Finished()
    {
        const auto stage = std::get<0>(Read());
        return stage == Stage::Done || stage == Stage::Failed || stage == Stage::Cancelled;
    }

    // ---- HdGenerator ----

    std::string HdGenerator::ToolIn(const std::string& userFolder)
    {
        return Path::Combine(userFolder, ToolsFolder, ToolFolder);
    }

    std::string HdGenerator::Run(const std::shared_ptr<Formats::MdkData>& data, const std::string& userFolder,
        const HdOptions& options, HdProgress& progress, const CancellationToken& cancel)
    {
        if (std::find(HdOptions::Scales.begin(), HdOptions::Scales.end(), options.Scale) == HdOptions::Scales.end())
        {
            std::vector<std::string> scales;
            for (const auto s : HdOptions::Scales)
            {
                scales.push_back(std::to_string(s));
            }

            throw ArgumentException(std::format("HD textures are made {} times larger", Str::Join(" or ", scales)));
        }

        // An older build's cache first: its current images aren't made again.
        Migrate(userFolder);
        const auto start = StopwatchSeconds();
        progress.Set(HdProgress::Stage::Download, 0, 0);
        auto tool = RealEsrgan::Install(ToolIn(userFolder), options.Source,
            [&](long long done, long long total) { progress.Set(HdProgress::Stage::Download, done, total); }, cancel);

        const auto folder = FolderIn(userFolder);
        Directory::CreateDirectory(folder);
        const auto model = RealEsrgan::NameOf(options.Upscaler());
        auto manifest = Current(folder, model, options.Scale);
        File::WriteAllText(Path::Combine(folder, ModInfo::FileName), Info(model, options.Scale).Format());

        // What each level shows, and what the cache lacks.
        std::vector<HdSource> todo;
        std::unordered_set<std::string> keys;
        std::vector<std::string> sizes;
        const auto sprites = Formats::Bni::Load(data->PathOf("TRAVERSE/TRAVSPRT.BNI"));
        for (const auto number : options.Levels)
        {
            progress.Set(HdProgress::Stage::Read, number, 0);
            Level::LevelData level(*data, number);
            const auto cmi = Formats::Cmi::Load(data->PathOf(std::format("TRAVERSE/LEVEL{}/LEVEL{}.CMI", number, number)));
            auto all = TextureExport::Collect(level, *cmi);
            const auto bank = Audio::SoundBank::ForLevel(*data, number);
            for (auto& kurt : TextureExport::Kurt(level, *sprites, [&](const std::string& name) { return bank->Animation(name); }))
            {
                all.push_back(std::move(kurt));
            }

            for (auto& image : CanvasExport::Level(level, *sprites))
            {
                all.push_back(std::move(image));
            }

            all.push_back(TextureExport::Sky(number, *level.Dti));

            const auto sources = DistinctByKey(all);
            Prune(folder, *manifest, number, sources);
            // Levels share some images: each is made once.
            AddMissing(todo, keys, sources, *manifest, folder);
            sizes.push_back(Sizes(number, sources, options.Scale));
        }

        // The menus' 2D images (no level's), the fonts left as made (upscaled, they blur).
        const auto menus = DistinctByKey(CanvasExport::Menus(data, CanvasExport::Fonts::Skipped));
        Prune(folder, *manifest, CanvasExport::NoLevel, menus);
        AddMissing(todo, keys, menus, *manifest, folder);

        manifest->Save(folder);
        const auto work = Path::Combine(folder, WorkFolder);
        try
        {
            if (!todo.empty())
            {
                Make(tool, todo, work, folder, *manifest, options, progress, cancel);
            }
        }
        catch (...)
        {
            if (Directory::Exists(work))
            {
                Directory::Delete(work, true);
            }

            throw;
        }

        if (Directory::Exists(work))
        {
            Directory::Delete(work, true);
        }

        manifest->Save(folder);
        const auto seconds = static_cast<long long>(Math::RoundAwayFromZero(StopwatchSeconds() - start));
        const auto summary = std::format("{} textures made, {} in the mod, {} MB on disk, {} s",
            todo.size(), manifest->Entries.Count(), FolderBytes(folder) / BytesPerMegabyte, seconds);
        for (const auto& line : sizes)
        {
            Console::WriteLine(line);
        }

        progress.Set(HdProgress::Stage::Done, std::ssize(todo), std::ssize(todo), summary);
        return summary;
    }

    std::string HdGenerator::FolderIn(const std::string& userFolder)
    {
        return Path::Combine(Mods::ModCatalog::FolderIn(userFolder), ModFolder);
    }

    ModInfo HdGenerator::Info(const std::string& model, int scale)
    {
        return ModInfo{ModFolder, "HD textures (Real-ESRGAN)", "made on this computer", "", std::format("{}, {}x", model, scale), ModPriority};
    }

    int HdGenerator::Migrate(const std::string& userFolder)
    {
        const auto old = Path::Combine(userFolder, OldFolder);
        const auto folder = FolderIn(userFolder);
        const auto manifest = HdManifest::Load(old);
        if (manifest == nullptr || HdManifest::Load(folder) != nullptr)
        {
            return 0;
        }

        HdManifest moved(manifest->Model(), manifest->Scale());
        moved.Version = manifest->Version;
        for (const auto& [key, entry] : manifest->Entries)
        {
            const auto source = Path::Combine(old, entry.File);
            if (!File::Exists(source))
            {
                continue;
            }

            const auto file = FileOf(entry.Level, entry.Name, entry.Key);
            const auto target = Path::Combine(folder, file);
            Directory::CreateDirectory(Path::GetDirectoryName(target));
            File::Move(source, target, true);
            auto movedEntry = entry;
            movedEntry.File = file;
            moved.Entries[entry.Key] = movedEntry;
        }

        // An old cache without images still becomes the (empty) mod.
        Directory::CreateDirectory(folder);
        moved.Save(folder);
        File::WriteAllText(Path::Combine(folder, ModInfo::FileName), Info(manifest->Model(), manifest->Scale()).Format());
        Directory::Delete(old, true);
        return moved.Entries.Count();
    }

    std::string HdGenerator::FileOf(int level, const std::string& name, const std::string& key, ModImages::Kind kind)
    {
        if (level == CanvasExport::NoLevel)
        {
            return std::format("{}/{}@{}.png", ModImages::FolderOf(kind), SafeName(name), key);
        }

        return std::format("{}/{}/{}@{}.png", ModImages::FolderOf(kind), ModImages::LevelFolder(level), SafeName(name), key);
    }

    std::shared_ptr<HdManifest> HdGenerator::Current(const std::string& folder, const std::string& model, int scale)
    {
        auto manifest = HdManifest::Load(folder);
        if (manifest != nullptr && manifest->Version == HdManifest::Format && manifest->Model() == model && manifest->Scale() == scale)
        {
            return manifest;
        }

        if (manifest != nullptr)
        {
            for (const auto& [key, entry] : manifest->Entries)
            {
                File::Delete(Path::Combine(folder, entry.File));
            }
        }

        return std::make_shared<HdManifest>(model, scale);
    }

    void HdGenerator::Prune(const std::string& folder, HdManifest& manifest, int level, const std::vector<HdSource>& sources)
    {
        std::unordered_set<std::string> keys;
        for (const auto& s : sources)
        {
            keys.insert(s.Key);
        }

        std::vector<HdManifest::Entry> stale;
        for (const auto& [key, e] : manifest.Entries)
        {
            if (e.Level == level && !keys.contains(e.Key))
            {
                stale.push_back(e);
            }
        }

        for (const auto& entry : stale)
        {
            File::Delete(Path::Combine(folder, entry.File));
            manifest.Entries.Remove(entry.Key);
        }
    }

    std::string HdGenerator::Sizes(int level, const std::vector<HdSource>& sources, int scale)
    {
        long long original = 0, hd = 0;
        for (const auto& s : sources)
        {
            const auto& t = *s.Texture;
            original += static_cast<long long>(Engine::Render::ColourMips::ChainSize(t.Width, t.Height)) * t.FrameCount;
            hd += static_cast<long long>(Engine::Render::ColourMips::ChainSize(t.Width * scale, t.Height * scale)) * t.FrameCount;
        }

        return std::format("LEVEL{}: {} textures, colour textures {} MB, HD {} MB", level, sources.size(), original / BytesPerMegabyte,
            hd / BytesPerMegabyte);
    }

    void HdGenerator::Make(Engine::Upscale::RealEsrgan& tool, const std::vector<HdSource>& todo, const std::string& work,
        const std::string& folder, HdManifest& manifest, const HdOptions& options, HdProgress& progress,
        const CancellationToken& cancel)
    {
        if (Directory::Exists(work))
        {
            Directory::Delete(work, true);
        }

        const auto input = Path::GetFullPath(Path::Combine(work, "in"));
        Directory::CreateDirectory(input);
        const auto output = Path::Combine(work, "out");
        long long frames = 0;
        for (const auto& s : todo)
        {
            frames += s.Texture->FrameCount;
        }

        // ParallelOptions.CancellationToken: no iteration starts once cancelled.
        std::atomic<long long> exported{0};
        Parallel::For(0, static_cast<int>(todo.size()), [&](int i)
        {
            cancel.ThrowIfCancellationRequested();
            const auto& source = todo[i];
            const auto& t = *source.Texture;
            for (int f = 0; f < t.FrameCount; f++)
            {
                const auto padded = UpscaleImages::Input(TextureExport::Frame(t, *source.Palette, f, source.ZeroOpaque),
                    t.Width, t.Height, Margin, source.WrapY);
                const auto [width, height] = UpscaleImages::Padded(t.Width, t.Height, Margin);
                File::WriteAllBytes(Path::Combine(input, FrameFile(source, f)), Formats::Png::Encode(width, height, padded, Formats::Png::Channels::Rgb));
                progress.Set(HdProgress::Stage::Export, ++exported, frames);
            }
        });
        cancel.ThrowIfCancellationRequested();

        const auto factor = RealEsrgan::Factor(options.Upscaler(), options.Scale);
        progress.Set(HdProgress::Stage::Upscale, 0, frames);
        tool.Run(input, output, options.Upscaler(), factor, [&](int done) { progress.Set(HdProgress::Stage::Upscale, done, frames); }, cancel);

        std::atomic<long long> saved{0};
        std::vector<HdManifest::Entry> entries(todo.size());
        Parallel::For(0, static_cast<int>(todo.size()), [&](int i)
        {
            cancel.ThrowIfCancellationRequested();
            entries[i] = Save(todo[i], output, folder, factor, options.Scale);
            progress.Set(HdProgress::Stage::Save, ++saved, std::ssize(todo));
        });
        cancel.ThrowIfCancellationRequested();

        for (const auto& entry : entries)
        {
            manifest.Entries[entry.Key] = entry;
        }
    }

    HdManifest::Entry HdGenerator::Save(const HdSource& source, const std::string& output, const std::string& folder, int factor,
        int scale)
    {
        const auto& t = *source.Texture;
        const auto frameBytes = t.Width * scale * t.Height * scale * Channels;
        std::vector<byte> image(static_cast<size_t>(frameBytes * t.FrameCount));
        for (int f = 0; f < t.FrameCount; f++)
        {
            const auto upscaled = Formats::Png::Decode(File::ReadAllBytes(Path::Combine(output, FrameFile(source, f))));
            const auto [width, height] = UpscaleImages::Padded(t.Width, t.Height, Margin);
            if (upscaled.Width != width * factor || upscaled.Height != height * factor)
            {
                throw InvalidDataException(std::format("{}: the upscaler made {} x {}", source.Name, upscaled.Width, upscaled.Height));
            }

            const auto frame = UpscaleImages::Output(upscaled.Rgba, factor,
                TextureExport::Frame(t, *source.Palette, f, source.ZeroOpaque), t.Width, t.Height,
                Margin, scale, source.Alpha);
            std::copy(frame.begin(), frame.end(), image.begin() + static_cast<std::ptrdiff_t>(f) * frameBytes);
        }

        const auto file = FileOf(source.Level, source.Name, source.Key, source.Kind);
        const auto path = Path::Combine(folder, file);
        Directory::CreateDirectory(Path::GetDirectoryName(path));
        const auto opaque = source.ZeroOpaque || std::find(t.Indices.begin(), t.Indices.end(), static_cast<byte>(0)) == t.Indices.end();
        File::WriteAllBytes(path, Formats::Png::Encode(t.Width * scale, t.Height * scale * t.FrameCount, image,
            opaque ? Formats::Png::Channels::Rgb : Formats::Png::Channels::Rgba));
        return HdManifest::Entry{source.Key, source.Level, source.Name, t.Width, t.Height, t.FrameCount, file};
    }

    std::string HdGenerator::SafeName(const std::string& name)
    {
        std::string safe;
        for (const auto c : name)
        {
            const auto letterOrDigit = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            safe += letterOrDigit || c == '_' || c == '-' ? c : '_';
        }

        return safe;
    }

    long long HdGenerator::FolderBytes(const std::string& folder)
    {
        long long bytes = 0;
        for (const auto& f : Directory::GetFiles(folder, "*", Directory::Search::AllDirectories))
        {
            bytes += File::Length(f);
        }

        return bytes;
    }
}
