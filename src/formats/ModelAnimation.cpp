#include "formats/ModelAnimation.h"

namespace Mdk::Formats
{
    namespace
    {
        // C#'s new Vector3((sbyte)r.U8(), (sbyte)r.U8(), (sbyte)r.U8()): the reads in order.
        Vector3 Deltas(BinReader& r)
        {
            float x = static_cast<sbyte>(r.U8());
            float y = static_cast<sbyte>(r.U8());
            float z = static_cast<sbyte>(r.U8());
            return {x, y, z};
        }
    }

    std::shared_ptr<ModelAnimation> ModelAnimation::Parse(const std::string& name, SharedBytes bytes, int offset)
    {
        auto animation = std::make_shared<ModelAnimation>();
        animation->Name = name;
        animation->_bytes = bytes;
        BinReader r(*bytes, offset);
        animation->Speed = r.F32();
        auto trackCount = r.U32();
        animation->FrameCount = static_cast<int>(r.U32());
        for (uint i = 0; i < trackCount; i++)
        {
            auto trackOffset = offset + 4 + static_cast<int>(r.U32());
            animation->_tracks[Str::ToLower(Bin::Ascii(*bytes, trackOffset, 12))] = trackOffset;
        }

        animation->RootMotion.resize(animation->FrameCount);
        for (auto f = 0; f < animation->FrameCount; f++)
        {
            animation->RootMotion[f] = r.Vec3();
        }

        auto pointCount = r.U32();
        for (uint i = 0; i < pointCount; i++)
        {
            std::vector<Vector3> points(animation->FrameCount);
            for (auto f = 0; f < animation->FrameCount; f++)
            {
                points[f] = r.Vec3();
            }

            animation->ReferencePoints.push_back(std::move(points));
        }

        return animation;
    }

    std::shared_ptr<ModelAnimation> ModelAnimation::ParseBeta(const std::string& name, SharedBytes bytes, int offset)
    {
        auto animation = std::make_shared<ModelAnimation>();
        animation->Name = name;
        animation->_bytes = bytes;
        animation->_layout = Layout::Beta;
        BinReader r(*bytes, offset);
        auto trackCount = r.U32();
        animation->FrameCount = static_cast<int>(r.U32());
        for (uint i = 0; i < trackCount; i++)
        {
            auto trackOffset = offset + static_cast<int>(r.U32());
            animation->_tracks[Str::ToLower(Bin::Ascii(*bytes, trackOffset, TrackNameLength))] = trackOffset;
        }

        animation->RootMotion.resize(animation->FrameCount);
        for (auto f = 0; f < animation->FrameCount; f++)
        {
            animation->RootMotion[f] = r.Vec3();
        }

        r.Skip(animation->FrameCount * BoundsSize);
        auto pointCount = r.U32();
        for (uint i = 0; i < pointCount; i++)
        {
            std::vector<Vector3> points(animation->FrameCount);
            for (auto f = 0; f < animation->FrameCount; f++)
            {
                points[f] = r.Vec3();
            }

            animation->ReferencePoints.push_back(std::move(points));
        }

        return animation;
    }

    bool ModelAnimation::Animates(const Model& model) const
    {
        for (auto& part : model.PartList)
        {
            if (_tracks.contains(Str::ToLower(part->Name)))
            {
                return true;
            }
        }

        return false;
    }

    std::vector<std::vector<std::vector<Vector3>>> ModelAnimation::Bake(const Model& model) const
    {
        std::vector<std::optional<std::vector<std::vector<Vector3>>>> perPart;
        for (auto& p : model.PartList)
        {
            auto found = _tracks.find(Str::ToLower(p->Name));
            perPart.push_back(found != _tracks.end() ? std::optional(DecodeTrack(found->second, static_cast<int>(p->Vertices.size()))) : std::nullopt);
        }

        std::vector<std::vector<std::vector<Vector3>>> frames(FrameCount);
        for (auto f = 0; f < FrameCount; f++)
        {
            frames[f].resize(model.PartList.size());
            for (size_t p = 0; p < model.PartList.size(); p++)
            {
                frames[f][p] = perPart[p] ? (*perPart[p])[f] : model.PartList[p]->Vertices;
            }
        }

        return frames;
    }

