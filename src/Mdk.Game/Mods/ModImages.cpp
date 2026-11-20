#include "Mdk.Game/Mods/ModImages.h"

#include <algorithm>
#include <format>
#include <limits>
#include <unordered_set>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/Files.h"
#include "Mdk.Core/IoErrors.h"
#include "Mdk.Core/Parallel.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Game/HdTextures/HdKey.h"

namespace Mdk::Game::Mods
{
    using HdTextures::HdImage;
    using HdTextures::HdKey;

    namespace
    {
        // Enum.GetValues<Kind>().
        constexpr ModImages::Kind AllKinds[] = {ModImages::Kind::Textures, ModImages::Kind::Images};
        constexpr byte Opaque = std::numeric_limits<byte>::max();

        // files.TryGetValue(name, out var file).
        const ModFile* TryGet(const std::map<std::string, ModFile, FolderLess>& files, const std::string& name)
        {
            const auto found = files.find(name);
            return found != files.end() ? &found->second : nullptr;
        }
    }

    std::shared_ptr<ModImages> ModImages::Open(const std::vector<std::shared_ptr<ModFiles>>& mods, std::optional<int> level)
    {
        auto images = std::shared_ptr<ModImages>(new ModImages());
        std::vector<ModFile> levelFiles;
        for (const auto& mod : mods)
        {
            std::unordered_map<Kind, std::shared_ptr<Index>> kinds;
            for (const auto kind : AllKinds)
            {
                kinds[kind] = Scan(mod, FolderOf(kind), level, levelFiles);
            }

            images->_mods.push_back(std::move(kinds));
        }

        images->Decode(levelFiles);
        return images;
    }

    std::shared_ptr<ModImages::Index> ModImages::Scan(const std::shared_ptr<ModFiles>& mod, const std::string& folder,
        std::optional<int> level, std::vector<ModFile>& levelFiles)
    {
        auto index = std::make_shared<Index>();
        const auto levelFolder = level ? std::optional<std::string>(LevelFolder(*level)) : std::nullopt;
        for (const auto& file : mod->List(folder))
        {
            if (!Str::EndsWithIgnoreCase(file, PngExtension))
            {
                continue;
            }

            // "textures/LEVEL3/WALL.png" is in "LEVEL3" ("" for the top).
            const ModFile path{mod, file};
            const auto stem = Path::GetFileNameWithoutExtension(file);
            const auto inFolder = file.substr(folder.size() + 1);
            const auto slash = Str::LastIndexOf(inFolder, Separator);
            const auto directory = slash >= 0 ? inFolder.substr(0, static_cast<size_t>(slash)) : "";
            const auto mark = Str::IndexOf(stem, KeyMark);
            const auto inLevel = levelFolder && Str::EqualsIgnoreCase(directory, *levelFolder);
            if (inLevel)
            {
                levelFiles.push_back(path);
            }

            if (mark >= 0)
            {
                index->Keys.try_emplace(stem.substr(static_cast<size_t>(mark) + 1), path);
            }
            else if (inLevel)
            {
                index->Level.try_emplace(stem, path);
            }
            else if (directory.empty())
            {
                index->Names.try_emplace(stem, path);
            }
        }

        return index;
    }

    void ModImages::Decode(const std::vector<ModFile>& paths)
    {
        std::vector<std::shared_ptr<const Formats::Png::Image>> images(paths.size());
        Parallel::For(0, static_cast<int>(paths.size()), [&](int i) { images[i] = Load(paths[i]); });
        for (size_t i = 0; i < paths.size(); i++)
        {
            _decoded[paths[i]] = std::move(images[i]);
        }
    }

    std::shared_ptr<const Formats::Png::Image> ModImages::Load(const ModFile& path)
    {
        try
        {
            auto image = Formats::Png::Decode(path.Read());
            Premultiply(image.Rgba);
            return std::make_shared<const Formats::Png::Image>(std::move(image));
        }
        catch (const std::exception& e)
        {
            if (!IsReadError(e))
            {
                throw;
            }

            Console::Error().WriteLine(std::format("Mod image {}: {}", path.ToString(), e.what()));
            return nullptr;
        }
    }

    std::shared_ptr<const Formats::Png::Image> ModImages::Decoded(const ModFile& path)
    {
        const auto found = _decoded.find(path);
        if (found != _decoded.end())
        {
            return found->second;
        }

        return _decoded[path] = Load(path);
    }

