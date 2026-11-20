#include "Mdk.Engine/Render/Renderer.h"

#include "Mdk.Engine/Render/ColourMips.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace Mdk::Engine::Render
{
    // The enhanced look's colour textures: each index texture seen through a palette,
    // expanded to RGBA8 with all its mips (ColourMips), one array layer per frame, and
    // sampled trilinear and anisotropic. Made when the game prepares its materials (Prepare,
    // at load), else before the frame that first needs them; rewritten in place when their indices or
    // palette change (bullet holes, fading palettes). The original look and the canvas keep the index
    // textures.
    //   index texture ─┐  (CPU copies)
    //   palette ───────┴─► Expand ─► Chain (scratch) ─► staging ─► 2D array (layer = frame) + mips ─► enhanced.hlsl t3

    namespace
    {
        // Parallel.For(first, last, body): the indices shared out to a thread per core.
        void ParallelFor(int first, int last, const std::function<void(int)>& body)
        {
            const auto cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
            const auto workers = std::min(cores, last - first);
            std::atomic<int> next{first};
            const auto work = [&]
            {
                for (auto i = next++; i < last; i = next++)
                {
                    body(i);
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
        }
    }

    void Renderer::CreateColourSampler()
    {
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = SDL_GPU_FILTER_LINEAR;
        info.mag_filter = SDL_GPU_FILTER_LINEAR;
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        info.max_lod = AllMips;
        info.enable_anisotropy = true;
        info.max_anisotropy = MaxAnisotropy;
        _colourSampler = SDL_CreateGPUSampler(_device, &info);
        Check(_colourSampler != nullptr, "SDL_CreateGPUSampler");
        _blankColours = CreateColours(1, 1, 1, 1);
        const std::array<byte, BytesPerPixel> blank{};
        Upload(_blankColours, 1, 1, blank);
    }

    void Renderer::Prepare(const Material& material)
    {
        if (!NeedsColours(material) || !CanExpand(KeyOf(material)) || _colours.contains(KeyOf(material)))
        {
            return;
        }

        // Made together in the next frame's copy pass, before it draws.
        _missing.insert(KeyOf(material));
    }

    void Renderer::Replace(const Material& material, int width, int height, std::vector<byte> rgba)
    {
        const auto key = KeyOf(material);
        _replaced[key] = Pixels{width, height * key.Frames, std::move(rgba)};
        if (_colours.contains(key))
        {
            _stale.push_back(key);
            ReleaseStale();
        }
    }

    void Renderer::ReplaceImage(int texture, int palette, int width, int height, std::vector<byte> rgba)
    {
        const ColourKey key{texture, palette, 1};
        _images.insert(key);
        Replace(Material(texture, palette, Vector4::One(), 1, Render::Pass::Overlay), width, height, std::move(rgba));
    }

    long long Renderer::ColourBytes() const
    {
        long long bytes = 0;
        const auto add = [&](ColourKey key) { bytes += CanExpand(key) ? ColourSize(key) : 0; };
        for (const auto& [key, texture] : _colours)
        {
            add(key);
        }

        for (const auto& key : _missing)
        {
            add(key);
        }

        return bytes;
    }

    bool Renderer::NeedsColours(const Render::Material& material)
    {
        return material.Texture != Material::None && material.Shading != Shading::Original
            && material.Pass != Render::Pass::Overlay && material.Pass != Render::Pass::Mirror;
    }

    void Renderer::KeepIndices(int texture, int width, int height, std::span<const byte> indices)
    {
        auto& kept = _indexPixels[texture];
        kept.Width = width;
        kept.Height = height;
        kept.Data.assign(indices.begin(), indices.end());
    }

    void Renderer::KeepPalette(int palette, std::span<const byte> rgba)
    {
        _palettePixels[palette].assign(rgba.begin(), rgba.end());
    }

    void Renderer::Changed(int texture, int width, int height, std::span<const byte> pixels)
    {
        if (_indexPixels.contains(texture))
        {
            KeepIndices(texture, width, height, pixels);
        }
        else if (_palettePixels.contains(texture))
        {
            KeepPalette(texture, pixels);
        }
        else
        {
            return;
        }

        // An HD texture no longer shows the texture: expanded again from now on (canvas images stay).
        for (auto replaced = _replaced.begin(); replaced != _replaced.end();)
        {
            const auto& key = replaced->first;
            if ((key.Texture == texture || key.Palette == texture) && !_images.contains(key))
            {
                replaced = _replaced.erase(replaced);
                continue;
            }

            ++replaced;
        }

        for (const auto& [key, colours] : _colours)
        {
            if ((key.Texture != texture && key.Palette != texture) || _images.contains(key))
            {
                continue;
            }

            // A texture of another size is made again before the next frame that draws it.
            if (!CanExpand(key) || !SameSize(key, colours))
            {
                _stale.push_back(key);
                continue;
            }

            _batch.emplace_back(key, colours);
        }

        if (!_batch.empty())
        {
            auto* commands = SDL_AcquireGPUCommandBuffer(_device);
            auto* copy = SDL_BeginGPUCopyPass(commands);
            UploadBatch(copy);
            SDL_EndGPUCopyPass(copy);
            SDL_SubmitGPUCommandBuffer(commands);
            _batch.clear();
        }

        ReleaseStale();
    }

    bool Renderer::SameSize(ColourKey key, SDL_GPUTexture* colours) const
    {
        const auto size = _colourSizes.find(colours);
        return size != _colourSizes.end() && size->second == SizeOf(key);
    }

    Renderer::Size Renderer::SizeOf(ColourKey key) const
    {
        if (const auto replaced = _replaced.find(key); replaced != _replaced.end())
        {
            return {replaced->second.Width, replaced->second.Height / key.Frames};
        }

        const auto& indices = _indexPixels.at(key.Texture);
        return {indices.Width, indices.Height / key.Frames};
    }

    void Renderer::ReleaseStale()
    {
        for (const auto& key : _stale)
        {
            auto* texture = _colours.at(key);
            SDL_ReleaseGPUTexture(_device, texture);
            _colourSizes.erase(texture);
            _colours.erase(key);
        }

        _stale.clear();
    }

    void Renderer::ReleaseColoursFrom(int first)
    {
        const auto from = [first](ColourKey key) { return key.Texture >= first || key.Palette >= first; };
        for (const auto& [key, texture] : _colours)
        {
            if (from(key))
            {
                _stale.push_back(key);
            }
        }

        ReleaseStale();
        std::erase_if(_replaced, [&](const auto& entry) { return from(entry.first); });
        std::erase_if(_images, from);
        std::erase_if(_indexPixels, [first](const auto& entry) { return entry.first >= first; });
        std::erase_if(_palettePixels, [first](const auto& entry) { return entry.first >= first; });
    }

    Renderer::ColourKey Renderer::KeyOf(const Render::Material& material)
    {
        return {material.Texture, material.Palette, Math::Max(material.FrameCount, 1)};
    }

    bool Renderer::CanExpand(ColourKey key) const
    {
        if (_replaced.contains(key))
        {
            return true;
        }

        const auto indices = _indexPixels.find(key.Texture);
        return _palettePixels.contains(key.Palette) && indices != _indexPixels.end() && indices->second.Height % key.Frames == 0;
    }

    void Renderer::PrepareColours(SDL_GPUCommandBuffer* commands)
    {
        FindMissing(_commands);
        for (const auto* inset : _insets)
        {
            FindMissing(inset->Commands);
        }

        if (_missing.empty())
        {
            return;
        }

        for (const auto& key : _missing)
        {
            _batch.emplace_back(key, nullptr);
        }

        auto* copy = SDL_BeginGPUCopyPass(commands);
        UploadBatch(copy);
        SDL_EndGPUCopyPass(copy);
        for (const auto& [key, texture] : _batch)
        {
            _colours[key] = texture;
        }

        _batch.clear();
        _missing.clear();
    }

    void Renderer::FindMissing(const std::vector<DrawCommand>& commands)
    {
        for (const auto& command : commands)
        {
            if (command.Material.Texture == Material::None)
            {
                continue;
            }

            const auto mode = ModeOf(command);
            if (mode != Mode::Lit && mode != Mode::Sprite && mode != Mode::Image)
            {
                continue;
            }

            const auto key = KeyOf(command.Material);
            if (!_colours.contains(key) && CanExpand(key))
            {
                _missing.insert(key);
            }
        }
    }

    int Renderer::ColourSize(ColourKey key) const
    {
        const auto [width, height] = SizeOf(key);
        return ColourMips::ChainSize(width, height) * key.Frames;
    }

    void Renderer::UploadBatch(SDL_GPUCopyPass* copy)
    {
        auto total = 0;
        for (const auto& [key, texture] : _batch)
        {
            total += ColourSize(key);
        }

        // Built in CPU memory (the upload buffer is slow to read back: each mip reads the last), in
        // chunks of the kept scratch, a chunk's textures on every core; then copied to the upload buffer.
        SDL_GPUTransferBuffer* transfer = nullptr;
        auto temporary = false;
        auto mapped = MapStaging(total, transfer, temporary);
        auto first = 0;
        auto at = 0;
        const auto count = static_cast<int>(_batch.size());
        while (first < count)
        {
            auto last = first;
            auto size = 0;
            while (last < count && (last == first || size + ColourSize(_batch[last].first) <= ChainChunk))
            {
                size += ColourSize(_batch[last++].first);
            }

            if (static_cast<int>(_chain.size()) < size)
            {
                _chain.assign(size, 0);
            }

            ExpandChunk(first, last);
            std::copy_n(_chain.begin(), size, mapped.begin() + at);
            first = last;
            at += size;
        }

        SDL_UnmapGPUTransferBuffer(_device, transfer);
        auto offset = 0;
        for (auto& [key, texture] : _batch)
        {
            if (texture == nullptr)
            {
                texture = CreateColours(key);
            }

            offset = UploadLayers(copy, key, texture, transfer, offset);
        }

        if (temporary)
        {
            SDL_ReleaseGPUTransferBuffer(_device, transfer);
        }

        // Given textures are on the GPU: only their size is still needed (a change expands the original).
        for (const auto& [key, texture] : _batch)
        {
            if (const auto replaced = _replaced.find(key); replaced != _replaced.end() && !replaced->second.Data.empty())
            {
                replaced->second.Data = {};
            }
        }
    }

    void Renderer::ExpandChunk(int first, int last)
    {
        if (last - first == 1)
        {
            Expand(_batch[first].first, std::span<byte>(_chain).first(ColourSize(_batch[first].first)));
            return;
        }

        std::vector<int> offsets(last - first + 1);
        for (int i = first; i < last; i++)
        {
            offsets[i - first + 1] = offsets[i - first] + ColourSize(_batch[i].first);
        }

        const std::span<byte> chain(_chain);
        ParallelFor(first, last, [&](int i)
        {
            Expand(_batch[i].first, chain.subspan(offsets[i - first], offsets[i - first + 1] - offsets[i - first]));
        });
    }

    void Renderer::Expand(ColourKey key, std::span<byte> target) const
    {
        if (const auto replaced = _replaced.find(key); replaced != _replaced.end())
        {
            Copy(key, replaced->second, target);
            return;
        }

        const auto& indices = _indexPixels.at(key.Texture);
        const auto& palette = _palettePixels.at(key.Palette);
        const auto [width, height] = SizeOf(key);
        const auto frameSize = width * height;
        const auto chainSize = ColourMips::ChainSize(width, height);
        for (int f = 0; f < key.Frames; f++)
        {
            const auto chain = target.subspan(f * chainSize, chainSize);
            ColourMips::Expand(std::span<const byte>(indices.Data).subspan(f * frameSize, frameSize), palette, chain);
            ColourMips::FillChain(chain, width, height);
        }
    }

    void Renderer::Copy(ColourKey key, const Pixels& replaced, std::span<byte> target) const
    {
        const auto [width, height] = SizeOf(key);
        const auto frameSize = ColourMips::LevelSize(width, height);
        const auto chainSize = ColourMips::ChainSize(width, height);
        for (int f = 0; f < key.Frames; f++)
        {
            const auto chain = target.subspan(f * chainSize, chainSize);
            const auto frame = std::span<const byte>(replaced.Data).subspan(f * frameSize, frameSize);
            std::copy(frame.begin(), frame.end(), chain.begin());
            ColourMips::FillChain(chain, width, height);
        }
    }

    int Renderer::UploadLayers(SDL_GPUCopyPass* copy, ColourKey key, SDL_GPUTexture* texture, SDL_GPUTransferBuffer* transfer, int offset)
    {
        const auto [width, height] = SizeOf(key);
        const auto levels = ColourMips::Levels(width, height);
        for (int layer = 0; layer < key.Frames; layer++)
        {
            auto w = width;
            auto h = height;
            for (int mip = 0; mip < levels; mip++)
            {
                SDL_GPUTextureTransferInfo source{};
                source.transfer_buffer = transfer;
                source.offset = static_cast<uint>(offset);
                source.pixels_per_row = static_cast<uint>(w);
                source.rows_per_layer = static_cast<uint>(h);
                SDL_GPUTextureRegion destination{};
                destination.texture = texture;
                destination.mip_level = static_cast<uint>(mip);
                destination.layer = static_cast<uint>(layer);
                destination.w = static_cast<uint>(w);
                destination.h = static_cast<uint>(h);
                destination.d = 1;
                SDL_UploadToGPUTexture(copy, &source, &destination, false);
                offset += ColourMips::LevelSize(w, h);
                w = ColourMips::Half(w);
                h = ColourMips::Half(h);
            }
        }

        return offset;
    }

    SDL_GPUTexture* Renderer::CreateColours(ColourKey key)
    {
        const auto [width, height] = SizeOf(key);
        auto* texture = CreateColours(width, height, key.Frames, ColourMips::Levels(width, height));
        _colourSizes[texture] = {width, height};
        return texture;
    }

    SDL_GPUTexture* Renderer::CreateColours(int width, int height, int layers, int levels)
    {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
        info.format = ColourFormat;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = static_cast<uint>(width);
        info.height = static_cast<uint>(height);
        info.layer_count_or_depth = static_cast<uint>(layers);
        info.num_levels = static_cast<uint>(levels);
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        auto* texture = SDL_CreateGPUTexture(_device, &info);
        Check(texture != nullptr, "SDL_CreateGPUTexture");
        return texture;
    }

    SDL_GPUTexture* Renderer::ColoursOf(const Render::Material& material) const
    {
        const auto texture = _colours.find(KeyOf(material));
        return texture != _colours.end() ? texture->second : _blankColours;
    }

    void Renderer::DisposeColours()
    {
        for (const auto& [key, texture] : _colours)
        {
            _stale.push_back(key);
        }

        ReleaseStale();
        SDL_ReleaseGPUTexture(_device, _blankColours);
        SDL_ReleaseGPUSampler(_device, _colourSampler);
    }
}
