#include "Mdk.Game/Scripts/EndLevel.h"

#include <algorithm>
#include <format>

#include "Mdk.Formats/Arena.h"
#include "Mdk.Formats/Dti.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/MaterialResolver.h"
#include "Mdk.Game/Scripts/ScriptMath.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"

namespace Mdk::Game::Scripts
{
    std::string EndLevel::Arena() const
    {
        return _arena != nullptr ? _arena->Name : "";
    }

    void EndLevel::Start(ScriptRuntime& runtime)
    {
        _runtime = &runtime;
        _arena = nullptr;
        for (const auto& a : runtime.Level->Arenas)
        {
            if (a->Name == runtime.CurrentArena)
            {
                _arena = a;
                break;
            }
        }

        if (_arena != nullptr)
        {
            for (auto triangle : HighestFirst(*runtime.Level, *_arena))
            {
                _queue.push_back(triangle);
            }
        }

        runtime.Kurt->Frozen = true;
        runtime.Kurt->Rising = true;
        runtime.RaiseShake(StartShake);
    }

    std::vector<int> EndLevel::HighestFirst(const Level::LevelData& level, const Formats::Arena& arena)
    {
        auto archives = level.ArchivesOf(arena);
        std::vector<int> triangles;
        for (auto t = 0; t < arena.TriangleCount(); t++)
        {
            if ((arena.TriangleFlags[t] & Hidden) != 0)
            {
                continue;
            }

            if (!Level::MaterialResolver::IsDrawn(arena.TriangleMaterials[t], arena.Materials, archives))
            {
                continue;
            }

            triangles.push_back(t);
        }

        // OrderByDescending is a stable sort.
        std::stable_sort(triangles.begin(), triangles.end(), [&](int a, int b) { return Top(arena, a) > Top(arena, b); });
        return triangles;
    }

    float EndLevel::Top(const Formats::Arena& arena, int triangle)
    {
        auto top = arena.Vertices[arena.TriangleIndices[triangle * 3]].Z;
        for (auto k = 1; k < 3; k++)
        {
            top = MathF::Max(top, arena.Vertices[arena.TriangleIndices[triangle * 3 + k]].Z);
        }

        return top;
    }

    Vector3 EndLevel::Centre(const Formats::Arena& arena, int triangle)
    {
        auto sum = Vector3::Zero();
        for (auto k = 0; k < 3; k++)
        {
            sum = sum + arena.Vertices[arena.TriangleIndices[triangle * 3 + k]];
        }

        return sum / 3.0f;
    }

    void EndLevel::Update(float ticks)
    {
        if (_done || _runtime == nullptr)
        {
            return;
        }

        auto& kurt = *_runtime->Kurt;
        TearOff(_runtime->Rng->Next(PiecesPerTick));
        MovePieces(kurt.Feet, ticks);

        // Kurt takes off, turns and rises (0x40b558).
        _turn = MathF::Min(_turn + TurnGrowth * ticks, TurnMax);
        kurt.Yaw -= _turn * ticks;
        _riseSpeed = RiseSpeed(_riseSpeed, ticks);
        kurt.Feet = kurt.Feet + Vector3(0.0f, 0.0f, _riseSpeed * ticks);
        _takeoff += ticks;
        kurt.Pose = _takeoff < _takeoffFrames ? Game::Kurt::Kurt::PoseFrame{std::string(Takeoff), ToInt(_takeoff)}
                                              : Game::Kurt::Kurt::PoseFrame{std::string(Floating), ToInt(_takeoff)};
        if (_riseSpeed <= FastRise)
        {
            return;
        }

        // The view tilts up, then the screen goes white.
        auto arenaPitch = DefaultPitch;
        for (const auto& a : _runtime->Level->Dti->Arenas)
        {
            if (a.Name == Arena())
            {
                arenaPitch = a.Pitch;
                break;
            }
        }

        auto tilt = (TiltTarget - arenaPitch) * TiltShare;
        if (PitchOffset > tilt)
        {
            PitchOffset = MathF::Max(PitchOffset - TiltSpeed * ticks / Game::Kurt::Kurt::Ticks, tilt);
            kurt.CameraTilt = PitchOffset;
            return;
        }

        _flash += FlashStep * ticks;
        kurt.WhiteFlash = MathF::Max(kurt.WhiteFlash, MathF::Min(_flash, FlashMax));
        if (_flash <= FlashEnd)
        {
            return;
        }

        _done = true;
        if (Finished)
        {
            Finished();
        }
    }

    std::string EndLevel::Describe() const
    {
        // .NET's "0" rounds half away from zero.
        return std::format("end of level: torn {}, pieces {}, rise {:.2f}, tilt {:.1f}, flash {:.0f}",
            _torn.size(), _pieces.size(), _riseSpeed, PitchOffset, MathF::RoundAwayFromZero(_flash));
    }

    float EndLevel::RiseSpeed(float speed, float ticks)
    {
        speed += Rise * ticks;
        return speed > FastRise ? speed + Rise * ticks * FastRiseFactor : speed;
    }

    void EndLevel::TearOff(int count)
    {
        for (auto i = 0; i < count && !_queue.empty(); i++)
        {
            auto triangle = _queue.front();
            _queue.pop_front();
            _torn.push_back(triangle);
            _pieces.push_back(std::make_shared<Piece>(triangle, Centre(*_arena, triangle)));
        }
    }

    void EndLevel::MovePieces(Vector3 kurt, float ticks)
    {
        auto limit = kurt.Z + Limit;
        for (auto i = static_cast<int>(_pieces.size()) - 1; i >= 0; i--)
        {
            auto& piece = *_pieces[i];
            piece.RiseSpeed += Rise * ticks;
            piece.Spin = MathF::Min(piece.Spin + SpinGrowth * ticks, SpinMax);
            auto turn = Matrix3x2::CreateRotation(ScriptMath::ToRadians(piece.Spin * ticks));
            auto around = Vector2::Transform(Vector2(piece.Centre.X - kurt.X, piece.Centre.Y - kurt.Y), turn);
            piece.Centre = Vector3(kurt.X + around.X, kurt.Y + around.Y, piece.Centre.Z + piece.RiseSpeed * ticks);
            piece.Angle += piece.Spin * ticks;
            if (piece.Centre.Z > limit)
            {
                _pieces.erase(_pieces.begin() + i);
            }
        }
    }
}
