#include "game/SoakTest.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

#include "core/Console.h"
#include "core/DotNet.h"
#include "game/collision/ArenaSpace.h"
#include "game/collision/Bsp.h"
#include "game/kurt/Kurt.h"
#include "game/kurt/StateName.h"
#include "game/level/ArenaStops.h"
#include "game/level/LevelData.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/ObjectBehaviors.h"
#include "game/scripts/ScriptRuntime.h"
#include "game/Viewer.h"

namespace Mdk::Game
{
    using Collision::ArenaSpace;
    using Collision::Bsp;
    using Level::LevelData;
    using Scripts::ScriptRuntime;
    using KurtState = Kurt::Kurt::State;

    namespace
    {
        // C#'s Vector3.ToString(): "<x, y, z>".
        std::string VectorText(Vector3 v)
        {
            return std::format("<{}, {}, {}>", Str::FromFloat(v.X), Str::FromFloat(v.Y), Str::FromFloat(v.Z));
        }
    }

    SoakTest::SoakTest(LevelData& level, ArenaSpace& space, SoakRoute route, float duration)
        : _duration(duration)
    {
        if (route == SoakRoute::Tour)
        {
            auto stops = Stops(level, space);
            _stops.insert(_stops.end(), stops.begin(), stops.end());
        }
    }

    void SoakTest::Step(Kurt::Kurt& kurt, ScriptRuntime& scripts, ArenaSpace& space, float time)
    {
        Travel(scripts, space, time);
        _visited.insert(scripts.CurrentArena);
        if (kurt.Health < HealBelow && kurt.Current != KurtState::Dead)
        {
            kurt.SetHealth(Kurt::Inventory::MaxHealth);
            _heals++;
        }

        Check(kurt, scripts, space, time);
    }

    void SoakTest::Report(ScriptRuntime& scripts, Kurt::Kurt& kurt, float time)
    {
        auto& stats = *scripts.Stats;
        auto f = kurt.Feet;
        auto arenas = std::count_if(_visited.begin(), _visited.end(), [](const std::string& a) { return a.size() != 0; });
        Console::WriteLine(std::format("Soak end t {:.1f}: arenas {} ({}), objects {}, ", time, arenas, scripts.CurrentArena, scripts.Objects.size())
            + std::format("shots {}, hits {}, kills {}, heals {}, ", stats.Shots, stats.ShotHits, stats.Kills, _heals)
            + std::format("Kurt at {:.0f} {:.0f} {:.0f} {}, problems {}", f.X, f.Y, f.Z, Kurt::StateName(kurt.Current), _problems.size()));
    }

    void SoakTest::Travel(ScriptRuntime& scripts, ArenaSpace& space, float time)
    {
        if (_stops.empty())
        {
            return;
        }

        auto count = static_cast<int>(_stops.size());
        auto stop = Math::Min(ToInt(time / (_duration / count)), count - 1);
        if (stop == _stop)
        {
            return;
        }

        _stop = stop;
        auto [arena, point] = _stops[stop];
        Console::WriteLine(std::format("Soak t {:.1f}: teleport to {} {:.0f} {:.0f} {:.0f}", time, arena, point.X, point.Y, point.Z));

        // Doors left open would freeze with their arenas (walking away shuts them first).
        scripts.ShutDoors();
        scripts.TeleportKurt(arena, point, scripts.Kurt->Yaw);

        // Kurt's next move collides with his new arena, not the one the last tick left solid.
        std::string solid[] = { arena };
        space.SetSolid(solid);

        _lost = 0.f;
        _astray = 0.f;
        _falling = 0.f;
        _floor = point;
        _previous = point;
    }

