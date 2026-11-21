#include "formats/Model.h"

namespace Mdk::Formats
{
    std::shared_ptr<Model> Model::Parse(const std::string& name, std::span<const byte> bytes, int offset, Header header, Parts parts)
    {
        auto model = std::make_shared<Model>();
        model->Name = name;
        BinReader r(bytes, offset);
        auto named = header == Header::Flags ? r.U32() != 0 : parts == Parts::Named;
        auto materialCount = r.U32();
        for (uint i = 0; i < materialCount; i++)
        {
            auto material = r.Name(16);
            model->Materials.push_back(material.size() > MaterialNameLength ? material.substr(0, MaterialNameLength) : material);
        }

        auto partCount = named ? r.U32() : 1;
        for (uint i = 0; i < partCount; i++)
        {
            model->PartList.push_back(ParsePart(r, named));
        }

        model->Bounds = ReadBounds(r);
        auto referenceCount = r.U32();
        for (uint i = 0; i < referenceCount; i++)
        {
            model->ReferencePoints.push_back(r.Vec3());
        }

        return model;
    }

    std::shared_ptr<Model::Part> Model::ParsePart(BinReader& r, bool named)
    {
        auto part = std::make_shared<Part>();
        if (named)
        {
            part->Name = r.Name(12);
            part->Pivot = r.Vec3();
        }

        part->Vertices.resize(r.U32());
        for (size_t v = 0; v < part->Vertices.size(); v++)
        {
            part->Vertices[v] = r.Vec3();
        }

        auto triangleCount = static_cast<int>(r.U32());
        part->TriangleIndices.resize(static_cast<size_t>(triangleCount) * 3);
        part->TriangleMaterials.resize(triangleCount);
        part->TriangleUvs.resize(static_cast<size_t>(triangleCount) * 3);
        for (auto t = 0; t < triangleCount; t++)
        {
            part->TriangleIndices[t * 3] = r.S16();
            part->TriangleIndices[t * 3 + 1] = r.S16();
            part->TriangleIndices[t * 3 + 2] = r.S16();
            part->TriangleMaterials[t] = r.S16();
            for (auto k = 0; k < 3; k++)
            {
                part->TriangleUvs[t * 3 + k] = Vector2{r.F32(), r.F32()};
            }

            r.Skip(4);
        }

        if (named)
        {
            part->Bounds = ReadBounds(r);
        }

        return part;
    }

    Box Model::ReadBounds(BinReader& r)
    {
        float x0 = r.F32(), x1 = r.F32(), y0 = r.F32(), y1 = r.F32(), z0 = r.F32(), z1 = r.F32();
        return Box{Vector3(x0, y0, z0), Vector3(x1, y1, z1)};
    }

    std::vector<std::vector<Vector3>> Model::RestPose() const
    {
        std::vector<std::vector<Vector3>> pose;
        for (auto& part : PartList)
        {
            pose.push_back(part->Vertices);
        }

        return pose;
    }
}
