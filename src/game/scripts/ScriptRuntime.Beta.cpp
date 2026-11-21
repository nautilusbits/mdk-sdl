#include "game/scripts/ScriptRuntime.h"

#include <cmath>
#include <limits>
#include <vector>

#include "formats/Cmi.h"
#include "game/collision/ArenaSpace.h"
#include "game/level/LevelData.h"
#include "game/scripts/ScriptMath.h"

namespace Mdk::Game::Scripts
{
    using namespace ScriptMath;

    bool ScriptRuntime::IsBeta() const
    {
        return Cmi->Dialect() == Formats::Scripts::ScriptDialect::Beta1996;
    }

    std::string ScriptRuntime::BetaArena()
    {
        auto arena = _space->ArenaAt(KurtPosition).value_or("");
        return CurrentArena.length() == 0 || Connects(CurrentArena, arena) ? arena : CurrentArena;
    }

    bool ScriptRuntime::Connects(const std::string& from, const std::string& to)
    {
        for (const auto& r : ArenaRecords(from))
        {
            if (r.Type == Game::Level::LevelData::Connection && _connections.GetValueOrDefault({from, r.Id}) == to)
            {
                return true;
            }
        }

        return false;
    }

    void ScriptRuntime::NoteAlarmEnded(MdkObject& obj)
    {
        if (obj.MoveCommand == AlarmCommand)
        {
            AlarmEndedTicks = AlarmEndedDuration;
        }
    }

    int ScriptRuntime::BetaHitPart(MdkObject& obj, Vector3 origin)
    {
        if (obj.Model == nullptr)
        {
            return -1;
        }

        constexpr auto Infinity = std::numeric_limits<float>::infinity();
        auto aim = FromAngle(KurtYaw);
        const auto& bounds = obj.PartBounds();
        std::vector<float> distances(bounds.size());
        for (int i = 0; i < static_cast<int>(bounds.size()); i++)
        {
            distances[i] = Infinity;
            if ((obj.HiddenParts & (1 << i)) != 0 || !bounds[i].has_value())
            {
                continue;
            }

            auto center = Center(GetWorldBounds(obj, *bounds[i]));
            auto across = Vector2(center.X - origin.X, center.Y - origin.Y);
            distances[i] = MathF::Abs(across.X * aim.Y - across.Y * aim.X);
        }

        // DefaultIfEmpty(+inf).Min(): NaN wins as in LINQ's float Min.
        auto nearest = Infinity;
        for (auto d : distances)
        {
            nearest = MathF::Min(nearest, d);
        }

        if (std::isinf(nearest) && nearest > 0.0f)
        {
            return -1;
        }

        std::vector<int> candidates;
        for (int i = 0; i < static_cast<int>(distances.size()); i++)
        {
            if (distances[i] <= nearest + BetaPartSpread)
            {
                candidates.push_back(i);
            }
        }

        return candidates[Rng->Next(static_cast<int>(candidates.size()))];
    }
}