    std::vector<std::vector<Vector3>> ModelAnimation::DecodeTrack(int offset, int vertexCount) const
    {
        auto scaleBits = Bin::U32(*_bytes, offset + 16);
        if (_layout == Layout::Beta)
        {
            return DecodeBetaTrack(offset, vertexCount, Bin::F32(*_bytes, offset + 16));
        }

        return (scaleBits & SignMask) == 0
            ? DecodeMatrixTrack(offset, vertexCount)
            : DecodeDeltaTrack(offset, vertexCount, Bin::F32(*_bytes, offset + 16));
    }

    std::vector<std::vector<Vector3>> ModelAnimation::DecodeDeltaTrack(int offset, int vertexCount, float scale) const
    {
        std::vector<std::vector<Vector3>> frames;
        BinReader r(*_bytes, offset + 20);
        std::vector<Vector3> vertices(vertexCount);
        for (auto v = 0; v < vertexCount; v++)
        {
            vertices[v] = r.Vec3();
        }

        frames.push_back(vertices);
        int next = r.S16();
        for (auto f = 1; f < FrameCount; f++)
        {
            if (next == f)
            {
                for (auto v = 0; v < vertexCount; v++)
                {
                    vertices[v] += Deltas(r) * scale;
                }

                next = r.S16();
            }

            frames.push_back(vertices);
        }

        return frames;
    }

    std::vector<std::vector<Vector3>> ModelAnimation::DecodeBetaTrack(int offset, int vertexCount, float scale) const
    {
        std::vector<std::vector<Vector3>> frames;
        BinReader r(*_bytes, offset + 20);
        std::vector<Vector3> vertices(vertexCount);
        for (auto v = 0; v < vertexCount; v++)
        {
            vertices[v] = r.Vec3();
        }

        frames.push_back(vertices);
        for (auto f = 1; f < FrameCount; f++)
        {
            for (auto v = 0; v < vertexCount; v++)
            {
                vertices[v] += Deltas(r) * scale;
            }

            frames.push_back(vertices);
        }

        return frames;
    }

    std::vector<std::vector<Vector3>> ModelAnimation::DecodeMatrixTrack(int offset, int vertexCount) const
    {
        auto& bytes = *_bytes;
        std::vector<std::vector<Vector3>> frames;
        auto rotationScale = 1.0f / static_cast<float>(0x8000 >> (bytes.at(offset + 20) & 31));
        auto positionScale = 1.0f / static_cast<float>(0x8000 >> (bytes.at(offset + 21) & 31));
        BinReader r(bytes, offset + 22);
        std::vector<Vector3> baseVertices(vertexCount);
        for (auto v = 0; v < vertexCount; v++)
        {
            baseVertices[v] = r.Vec3();
        }

        float m[MatrixValues] = {};
        for (auto f = 0; f < FrameCount; f++)
        {
            for (auto i = 0; i < MatrixValues; i++)
            {
                m[i] = r.S16();
            }

            std::vector<Vector3> vertices(vertexCount);
            for (auto v = 0; v < vertexCount; v++)
            {
                auto b = baseVertices[v];
                vertices[v] = Vector3(
                    (m[0] * b.X + m[1] * b.Y + m[2] * b.Z) * rotationScale + m[3] * positionScale,
                    (m[4] * b.X + m[5] * b.Y + m[6] * b.Z) * rotationScale + m[7] * positionScale,
                    (m[8] * b.X + m[9] * b.Y + m[10] * b.Z) * rotationScale + m[11] * positionScale);
            }

            frames.push_back(std::move(vertices));
        }

        return frames;
    }
}
