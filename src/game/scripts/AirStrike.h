#pragma once

#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "core/Numerics.h"

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    class BezierPath;
    class ScriptRuntime;

    // Bones' air strike (sniper ammo type 5, SW_BONES; 0x4641ac, 0x43fa0c, 0x43deac; a port of
    // godot-mdk's air_strike.gd, docs/gameplay.md "Sniper mode").
    //
    //   start (50 behind Kurt, arena top + 30) ─► before ─► over the target (+32) ─► after ─► end
    //                                             teeth: 9 X_TOOTH grenades, one every 15 units
    //   last two levels: one strike only, diving into the target (450 damage within 80)
    //
    // The point in the crosshair (within 5000 units, with open sky 1000 units above it) is the target.
    // X_STRIKE flies a 5-point curve at 150 units/s.
    class AirStrike
    {
    public:
        explicit AirStrike(ScriptRuntime& runtime) : _runtime(&runtime) {}

        // The one strike of the last two levels was used (0x57440b).
        bool UsedUp = false;

        bool IsActive() const;

        // The point in the view from eye a strike can hit (0x4641ac): within
        // 5000 units, with open sky 1000 units above it; null if there's none.
        std::optional<Vector3> FindTarget(Vector3 eye, Vector3 forward);

        // Calls the strike on the point in the view. Returns false (and Kurt blows a raspberry)
        // without a target, while a strike is out or when it's used up.
        bool Call(Vector3 eye, Vector3 forward);

        // Moves the strike by ticks.
        void Update(float ticks);

    private:
        // Over the target dropping teeth, or diving into it.
        enum class Flight { Over, Dive };

        static constexpr float Speed = 150.0f;
        static constexpr float Range = 5000.0f;
        static constexpr float Sky = 1000.0f;
        static constexpr float Height = 30.0f;
        static constexpr float OverTarget = 32.0f;
        static constexpr float Behind = 50.0f;
        static constexpr int Teeth = 9;
        static constexpr float ToothSpacing = 15.0f;
        // The middle tooth falls over the target.
        static constexpr float ToothBias = 5.0f;
        static constexpr int ToothTicks = 900;
        static constexpr float ToothFall = -5.0f;
        static constexpr int DiveDamage = 450;
        static constexpr int DiveKurtDamage = 67;
        static constexpr float DiveRadius = 80.0f;
        static constexpr int HitDive = -6;
        static constexpr float ExplosionScale = 2.0f;
        static constexpr int TargetsAll = 6;
        static constexpr int TargetKurt = 1;
        static constexpr int StrikeHealth = 65000;
        static constexpr int StrikeFlags = 0x85e20;
        // Thrown items' flags (Items.ThrownFlags): the teeth fall as hand grenades.
        static constexpr int ThrownFlags = 0x818a6;
        // Levels after the fourth in the order of play (LEVEL8, LEVEL5) have the dive.
        static constexpr int LastStrikeLevel = 3;
        // Points towards the target are tried in steps of 9% of the way.
        static constexpr float ClearStep = 0.09f;
        static constexpr int ClearSteps = 10;
        static constexpr float ClearFallback = 0.9f;
        // The target sees the path from 2 units above it.
        static constexpr float TargetEye = 2.0f;

        ScriptRuntime* _runtime;
        std::shared_ptr<Objects::MdkObject> _strike;
        std::shared_ptr<BezierPath> _curve;
        float _offset = 0.0f;
        float _targetOffset = 0.0f;
        float _diveOffset = std::numeric_limits<float>::infinity();
        int _teeth = 0;
        bool _diving = false;

        void Start(Vector3 target, Flight flight);

        // The first point from from towards over that the target can see.
        Vector3 ClearPoint(Vector3 from, Vector3 over, Vector3 target);

        // The diving strike blows up when it hits something.
        void Crash(Objects::MdkObject& strike);

        // A tooth falls as a hand grenade.
        void DropTooth(Objects::MdkObject& strike);
    };

    // A path through points with Bézier handles of a quarter of the neighbours' distance
    // (Godot's Curve3D as the Godot port builds it), sampled by length.
    class BezierPath
    {
    public:
        explicit BezierPath(const std::vector<Vector3>& points);

        float Length() const { return _lengths.back(); }

        // The point offset along the path.
        Vector3 Sample(float offset) const;

        // The offset along the path of its point nearest to point.
        float ClosestOffset(Vector3 point) const;

    private:
        static constexpr float HandleFactor = 0.25f;
        static constexpr int SamplesPerSegment = 64;

        std::vector<Vector3> _points;
        std::vector<float> _lengths;

        static Vector3 Cubic(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);
    };
}
