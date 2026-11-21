#pragma once

#include <memory>
#include <optional>
#include <string>

#include "core/Dictionary.h"

namespace Mdk::Game::HdTextures
{
    // The HD cache's index (manifest.txt): how it was made, then one line per image.
    //   format=1
    //   model=realesrgan-x4plus
    //   scale=2
    //   key level name width height frames file      (the source's size; the image is scale × it,
    //   9f3a... 7 B_WALL 64 64 1 LEVEL7/B_WALL_9f3a....png   frames stacked downwards)
    class HdManifest
    {
    public:
        // The layout of the cache; another one is ignored (made again).
        static constexpr int Format = 1;
        static constexpr const char* FileName = "manifest.txt";

        struct Entry
        {
            std::string Key;
            int Level = 0;
            std::string Name;
            int Width = 0;
            int Height = 0;
            int Frames = 0;
            std::string File;

            bool operator==(const Entry&) const = default;
        };

        HdManifest(std::string model, int scale) : _model(std::move(model)), _scale(scale) {}

        int Version = Format;

        // The upscaler's model and the images' scale.
        const std::string& Model() const { return _model; }
        int Scale() const { return _scale; }

        Dictionary<std::string, Entry> Entries;

        // The manifest of a cache folder, or null (none, unreadable).
        static std::shared_ptr<HdManifest> Load(const std::string& folder);

        // Writes the manifest (through a temporary file: an interrupted run leaves the last one).
        void Save(const std::string& folder) const;

    private:
        static constexpr int Fields = 7;

        std::string _model;
        int _scale;

        static std::optional<Entry> Parse(const std::string& line);
    };
}
