#include "game/objects/RopeView.h"

#include <limits>

#include "core/DotNet.h"
#include "game/FrameBlend.h"
#include "game/level/ArenaBand.h"
#include "game/objects/MdkObject.h"

namespace Mdk::Game::Objects
{
    using Engine::Render::DepthBand;
    using Engine::Render::Material;
    using Engine::Render::Pass;
    using Engine::Render::Vertex;

    RopeView::RopeView(Engine::Render::Renderer& renderer, Level::LevelData& level)
        : _renderer(renderer), _level(level), _mesh(renderer.CreateDynamicMesh(MaxVertices))
    {
    }

    FrameBlend& RopeView::Steps()
    {
        static FrameBlend steps(Blending::Off);
        return steps;
    }

    std::vector<Vector3> RopeView::LinesOf(MdkObject& obj)
    {
        return LinesOf(obj, Steps());
    }

    std::vector<Vector3> RopeView::LinesOf(MdkObject& obj, FrameBlend& blend)
    {
        std::vector<Vector3> lines;
        AddLines(obj, blend, lines);
        return lines;
    }

    void RopeView::AddLines(MdkObject& obj, FrameBlend& blend, std::vector<Vector3>& lines)
    {
        if (obj.RopeMask == AllLines)
        {
            for (size_t i = 0; i < obj.RopePoints.size(); i++)
            {
                if (obj.RopePoints[i] != Vector3::Zero())
                {
                    lines.push_back(blend.Carried(obj, obj.ReferencePoint(static_cast<int>(i) + 1)));
                    lines.push_back(obj.RopePoints[i]);
                }
            }

            return;
        }

        for (auto i = 0; i < Pairs; i++)
        {
            if ((obj.RopeMask & (1 << i)) != 0)
            {
                lines.push_back(blend.Carried(obj, obj.RopePoints[i * 2]) + Vector3::UnitZ() * PairLift);
                lines.push_back(obj.RopePoints[i * 2 + 1]);
            }
        }
    }

    void RopeView::Draw(std::span<const std::shared_ptr<MdkObject>> objects, FrameBlend& blend, const std::string& camera)
    {
        _vertices.clear();
        _draws.clear();
        for (const auto& obj : objects)
        {
            if (obj->RopeMask == 0 || !obj->Visible || obj->Dead)
            {
                continue;
            }

            auto& lines = _lines;
            lines.clear();
            AddLines(*obj, blend, lines);
            if (lines.empty() || static_cast<int>(_vertices.size() + lines.size()) > MaxVertices)
            {
                continue;
            }

            _draws.push_back(Queued{static_cast<int>(_vertices.size()), static_cast<int>(lines.size()), Material::Flat(Colour(obj->Arena, obj->RopeColor), Pass::Solid), Level::ArenaBand::Of(obj->Arena, camera)});
            for (const auto& point : lines)
            {
                _vertices.push_back(Vertex(point, Vector2::Zero()));
            }
        }

        if (_vertices.empty())
        {
            return;
        }

        _renderer.UpdateMesh(_mesh, _vertices);
        for (const auto& draw : _draws)
        {
            _renderer.Band = draw.Band;
            _renderer.DrawLines(_mesh, draw.First, draw.Count, draw.Material);
        }

        _renderer.Band = DepthBand::Front;
    }

    Vector4 RopeView::Colour(const std::string& arena, int index)
    {
        auto found = _palettes.find(arena);
        if (found == _palettes.end())
        {
            auto named = _level.ArenaNamed(arena);
            found = _palettes.emplace(arena, named != nullptr ? _level.PaletteOf(*named) : _level.Dti->Palette).first;
        }

        const auto& rgba = found->second->Rgba;
        auto at = Math::Clamp(index, 0, static_cast<int>(std::numeric_limits<byte>::max())) * 4;
        return Vector4(rgba[at], rgba[at + 1], rgba[at + 2], std::numeric_limits<byte>::max()) * ByteToUnit;
    }
}
