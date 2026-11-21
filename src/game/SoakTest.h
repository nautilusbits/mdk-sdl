#pragma once

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/Numerics.h"
#include "game/kurt/Inventory.h"

namespace Mdk::Game::Collision
{
    class ArenaSpace;
}

namespace Mdk::Game::Kurt
{
    class Kurt;
}

namespace Mdk::Game::Level
{
    class LevelData;
}

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;
}

namespace Mdk::Game
{
    // What the soak test does in a level besides the keys: stay there, or tour every arena.
    enum class SoakRoute { Stay, Tour };

    // The soak test in a level (--soak, --tour): Kurt is healed so he lives on, the tour
    // teleports him to each reachable arena in turn (to a floor under one of its records), and every
    // step looks for problems: NaN positions, Kurt standing outside every arena or falling through a
    // floor (falling into a pit is only noted), an open door onto an arena not drawn.
    // Problems print as "Soak problem ..." (once per kind and arena), the counts at the end ("Soak end").
    //   step ──► tour: next arena due? ──► teleport
    //        ├─► health low ──► healed
    //        └─► checks ──► problems (printed once)
    class SoakTest
    {
    public:
        SoakTest(Level::LevelData& level, Collision::ArenaSpace& space, SoakRoute route, float duration);

        // After each game step at game time `time`.
        void Step(Kurt::Kurt& kurt, Scripts::ScriptRuntime& scripts, Collision::ArenaSpace& space, float time);

        // The summary line at the end.
        void Report(Scripts::ScriptRuntime& scripts, Kurt::Kurt& kurt, float time);

    private:
        static constexpr int HealBelow = Kurt::Inventory::MaxHealth / 2;
        // Seconds standing outside every arena before it's a problem, falling before it's a pit.
        static constexpr float LostTime = 3.f;
        // Seconds an open door may show an undrawn arena (it's shown a tick after it opens).
        static constexpr float HoleTime = 0.5f;
        static constexpr float FallTime = 6.f;
        // The feet stand this little above a floor; a step's move starts this high.
        static constexpr float FloorLift = 0.05f;
        // Feet this far under a floor went through it (they touch it standing).
        static constexpr float Sink = 0.5f;
        // The floor Kurt stands on is this close to his feet.
        static constexpr float FloorReach = 1.f;
        // Longer moves in a step are teleports (the fastest fall moves 250 / 60).
        static constexpr float MaxStep = 10.f;

        float _duration;
        std::vector<std::pair<std::string, Vector3>> _stops;
        std::unordered_set<std::string> _problems;
        std::unordered_set<std::string> _notes;
        std::unordered_set<std::string> _visited;
        int _stop = -1;
        float _lost = 0;
        // Seconds standing on another arena's floor than Kurt's.
        float _astray = 0;
        float _hole = 0;
        float _falling = 0;
        int _heals = 0;
        // Where Kurt last stood (problems tell where a fall started).
        Vector3 _floor;
        Vector3 _previous;

        // Teleports Kurt to the next arena of the tour when its time comes.
        void Travel(Scripts::ScriptRuntime& scripts, Collision::ArenaSpace& space, float time);

        void Check(Kurt::Kurt& kurt, Scripts::ScriptRuntime& scripts, Collision::ArenaSpace& space, float time);

        // Prints a problem the first time its kind shows in an arena.
        void Problem(float time, const std::string& arena, const std::string& kind, const std::string& detail);

        // Prints something that isn't a problem (pits are in the levels) once per arena.
        void Note(const std::string& arena, const std::string& kind, const std::string& detail);

        std::string Where(Kurt::Kurt& kurt) const;

        // Whether the floor under the feet is another arena's, not Kurt's (objects and
        // platforms aren't floors of any).
        static bool StandsAstray(Collision::ArenaSpace& space, const std::string& arena, Vector3 feet);

        static bool IsFinite(Vector3 v);

        // A point on a floor in each reachable arena (ArenaStops).
        static std::vector<std::pair<std::string, Vector3>> Stops(Level::LevelData& level, Collision::ArenaSpace& space);
    };
}
