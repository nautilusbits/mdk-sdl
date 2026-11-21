#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/DotNet.h"
#include "core/Numerics.h"

namespace Mdk::Formats
{
    class Arena;
}

namespace Mdk::Game::Level
{
    class LevelData;
}

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;

    // The end of a level (endlev.c: 0x40a9e0, 0x40ad9c each frame, pieces 0x40b280, Kurt
    // 0x40b558; end_level.gd). The visible triangles of Kurt's arena are taken from the highest down:
    // each tick 0-7 more are torn off and fly up (vz += 0.025 per tick) spinning around Kurt (the spin
    // grows by 0.15° per tick up to 2.5°), until they're 500 above him. Kurt takes off (K_TAKEOF, then
    // K_FLOATC), turns ever faster (0.15° per tick², up to 2.5° per tick) and rises with them (0.025
    // per tick²); once he rises faster than 3 units per tick his rise speeds up three times as fast
    // and the view tilts up (22.5° per second, to (-60 - the arena's pitch) / 2), then the screen goes
    // white (8 per tick); past 300 the level is over.
    //
    //   arena triangles, highest first ──0-7 a tick──► pieces: rise, spin around Kurt ──500 above──► gone
    //   Kurt: rise ──vz > 3──► tilt up ──► white flash ──> 300──► Finished
    class EndLevel
    {
    public:
        // A torn-off triangle: where its centre is and how far it turned (degrees).
        class Piece
        {
        public:
            Piece(int triangle, Vector3 centre) : Triangle(triangle), Centre(centre) {}

            int Triangle;
            Vector3 Centre;
            float Angle = 0.0f;
            float RiseSpeed = 0.0f;
            float Spin = 0.0f;
        };

        static constexpr std::string_view Takeoff = "K_TAKEOF";

        explicit EndLevel(int takeoffFrames) : _takeoffFrames(takeoffFrames) {}

        std::function<void()> Finished;

        // Added to the camera pitch (degrees, negative looks up; 0x573b1c).
        float PitchOffset = 0.0f;

        // The arena torn apart.
        std::string Arena() const;

        // The triangles torn off so far, in order (the view hides them).
        const std::vector<int>& Torn() const { return _torn; }

        const std::vector<std::shared_ptr<Piece>>& Pieces() const { return _pieces; }

        // Starts in Kurt's arena: he stops, the screen shakes.
        void Start(ScriptRuntime& runtime);

        // The visible triangles of an arena (shown, with a drawn material), the highest top first.
        static std::vector<int> HighestFirst(const Level::LevelData& level, const Formats::Arena& arena);

        void Update(float ticks);

        // For tests: the triangles torn off, the pieces flying, the tilt and the flash.
        std::string Describe() const;

        // Kurt's rise speed after a step: 0.025 per tick², three times that above 3 per tick.
        static float RiseSpeed(float speed, float ticks);

    private:
        static constexpr std::string_view Floating = "K_FLOATC";
        static constexpr float Rise = 0.025f;
        static constexpr float SpinGrowth = 0.15f;
        static constexpr float SpinMax = 2.5f;
        static constexpr float Limit = 500.0f;
        static constexpr int PiecesPerTick = 8;
        static constexpr float FastRise = 3.0f;
        static constexpr float FastRiseFactor = 2.0f;
        static constexpr float TurnGrowth = 0.15f;
        static constexpr float TurnMax = 2.5f;
        static constexpr float TiltSpeed = 22.5f;
        static constexpr float TiltTarget = -60.0f;
        static constexpr float TiltShare = 0.5f;
        static constexpr float DefaultPitch = 4.0f;
        static constexpr float FlashStep = 8.0f;
        static constexpr float FlashEnd = 300.0f;
        static constexpr float FlashMax = 255.0f;
        static constexpr float StartShake = 5.0f;
        // Triangle flag of hidden triangles (group_set_state).
        static constexpr uint Hidden = 0x10;

        int _takeoffFrames;
        std::deque<int> _queue;
        std::vector<int> _torn;
        std::vector<std::shared_ptr<Piece>> _pieces;
        ScriptRuntime* _runtime = nullptr;
        std::shared_ptr<Formats::Arena> _arena;
        float _riseSpeed = 0.0f;
        float _turn = 0.0f;
        float _flash = 0.0f;
        float _takeoff = 0.0f;
        bool _done = false;

        static float Top(const Formats::Arena& arena, int triangle);

        static Vector3 Centre(const Formats::Arena& arena, int triangle);

        // The next count highest triangles become pieces.
        void TearOff(int count);

        // The pieces rise and spin around Kurt; those 500 above him go.
        void MovePieces(Vector3 kurt, float ticks);
    };
}
