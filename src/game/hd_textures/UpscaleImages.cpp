#include "game/hd_textures/UpscaleImages.h"

#include <algorithm>

namespace Mdk::Game::HdTextures
{
    std::vector<byte> UpscaleImages::Input(std::span<const byte> rgba, int width, int height, int margin, bool wrapY)
    {
        const auto bled = Bleed(rgba, width, height);
        const auto [paddedWidth, paddedHeight] = Padded(width, height, margin);
        std::vector<byte> padded(static_cast<size_t>(paddedWidth * paddedHeight * Channels));
        for (int y = 0; y < paddedHeight; y++)
        {
            const auto sy = wrapY ? Wrap(y - margin, height) : std::clamp(y - margin, 0, height - 1);
            for (int x = 0; x < paddedWidth; x++)
            {
                const auto sx = Wrap(x - margin, width);
                std::copy_n(bled.begin() + (sy * width + sx) * Channels, Channels, padded.begin() + (y * paddedWidth + x) * Channels);
            }
        }

        return padded;
    }

    std::vector<byte> UpscaleImages::Bleed(std::span<const byte> rgba, int width, int height)
    {
        std::vector<byte> result(rgba.begin(), rgba.end());
        std::vector<bool> filled(static_cast<size_t>(width * height));
        std::vector<int> pending;
        for (int i = 0; i < std::ssize(filled); i++)
        {
            filled[i] = rgba[i * Channels + Alpha] != 0;
            if (!filled[i])
            {
                pending.push_back(i);
            }
        }

        std::vector<int> ring;
        while (!pending.empty())
        {
            ring.clear();
            for (const auto i : pending)
            {
                if (Mean(result, filled, i % width, i / width, width, height))
                {
                    ring.push_back(i);
                }
            }

            if (ring.empty())
            {
                break;
            }

            for (const auto i : ring)
            {
                filled[i] = true;
            }

            std::erase_if(pending, [&](int i) { return filled[i]; });
        }

        for (int i = 0; i < std::ssize(filled); i++)
        {
            result[i * Channels + Alpha] = Opaque;
        }

        return result;
    }

    bool UpscaleImages::Mean(std::span<byte> rgba, std::vector<bool>& filled, int x, int y, int width, int height)
    {
        int sum[Alpha] = {};
        int count = 0;
        for (const auto& [dx, dy] : Neighbours)
        {
            const auto nx = x + dx;
            const auto ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height || !filled[ny * width + nx])
            {
                continue;
            }

            for (int c = 0; c < Alpha; c++)
            {
                sum[c] += rgba[(ny * width + nx) * Channels + c];
            }

            count++;
        }

        if (count == 0)
        {
            return false;
        }

        for (int c = 0; c < Alpha; c++)
        {
            rgba[(y * width + x) * Channels + c] = static_cast<byte>((sum[c] + count / 2) / count);
        }

        return true;
    }

    std::vector<byte> UpscaleImages::Output(std::span<const byte> upscaled, int factor, std::span<const byte> source, int width,
        int height, int margin, int scale, HdAlpha alpha)
    {
        const auto upscaledWidth = std::get<0>(Padded(width, height, margin)) * factor;
        const auto shrink = factor / scale;
        const auto outWidth = width * scale;
        const auto outHeight = height * scale;
        std::vector<byte> result(static_cast<size_t>(outWidth * outHeight * Channels));
        const auto opaque = AllOpaque(source);
        for (int y = 0; y < outHeight; y++)
        {
            for (int x = 0; x < outWidth; x++)
            {
                const auto target = (y * outWidth + x) * Channels;
                Box(upscaled, upscaledWidth, margin * factor + x * shrink, margin * factor + y * shrink, shrink,
                    std::span<byte>(result).subspan(static_cast<size_t>(target), Alpha));
                result[target + Alpha] = opaque ? Opaque
                    : Cover(source, width, height, (x + 0.5f) / scale - 0.5f, (y + 0.5f) / scale - 0.5f, alpha);
            }
        }

        return result;
    }

    bool UpscaleImages::AllOpaque(std::span<const byte> rgba)
    {
        for (size_t i = Alpha; i < rgba.size(); i += Channels)
        {
            if (rgba[i] != Opaque)
            {
                return false;
            }
        }

        return true;
    }

    void UpscaleImages::Box(std::span<const byte> rgba, int width, int left, int top, int size, std::span<byte> colour)
    {
        int sum[Alpha] = {};
        for (int y = top; y < top + size; y++)
        {
            for (int x = left; x < left + size; x++)
            {
                for (int c = 0; c < Alpha; c++)
                {
                    sum[c] += rgba[(y * width + x) * Channels + c];
                }
            }
        }

        const auto count = size * size;
        for (int c = 0; c < Alpha; c++)
        {
            colour[c] = static_cast<byte>((sum[c] + count / 2) / count);
        }
    }

    byte UpscaleImages::Cover(std::span<const byte> rgba, int width, int height, float x, float y, HdAlpha alpha)
    {
        x = Math::Clamp(x, 0.f, static_cast<float>(width - 1));
        y = Math::Clamp(y, 0.f, static_cast<float>(height - 1));
        const auto x0 = ToInt(x);
        const auto y0 = ToInt(y);
        const auto x1 = std::min(x0 + 1, width - 1);
        const auto y1 = std::min(y0 + 1, height - 1);
        const auto fx = x - x0;
        const auto fy = y - y0;
        const auto at = [&](int ax, int ay) { return static_cast<float>(rgba[(ay * width + ax) * Channels + Alpha]); };
        const auto top = MathF::Lerp(at(x0, y0), at(x1, y0), fx);
        const auto bottom = MathF::Lerp(at(x0, y1), at(x1, y1), fx);
        const auto cover = MathF::Lerp(top, bottom, fy);
        if (alpha == HdAlpha::Soft)
        {
            return static_cast<byte>(MathF::Round(cover));
        }

        return cover >= HalfCover ? Opaque : static_cast<byte>(0);
    }
}