    void ModImages::Prefetch(const std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>>& textures,
        const Formats::Palette& palette)
    {
        std::unordered_set<ModFile, ModFileHash> paths;
        for (const auto& [name, texture] : textures)
        {
            const auto path = FirstFile(name, *texture, palette);
            if (path && !_decoded.contains(*path))
            {
                paths.insert(*path);
            }
        }

        Decode(std::vector<ModFile>(paths.begin(), paths.end()));
    }

    void ModImages::Trim()
    {
        _decoded.clear();
    }

    std::optional<ModFile> ModImages::FirstFile(const std::string& name, const Formats::Texture& texture, const Formats::Palette& palette)
    {
        std::optional<std::string> key;
        for (const auto& mod : _mods)
        {
            const auto& index = *mod.at(Kind::Textures);
            if (!index.Keys.empty())
            {
                if (!key)
                {
                    key = HdKey::Of(texture, palette);
                }

                if (const auto* exact = TryGet(index.Keys, *key))
                {
                    return *exact;
                }
            }

            const auto* level = TryGet(index.Level, name);
            if (level == nullptr)
            {
                level = TryGet(index.Names, name);
            }

            if (level != nullptr)
            {
                return *level;
            }
        }

        return std::nullopt;
    }

    std::optional<HdImage> ModImages::Find(Kind kind, const std::string& name, const Formats::Texture& texture,
        const Formats::Palette& palette)
    {
        std::optional<std::string> key;
        for (const auto& mod : _mods)
        {
            const auto& index = *mod.at(kind);
            if (!index.Keys.empty())
            {
                if (!key)
                {
                    key = HdKey::Of(texture, palette);
                }

                if (const auto* exact = TryGet(index.Keys, *key))
                {
                    if (auto byKey = Strip(*exact, texture.FrameCount))
                    {
                        return Report(name, std::move(*byKey));
                    }
                }
            }

            if (auto byLevel = Frames(index.Level, name, texture.FrameCount))
            {
                return Report(name, std::move(*byLevel));
            }

            if (auto byName = Frames(index.Names, name, texture.FrameCount))
            {
                return Report(name, std::move(*byName));
            }
        }

        return std::nullopt;
    }

    HdImage ModImages::Report(const std::string& name, HdImage image)
    {
        if (std::find(_found.begin(), _found.end(), name) == _found.end())
        {
            _found.push_back(name);
        }

        return image;
    }

    std::optional<HdImage> ModImages::Frames(const FilesByName& files, const std::string& name, int frames)
    {
        if (const auto* path = TryGet(files, name))
        {
            return Strip(*path, frames);
        }

        if (frames == 1)
        {
            return std::nullopt;
        }

        std::vector<std::shared_ptr<const Formats::Png::Image>> images;
        for (int f = 0; f < frames; f++)
        {
            const auto* frame = TryGet(files, std::format("{}{}{}", name, FrameMark, f));
            auto image = frame != nullptr ? Decoded(*frame) : nullptr;
            if (!image)
            {
                return std::nullopt;
            }

            images.push_back(std::move(image));
        }

        const auto differ = std::any_of(images.begin(), images.end(), [&](const auto& i)
        {
            return i->Width != images[0]->Width || i->Height != images[0]->Height;
        });
        if (differ)
        {
            Console::Error().WriteLine(std::format("Mod image {}: frames of different sizes", name));
            return std::nullopt;
        }

        std::vector<byte> rgba;
        for (const auto& i : images)
        {
            rgba.insert(rgba.end(), i->Rgba.begin(), i->Rgba.end());
        }

        return HdImage{images[0]->Width, images[0]->Height, frames, std::move(rgba)};
    }

    std::optional<HdImage> ModImages::Strip(const ModFile& path, int frames)
    {
        const auto image = Decoded(path);
        if (image == nullptr)
        {
            return std::nullopt;
        }

        if (image->Height % frames != 0)
        {
            Console::Error().WriteLine(std::format("Mod image {}: {} rows aren't {} frames", path.ToString(), image->Height, frames));
            return std::nullopt;
        }

        return HdImage{image->Width, image->Height / frames, frames, image->Rgba};
    }

    void ModImages::Premultiply(std::span<byte> rgba)
    {
        for (size_t i = 0; i < rgba.size(); i += Channels)
        {
            // Opaque pixels keep their colour: (c × 255 + 127) / 255 is c.
            const auto alpha = rgba[i + Alpha];
            if (alpha == Opaque)
            {
                continue;
            }

            for (int c = 0; c < Alpha; c++)
            {
                rgba[i + c] = static_cast<byte>((rgba[i + c] * alpha + Opaque / 2) / Opaque);
            }
        }
    }
}
