#include "formats/Mve.h"

#include <algorithm>
#include <stdexcept>

#include "core/Files.h"

namespace Mdk::Formats
{
    const std::array<short, 256> Mve::Deltas = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
        32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 47, 51, 56, 61,
        66, 72, 79, 86, 94, 102, 112, 122, 133, 145, 158, 173, 189, 206, 225, 245,
        267, 292, 318, 348, 379, 414, 452, 493, 538, 587, 640, 699, 763, 832, 908, 991,
        1081, 1180, 1288, 1405, 1534, 1673, 1826, 1993, 2175, 2373, 2590, 2826, 3084, 3365, 3672, 4008,
        4373, 4772, 5208, 5683, 6202, 6767, 7385, 8059, 8794, 9597, 10472, 11428, 12471, 13609, 14851, 16206,
        17685, 19298, 21060, 22981, 25078, 27367, 29864, 32589, -29973, -26728, -23186, -19322, -15105, -10503, -5481, -1,
        1, 1, 5481, 10503, 15105, 19322, 23186, 26728, 29973, -32589, -29864, -27367, -25078, -22981, -21060, -19298,
        -17685, -16206, -14851, -13609, -12471, -11428, -10472, -9597, -8794, -8059, -7385, -6767, -6202, -5683, -5208, -4772,
        -4373, -4008, -3672, -3365, -3084, -2826, -2590, -2373, -2175, -1993, -1826, -1673, -1534, -1405, -1288, -1180,
        -1081, -991, -908, -832, -763, -699, -640, -587, -538, -493, -452, -414, -379, -348, -318, -292,
        -267, -245, -225, -206, -189, -173, -158, -145, -133, -122, -112, -102, -94, -86, -79, -72,
        -66, -61, -56, -51, -47, -43, -42, -41, -40, -39, -38, -37, -36, -35, -34, -33,
        -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17,
        -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1,
    };

    std::shared_ptr<Mve> Mve::Load(const std::string& path)
    {
        auto bytes = File::ReadAllBytes(path);
        if (bytes.size() < SignatureSize || Bin::Ascii(bytes, 0, static_cast<int>(Signature.size())) != Signature)
        {
            return nullptr;
        }

        return std::shared_ptr<Mve>(new Mve(std::move(bytes)));
    }

    bool Mve::NextFrame()
    {
        Audio.clear();
        _shown = false;
        auto length = static_cast<int>(_bytes.size());
        while (!_ended && !_shown && _pos + ChunkHeader <= length)
        {
            int size = Bin::U16(_bytes, _pos);
            auto end = _pos + ChunkHeader + size;
            _pos += ChunkHeader;
            while (_pos + OpcodeHeader <= end)
            {
                int opcodeLength = Bin::U16(_bytes, _pos);
                auto type = static_cast<Opcode>(_bytes.at(_pos + 2));
                Run(type, _bytes.at(_pos + 3), _pos + OpcodeHeader, opcodeLength);
                _pos += OpcodeHeader + opcodeLength;
                _ended |= type == Opcode::EndOfStream;
            }

            _pos = end;
        }

        if (_shown)
        {
            _frame++;
        }

        return _shown;
    }

    void Mve::Run(Opcode type, int version, int p, int length)
    {
        switch (type)
        {
            case Opcode::CreateTimer:
                _frameTime = static_cast<int>(Bin::U32(_bytes, p)) * Bin::U16(_bytes, p + 4);
                break;
            case Opcode::InitAudio:
                _audioFlags = Bin::U16(_bytes, p + 2);
                if (version == 0)
                {
                    _audioFlags &= ~AudioCompressed;
                }

                _sampleRate = Bin::U16(_bytes, p + 4);
                _channels = (_audioFlags & AudioStereo) != 0 ? 2 : 1;
                break;
            case Opcode::InitVideo:
                InitVideo(Bin::U16(_bytes, p) * Block, Bin::U16(_bytes, p + 2) * Block);
                break;
            case Opcode::SetPalette:
                SetPalette(p);
                break;
            case Opcode::DecodingMap:
                _map = Bin::Slice(_bytes, p, length);
                break;
            case Opcode::VideoData:
                DecodeVideo(p + VideoHeader, p + length);
                break;
            case Opcode::SendBuffer:
                std::swap(_last, _current);
                _shown = true;
                break;
            case Opcode::AudioFrame:
                if ((Bin::U16(_bytes, p + 2) & StreamMask) != 0)
                {
                    DecodeAudio(p + 6, p + length, Bin::U16(_bytes, p + 4));
                }

                break;
            case Opcode::SilenceFrame:
                if ((Bin::U16(_bytes, p + 2) & StreamMask) != 0)
                {
                    Audio.insert(Audio.end(), Bin::U16(_bytes, p + 4) / (2 * _channels), Vector2::Zero());
                }

                break;
            default:
                break;
        }
    }

    void Mve::InitVideo(int width, int height)
    {
        _width = width;
        _height = height;
        _current.assign(static_cast<size_t>(width) * height, 0);
        _last.assign(static_cast<size_t>(width) * height, 0);
    }

    void Mve::SetPalette(int p)
    {
        int start = Bin::U16(_bytes, p);
        int count = Bin::U16(_bytes, p + 2);
        p += 4;
        for (auto i = 0; i < count * 3; i++)
        {
            auto index = start * 3 + i;
            if (index >= PaletteBytes)
            {
                break;
            }

            auto v = _bytes.at(p + i) & 0x3F;
            Palette[index] = static_cast<byte>((v << 2) | (v >> 4));
        }
    }

    void Mve::DecodeAudio(int p, int end, int size)
    {
        if ((_audioFlags & AudioCompressed) == 0)
        {
            DecodePcm(p, end);
            return;
        }

        std::vector<int> samples(size / 2);
        auto count = 0;
        for (auto ch = 0; ch < _channels; ch++)
        {
            _predictors[ch] = Bin::S16(_bytes, p);
            samples.at(count++) = _predictors[ch];
            p += 2;
        }

        auto channel = 0;
        while (count < static_cast<int>(samples.size()) && p < end)
        {
            _predictors[channel] = Math::Clamp(_predictors[channel] + Deltas[_bytes.at(p)], -32768, 32767);
            samples[count++] = _predictors[channel];
            p++;
            channel ^= _channels - 1;
        }

        PushSamples(samples, count);
    }

    void Mve::DecodePcm(int p, int end)
    {
        std::vector<int> samples;
        auto step = (_audioFlags & Audio16Bit) != 0 ? 2 : 1;
        for (; p + step <= end; p += step)
        {
            samples.push_back(step == 2 ? Bin::S16(_bytes, p) : (_bytes.at(p) - ByteMiddle) << 8);
        }

        PushSamples(samples, static_cast<int>(samples.size()));
    }

    void Mve::PushSamples(const std::vector<int>& samples, int count)
    {
        if (_channels == 1)
        {
            for (auto i = 0; i < count; i++)
            {
                Audio.push_back(Vector2::One() * static_cast<float>(samples[i]) * SampleScale);
            }

            return;
        }

        for (auto i = 0; i + 1 < count; i += 2)
        {
            Audio.push_back(Vector2(static_cast<float>(samples[i]), static_cast<float>(samples[i + 1])) * SampleScale);
        }
    }

    void Mve::DecodeVideo(int p, int end)
    {
        auto block = 0;
        for (auto by = 0; by < _height; by += Block)
        {
            for (auto bx = 0; bx < _width; bx += Block)
            {
                auto code = (block & 1) != 0 ? _map.at(block >> 1) >> 4 : _map.at(block >> 1) & 0x0F;
                block++;
                p = DecodeBlock(code, bx, by, p);
                if (p > end)
                {
                    return;
                }
            }
        }
    }

    int Mve::DecodeBlock(int code, int x, int y, int p)
    {
        auto o = y * _width + x;
        switch (code)
        {
            case 0x0:
                Copy(_last, o, 0, 0);
                return p;
            case 0x1:
                return p;
            case 0x2:
            {
                int b = _bytes.at(p);
                if (b < 56)
                {
                    Copy(_current, o, 8 + b % 7, b / 7);
                }
                else
                {
                    Copy(_current, o, -14 + (b - 56) % 29, 8 + (b - 56) / 29);
                }

                return p + 1;
            }
            case 0x3:
            {
                int b = _bytes.at(p);
                if (b < 56)
                {
                    Copy(_current, o, -(8 + b % 7), -(b / 7));
                }
                else
                {
                    Copy(_current, o, -(-14 + (b - 56) % 29), -(8 + (b - 56) / 29));
                }

                return p + 1;
            }
            case 0x4:
            {
                int b = _bytes.at(p);
                Copy(_last, o, -8 + (b & 0x0F), -8 + (b >> 4));
                return p + 1;
            }
            case 0x5:
                Copy(_last, o, static_cast<sbyte>(_bytes.at(p)), static_cast<sbyte>(_bytes.at(p + 1)));
                return p + 2;
            case 0x7:
                return TwoColours(o, p);
            case 0x8:
                return TwoColourQuadrants(o, p);
            case 0x9:
                return FourColours(o, p);
            case 0xA:
                return FourColourQuadrants(o, p);
            case 0xB:
                for (auto row = 0; row < Block; row++)
                {
                    auto source = Bin::Slice(_bytes, p + row * Block, Block);
                    auto at = o + row * _width;
                    if (at + Block > static_cast<int>(_current.size()))
                    {
                        throw std::out_of_range("Destination is too short.");
                    }

                    std::copy(source.begin(), source.end(), _current.begin() + at);
                }

                return p + Block * Block;
            case 0xC:
                for (auto i = 0; i < 16; i++)
                {
                    Fill(o + (i >> 2) * 2 * _width + (i & 3) * 2, 2, 2, _bytes.at(p + i));
                }

                return p + 16;
            case 0xD:
                for (auto i = 0; i < 4; i++)
                {
                    Fill(o + (i >> 1) * 4 * _width + (i & 1) * 4, 4, 4, _bytes.at(p + i));
                }

                return p + 4;
            case 0xE:
                Fill(o, Block, Block, _bytes.at(p));
                return p + 1;
            case 0xF:
                for (auto row = 0; row < Block; row++)
                {
                    for (auto col = 0; col < Block; col++)
                    {
                        _current.at(o + row * _width + col) = _bytes.at(p + ((row + col) & 1));
                    }
                }

                return p + 2;
        }

        return p;
    }

    void Mve::Copy(const std::vector<byte>& source, int o, int dx, int dy)
    {
        auto from = o + dy * _width + dx;
        if (from < 0 || from + 7 * _width + Block > static_cast<int>(source.size()))
        {
            return;
        }

        for (auto row = 0; row < Block; row++)
        {
            // Overlapping copies within the back buffer go pixel by pixel, as the original.
            for (auto col = 0; col < Block; col++)
            {
                _current.at(o + row * _width + col) = source[from + row * _width + col];
            }
        }
    }

    void Mve::Fill(int o, int w, int h, byte value)
    {
        for (auto row = 0; row < h; row++)
        {
            auto at = o + row * _width;
            if (at < 0 || at + w > static_cast<int>(_current.size()))
            {
                throw std::out_of_range("Specified argument was out of the range of valid values.");
            }

            std::fill_n(_current.begin() + at, w, value);
        }
    }

    int Mve::TwoColours(int o, int p)
    {
        auto p0 = _bytes.at(p);
        auto p1 = _bytes.at(p + 1);
        p += 2;
        if (p0 <= p1)
        {
            for (auto row = 0; row < Block; row++)
            {
                int flags = _bytes.at(p + row);
                for (auto col = 0; col < Block; col++)
                {
                    _current.at(o + row * _width + col) = ((flags >> col) & 1) != 0 ? p1 : p0;
                }
            }

            return p + Block;
        }

        int bits = Bin::U16(_bytes, p);
        for (auto i = 0; i < 16; i++)
        {
            Fill(o + (i >> 2) * 2 * _width + (i & 3) * 2, 2, 2, ((bits >> i) & 1) != 0 ? p1 : p0);
        }

        return p + 2;
    }

    int Mve::TwoColourQuadrants(int o, int p)
    {
        std::array<byte, 2> colours = {_bytes.at(p), _bytes.at(p + 1)};
        if (colours[0] <= colours[1])
        {
            for (auto q = 0; q < 4; q++)
            {
                colours = {_bytes.at(p), _bytes.at(p + 1)};
                int bits16 = Bin::U16(_bytes, p + 2);
                p += 4;
                auto qo = o + (q & 1) * 4 * _width + (q >> 1) * 4;
                for (auto i = 0; i < 16; i++)
                {
                    _current.at(qo + (i >> 2) * _width + (i & 3)) = colours[(bits16 >> i) & 1];
                }
            }

            return p;
        }

        auto bits = Bin::U32(_bytes, p + 2);
        std::array<byte, 2> other = {_bytes.at(p + 6), _bytes.at(p + 7)};
        p += 8;
        auto sideBySide = other[0] <= other[1];
        for (auto half = 0; half < 2; half++)
        {
            if (half == 1)
            {
                colours = other;
                bits = Bin::U32(_bytes, p);
                p += 4;
            }

            for (auto i = 0; i < 32; i++)
            {
                auto c = colours[(bits >> i) & 1];
                auto at = sideBySide ? o + (i >> 2) * _width + half * 4 + (i & 3) : o + (half * 4 + (i >> 3)) * _width + (i & 7);
                _current.at(at) = c;
            }
        }

        return p;
    }

    int Mve::FourColours(int o, int p)
    {
        std::array<byte, 4> colours = {_bytes.at(p), _bytes.at(p + 1), _bytes.at(p + 2), _bytes.at(p + 3)};
        p += 4;
        if (colours[0] <= colours[1])
        {
            if (colours[2] <= colours[3])
            {
                for (auto row = 0; row < Block; row++)
                {
                    int flags = Bin::U16(_bytes, p + row * 2);
                    for (auto col = 0; col < Block; col++)
                    {
                        _current.at(o + row * _width + col) = colours[(flags >> (col * 2)) & 3];
                    }
                }

                return p + 16;
            }

            auto bits = Bin::U32(_bytes, p);
            for (auto i = 0; i < 16; i++)
            {
                Fill(o + (i >> 2) * 2 * _width + (i & 3) * 2, 2, 2, colours[(bits >> (i * 2)) & 3]);
            }

            return p + 4;
        }

        auto all = static_cast<ulong>(Bin::U32(_bytes, p)) | (static_cast<ulong>(Bin::U32(_bytes, p + 4)) << 32);
        for (auto i = 0; i < 32; i++)
        {
            auto c = colours[(all >> (i * 2)) & 3];
            if (colours[2] <= colours[3])
            {
                Fill(o + (i >> 2) * _width + (i & 3) * 2, 2, 1, c);
            }
            else
            {
                Fill(o + (i >> 3) * 2 * _width + (i & 7), 1, 2, c);
            }
        }

        return p + 8;
    }

    int Mve::FourColourQuadrants(int o, int p)
    {
        std::array<byte, 4> colours = {_bytes.at(p), _bytes.at(p + 1), _bytes.at(p + 2), _bytes.at(p + 3)};
        if (colours[0] <= colours[1])
        {
            for (auto q = 0; q < 4; q++)
            {
                colours = {_bytes.at(p), _bytes.at(p + 1), _bytes.at(p + 2), _bytes.at(p + 3)};
                auto bits = Bin::U32(_bytes, p + 4);
                p += 8;
                auto qo = o + (q & 1) * 4 * _width + (q >> 1) * 4;
                for (auto i = 0; i < 16; i++)
                {
                    _current.at(qo + (i >> 2) * _width + (i & 3)) = colours[(bits >> (i * 2)) & 3];
                }
            }

            return p;
        }

        auto all = static_cast<ulong>(Bin::U32(_bytes, p + 4)) | (static_cast<ulong>(Bin::U32(_bytes, p + 8)) << 32);
        std::array<byte, 4> other = {_bytes.at(p + 12), _bytes.at(p + 13), _bytes.at(p + 14), _bytes.at(p + 15)};
        p += 16;
        auto sideBySide = other[0] <= other[1];
        for (auto half = 0; half < 2; half++)
        {
            if (half == 1)
            {
                colours = other;
                all = static_cast<ulong>(Bin::U32(_bytes, p)) | (static_cast<ulong>(Bin::U32(_bytes, p + 4)) << 32);
                p += 8;
            }

            for (auto i = 0; i < 32; i++)
            {
                auto c = colours[(all >> (i * 2)) & 3];
                auto at = sideBySide ? o + (i >> 2) * _width + half * 4 + (i & 3) : o + (half * 4 + (i >> 3)) * _width + (i & 7);
                _current.at(at) = c;
            }
        }

        return p;
    }
}