    void SoakTest::Check(Kurt::Kurt& kurt, ScriptRuntime& scripts, ArenaSpace& space, float time)
    {
        auto arena = scripts.CurrentArena;
        if (!IsFinite(kurt.Feet) || !std::isfinite(kurt.Yaw))
        {
            Problem(time, arena, "Kurt's position is not a number", std::format("{} yaw {}", VectorText(kurt.Feet), Str::FromFloat(kurt.Yaw)));
        }

        for (const auto& obj : scripts.Objects)
        {
            if (IsFinite(obj->Position))
            {
                continue;
            }

            Problem(time, obj->Arena, std::format("{} position is not a number", obj->TypeName),
                std::format("{}_{} {}", obj->TypeName, obj->InstanceId, VectorText(obj->Position)));
        }

        // Dead, he counts as on a floor.
        if (kurt.OnFloor && kurt.Current != KurtState::Dead)
        {
            _floor = kurt.Feet;
        }

        // Standing outside every arena (in the air: a jump, a fan, a pit, checked below; dead: he
        // fell 50 below his arena, damp_gravity).
        auto dead = kurt.Current == KurtState::Dead;
        auto outside = !space.ArenaAt(kurt.Feet).has_value() && kurt.OnFloor && !dead;
        if (dead && !space.ArenaAt(kurt.Feet).has_value())
        {
            Note(arena, "Kurt fell out of the arena", Where(kurt));
        }

        _lost = outside ? _lost + Viewer::Step : 0.f;
        if (_lost >= LostTime)
        {
            Problem(time, arena, "Kurt outside every arena", Where(kurt));
        }

        // On a floor of another arena than his, Kurt missed its doorway: when his arena's
        // neighbour goes, nothing holds him (he falls out).
        auto astray = kurt.OnFloor && !dead && StandsAstray(space, arena, kurt.Feet);
        _astray = astray ? _astray + Viewer::Step : 0.f;
        if (_astray >= LostTime)
        {
            Problem(time, arena, "Kurt stands outside his arena", std::format("on {}, {}", space.ArenaAt(kurt.Feet).value_or(""), Where(kurt)));
        }

        // A drawn door that isn't shut shows its other side: that arena must be drawn too.
        const auto& drawn = scripts.DrawnArenas();
        auto found = std::find_if(scripts.Objects.begin(), scripts.Objects.end(), [&drawn](const auto& o)
        {
            return o->Visible && !o->Dead && (o->Flags & Objects::MdkObject::FlagDoor) != 0
                && (o->DoorState & Scripts::ObjectBehaviors::DoorClosed) == 0 && o->Connects.size() != 0
                && std::find(drawn.begin(), drawn.end(), o->Connects) == drawn.end();
        });
        auto hole = found != scripts.Objects.end() ? found->get() : nullptr;
        _hole = hole != nullptr ? _hole + Viewer::Step : 0.f;
        if (hole != nullptr && _hole >= HoleTime)
        {
            Problem(time, arena, "open door shows an undrawn arena",
                std::format("{}_{} in {} to {}, {}", hole->TypeName, hole->InstanceId, hole->Arena, hole->Connects, Where(kurt)));
        }

        _falling = kurt.OnFloor || kurt.Platform != nullptr ? 0.f : _falling + Viewer::Step;

        // The feet went down through a floor this step: the sweep let them through (teleports aside).
        auto lifted = _previous + Vector3(0.f, 0.f, FloorLift);
        auto falling = !kurt.OnFloor && kurt.Feet.Z < _previous.Z && Vector3::Distance(_previous, kurt.Feet) < MaxStep;
        Vector3 crossed;
        if (falling && space.Floor(lifted, kurt.Feet, crossed) && crossed.Z - kurt.Feet.Z > Sink)
        {
            Problem(time, arena, "Kurt fell through a floor", std::format("{}, floor at {:.1f}", Where(kurt), crossed.Z));
        }

        _previous = kurt.Feet;
        if (_falling < FallTime)
        {
            return;
        }

        Note(arena, "Kurt falls into a pit", Where(kurt));
    }

    void SoakTest::Problem(float time, const std::string& arena, const std::string& kind, const std::string& detail)
    {
        if (!_problems.insert(std::format("{} in {}", kind, arena)).second)
        {
            return;
        }

        Console::WriteLine(std::format("Soak problem t {:.2f} in {}: {}: {}", time, arena, kind, detail));
    }

    void SoakTest::Note(const std::string& arena, const std::string& kind, const std::string& detail)
    {
        if (_notes.insert(std::format("{} in {}", kind, arena)).second)
        {
            Console::WriteLine(std::format("Soak note in {}: {}: {}", arena, kind, detail));
        }
    }

    std::string SoakTest::Where(Kurt::Kurt& kurt) const
    {
        return std::format("at {:.1f} {:.1f} {:.1f} {} vz {:.1f}, stood at {:.1f} {:.1f} {:.1f}", kurt.Feet.X, kurt.Feet.Y, kurt.Feet.Z,
            Kurt::StateName(kurt.Current), kurt.VerticalSpeed, _floor.X, _floor.Y, _floor.Z);
    }

    bool SoakTest::StandsAstray(ArenaSpace& space, const std::string& arena, Vector3 feet)
    {
        auto from = feet + Vector3(0.f, 0.f, FloorReach);
        auto to = feet - Vector3(0.f, 0.f, FloorReach);
        std::vector<Bsp*> under;
        for (auto* b : space.At(feet))
        {
            Vector3 point;
            if (b->Segment(from, to, Bsp::SegmentMode::Floor, point) != Bsp::None)
            {
                under.push_back(b);
            }
        }

        return under.size() != 0 && std::all_of(under.begin(), under.end(), [&arena](Bsp* b) { return b->Arena->Name != arena; });
    }

    bool SoakTest::IsFinite(Vector3 v)
    {
        return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
    }

    std::vector<std::pair<std::string, Vector3>> SoakTest::Stops(LevelData& level, ArenaSpace& space)
    {
        std::vector<std::pair<std::string, Vector3>> stops;
        std::vector<Vector3> points;
        for (const auto& entry : level.Dti->Arenas)
        {
            if (!level.IsReachable(entry.Name))
            {
                continue;
            }

            // A corridor's connections are those of its arenas (CHMO_1 ends in HMO_1): visited once.
            auto stop = Level::ArenaStops::Find(level, space, entry.Name);
            if (!stop.has_value() || std::find(points.begin(), points.end(), *stop) != points.end())
            {
                continue;
            }

            points.push_back(*stop);
            stops.emplace_back(entry.Name, *stop);
        }

        return stops;
    }
}
