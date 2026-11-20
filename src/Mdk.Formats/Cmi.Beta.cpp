#include <cstring>

#include "Mdk.Formats/Cmi.h"

namespace Mdk::Formats
{
    namespace
    {
        // BinaryWriter.Write(int / float): little-endian.
        template <typename T>
        void Write(std::vector<byte>& bytes, size_t& at, T value)
        {
            std::memcpy(bytes.data() + at, &value, sizeof(T));
            at += sizeof(T);
        }
    }

    std::shared_ptr<Cmi> Cmi::ParseBeta(SharedBytes bytes) { return std::shared_ptr<Cmi>(new Cmi(std::move(bytes), Scripts::ScriptDialect::Beta1996)); }

    std::shared_ptr<ModelAnimation> Cmi::GetBetaAnimation(int offset)
    {
        auto& animation = _betaAnimations[offset];
        if (!animation)
        {
            animation = ModelAnimation::ParseBeta(std::format("@{:x}", offset), _bytes, offset);
        }

        return animation;
    }

    void Cmi::ParseBeta()
    {
        BinReader r(*_bytes, BetaDirectoryOffset);
        for (auto* directory : {&AlienScripts, &ModelOffsets, &ObjectScripts, &ArenaScripts})
        {
            auto count = r.U32();
            for (uint i = 0; i < count; i++)
            {
                auto name = Str::ToUpper(r.Name(r.U8()));
                auto offset = Absolute(r.U32());
                (*directory)[name] = offset;
                if (offset != 0 && (directory == &AlienScripts || directory == &ObjectScripts))
                {
                    _entries.push_back(offset);
                }
            }
        }

        std::vector<std::string> arenas;
        for (auto& [arena, offset] : ArenaScripts)
        {
            arenas.push_back(arena);
        }

        for (auto& arena : arenas)
        {
            BinReader record(*_bytes, ArenaScripts[arena]);
            ArenaMusic[arena] = Str::ToUpper(record.Name(record.U8()));
            ArenaAmbience[arena] = Str::ToUpper(record.Name(record.U8()));
            ArenaScripts[arena] = 0;
        }

        ConvertPaths();
    }

    void Cmi::ConvertPaths()
    {
        auto paths = Scripts::ScriptDecoder::BetaPaths(*this);
        std::vector<byte> bytes(*_bytes);
        for (auto path : paths)
        {
            BetaPaths[path] = static_cast<int>(bytes.size());
            auto spline = SplineOf(*_bytes, path);
            bytes.insert(bytes.end(), spline.begin(), spline.end());
        }

        _bytes = Share(std::move(bytes));
    }

    std::vector<byte> Cmi::SplineOf(std::span<const byte> source, int path)
    {
        auto count = static_cast<int>(Bin::U32(source, path));
        std::vector<Vector3> positions;
        auto position = Vector3::Zero();
        for (auto i = 0; i < count; i++)
        {
            auto p = path + PathCountSize + i * PathStepSize;
            auto step = Vector3(Bin::F32(source, p), Bin::F32(source, p + 4), Bin::F32(source, p + 8));
            position = i == 0 ? step : position + step;
            positions.push_back(position);
        }

        if (positions.empty())
        {
            positions.push_back(Vector3::Zero());
        }

        positions.push_back(positions[0]);
        std::vector<byte> record(PathCountSize + positions.size() * PathKeySize);
        size_t at = 0;
        Write(record, at, static_cast<int>(positions.size()));
        auto last = static_cast<int>(positions.size()) - 1;
        for (auto i = 0; i <= last; i++)
        {
            auto incoming = positions[i] - positions[Math::Max(i - 1, 0)];
            auto outgoing = positions[Math::Min(i + 1, last)] - positions[i];
            Write(record, at, i);
            for (auto v : {positions[i], incoming, outgoing})
            {
                Write(record, at, v.X);
                Write(record, at, v.Y);
                Write(record, at, v.Z);
            }
        }

        return record;
    }
}
