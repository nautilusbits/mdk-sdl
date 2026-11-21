#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "core/Cancellation.h"
#include "engine/upscale/RealEsrgan.h"
#include "engine/upscale/UpscalerSource.h"
#include "game/hd_textures/HdManifest.h"
#include "game/hd_textures/TextureExport.h"
#include "game/mods/ModImages.h"
#include "game/mods/ModInfo.h"

namespace Mdk::Formats
{
    class MdkData;
}

namespace Mdk::Game::HdTextures
{
    // The upscaler's network: Real-ESRGAN x4plus (General) or animevideov3 (Anime).
    enum class HdModel { General, Anime };

    // What to make: the levels, the upscaler's model, the images' scale (2 or 4).
    struct HdOptions
    {
        static constexpr std::array<int, 6> AllLevels{3, 4, 5, 6, 7, 8};
        static constexpr std::array<int, 2> Scales{2, 4};

        std::vector<int> Levels;
        HdModel Model{};
        int Scale = 0;
        // Where the upscaler is downloaded from (settings.cfg).
        Engine::Upscale::UpscalerSource Source = Engine::Upscale::UpscalerSource::Default();

        bool operator==(const HdOptions&) const = default;

        static const HdOptions& Default()
        {
            static const HdOptions options{{AllLevels.begin(), AllLevels.end()}, HdModel::General, 2};
            return options;
        }

        // The engine's model.
        Engine::Upscale::UpscaleModel Upscaler() const
        {
            return Model == HdModel::General ? Engine::Upscale::UpscaleModel::General : Engine::Upscale::UpscaleModel::Anime;
        }
    };

    // Where a run is, for a progress screen on another thread.
    class HdProgress
    {
    public:
        enum class Stage { Download, Read, Export, Upscale, Save, Done, Failed, Cancelled };

        void Set(Stage stage, long long done, long long total, const std::string& message = "");

        std::tuple<Stage, long long, long long, std::string> Read();

        // One short line (a menu's): "Upscaling 120/900", "Downloading 12/45 MB", "Done: 311 made".
        std::string Describe();

        bool Finished();

    private:
        // A failure's message is cut to this length (one line on the menu).
        static constexpr int MaxMessage = 24;

        std::mutex _lock;
        Stage _stage{};
        long long _done = 0;
        long long _total = 0;
        std::string _message;
    };

    // Makes the HD textures from the user's game files: the levels' textures and 2D images
    // (TextureExport) through Real-ESRGAN (RealEsrgan), into a mod of the user's folder (ModFolder,
    // priority below other mods: theirs win). Nothing of the game leaves the computer. Images already
    // made for the same texture, palette, model and scale are kept; images of textures no longer
    // exported are deleted. Each image's name holds its key: it replaces exactly that texture through
    // that palette (ModImages).
    //   tools/realesrgan/ ◄── download (once, SHA-256 checked)
    //   mods/hd-textures/work/in/<key>_<frame>.png ─► upscaler ─► work/out/ ─► crop, scale, alpha
    //     ─► mods/hd-textures/textures/LEVELn/<NAME>@<key>.png (images/ for 2D), manifest.txt, mod.txt
    struct HdGenerator
    {
        // The mod's folder in mods/.
        static constexpr const char* ModFolder = "hd-textures";
        // Where older builds kept the HD textures, in the user folder.
        static constexpr const char* OldFolder = "textures-hd";

        // The upscaler's folder in a user folder.
        static std::string ToolIn(const std::string& userFolder);

        // Makes the cache; returns a summary line. Throws on failure (the cache keeps what it had).
        static std::string Run(const std::shared_ptr<Formats::MdkData>& data, const std::string& userFolder,
            const HdOptions& options, HdProgress& progress, const CancellationToken& cancel);

        // The mod's folder in a user folder.
        static std::string FolderIn(const std::string& userFolder);

        // Moves older builds' textures-hd/ into the mod (once; its images keep their keys). Returns
        // how many images moved.
        static int Migrate(const std::string& userFolder);

    private:
        // Below other mods (0 by default).
        static constexpr int ModPriority = -100;
        // Source texels wrapped around each frame for the upscaler (seamless tiling).
        static constexpr int Margin = 8;
        static constexpr const char* ToolsFolder = "tools";
        static constexpr const char* ToolFolder = "realesrgan";
        static constexpr const char* WorkFolder = "work";
        static constexpr int BytesPerMegabyte = 1 << 20;
        static constexpr int Channels = 4;

        // The mod's mod.txt: the upscaler's model and scale.
        static Mods::ModInfo Info(const std::string& model, int scale);

        // An image's file in the mod: its level's folder (the menus' none), its name, its key.
        static std::string FileOf(int level, const std::string& name, const std::string& key,
            Mods::ModImages::Kind kind = Mods::ModImages::Kind::Textures);

        // The cache's manifest when it was made the same way; else the old images go.
        static std::shared_ptr<HdManifest> Current(const std::string& folder, const std::string& model, int scale);

        // Forgets (and deletes) a level's images whose texture isn't exported any more (stale).
        static void Prune(const std::string& folder, HdManifest& manifest, int level, const std::vector<HdSource>& sources);

        // A level's colour textures with their mips, original and HD (the GPU memory they take).
        static std::string Sizes(int level, const std::vector<HdSource>& sources, int scale);

        // Exports, upscales and saves `todo` into the cache.
        static void Make(Engine::Upscale::RealEsrgan& tool, const std::vector<HdSource>& todo, const std::string& work,
            const std::string& folder, HdManifest& manifest, const HdOptions& options, HdProgress& progress,
            const CancellationToken& cancel);

        static std::string FrameFile(const HdSource& source, int frame)
        {
            return source.Key + "_" + std::to_string(frame) + ".png";
        }

        // A texture's upscaled frames finished (UpscaleImages.Output), stacked, saved.
        static HdManifest::Entry Save(const HdSource& source, const std::string& output, const std::string& folder, int factor,
            int scale);

        // A texture's name as a file name (characters a file system refuses become _).
        static std::string SafeName(const std::string& name);

        static long long FolderBytes(const std::string& folder);
    };
}
