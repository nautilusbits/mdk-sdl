#include "Mdk.Formats/SpriteAnimation.h"

#include <algorithm>

namespace Mdk::Formats
{
    SpriteAnimation::SpriteAnimation(const std::string& name, SharedBytes bytes, int offset)
        : _bytes(std::move(bytes)), _base(offset), _name(name)
    {
        _frameCount = static_cast<int>(Bin::U32(*_bytes, offset));
    }

    std::shared_ptr<SpriteAnimation> SpriteAnimation::Parse(const std::string& name, SharedBytes bytes, int offset)
    {
        return std::shared_ptr<SpriteAnimation>(new SpriteAnimation(name, std::move(bytes), offset));
    }

    SpriteAnimation::Frame SpriteAnimation::GetFrame(int index)
    {
        auto found = _frames.find(index);
        if (found != _frames.end())
        {
            return found->second;
        }

        return _frames[index] = Decode(_base + static_cast<int>(Bin::U32(*_bytes, _base + 4 + index * 4)));
    }

    SpriteAnimation::Frame SpriteAnimation::Decode(int offset)
    {
        auto& bytes = *_bytes;
        int width = Bin::U16(bytes, offset);
        int height = Bin::U16(bytes, offset + 2);
        std::vector<byte> indices(static_cast<size_t>(width) * height);
        auto p = offset + 8;
        for (auto y = 0; y < height; y++)
        {
            auto i = y * width;
            while (true)
            {
                auto command = bytes.at(p++);
                if (command >= EndOfRow)
                {
                    break;
                }

                if (command <= LiteralLast)
                {
                    auto source = Bin::Slice(bytes, p, command + 1);
                    if (i + command + 1 > static_cast<int>(indices.size()))
                    {
                        throw std::out_of_range("Destination is too short.");
                    }

                    std::copy(source.begin(), source.end(), indices.begin() + i);
                    i += command + 1;
                    p += command + 1;
                    continue;
                }

                auto value = bytes.at(p++);
                if (i + command - RepeatBias > static_cast<int>(indices.size()))
                {
                    throw std::out_of_range("Specified argument was out of the range of valid values.");
                }

                std::fill_n(indices.begin() + i, command - RepeatBias, value);
                i += command - RepeatBias;
            }
        }

        auto image = std::make_shared<Texture>();
        image->Name = _name;
        image->Width = width;
        image->Height = height;
        image->Indices = std::move(indices);
        return Frame{image, Bin::S16(bytes, offset + 4), Bin::S16(bytes, offset + 6)};
    }
}
