#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <json.hpp>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/Dti.h"

namespace Mdk::Game::Scripts
{
    // Fans (updrafts) of the arenas (a port of godot-mdk's fans.gd; arena+0x45e):
    // created on the arenas' type-7 hotspots by fan_create (0x413a94), they lift Kurt, objects and
    // pieces inside their box (updraft_query 0x413c24 → 0x413d14). Each tick, with a chance of 1 in 8,
    // a still fire spark appears just above the bottom of the box for the updraft to lift (0x414230).
    // See godot-mdk docs/engine.md ("Fans and conveyors").
    //
    //        z1 + 5 ┌───────┐  the box reaches 5 units above the top
    //           z1  │ ↑ ↑ ↑ │  type 6: full strength up to 5 below z1, then fading; wobble near z1
    //               │ ↑ ↑ ↑ │
    //     z0 - 0.5  └───────┘  sparks at z0 + 0.25
    class Fans
    {
    public:
        // Who asks: Kurt, an object, or a spark or piece; a fan acts on those whose bit is in its mask.
        static constexpr int MaskKurt = 1;
        static constexpr int MaskObjects = 2;
        static constexpr int MaskEffects = 8;

        enum class Power { Off, On };

        Fans(std::vector<Formats::Dti::ArenaEntry> arenas, std::shared_ptr<Random> rng);

        // A fan's still spark: (arena, point).
        std::function<void(const std::string&, Vector3)> Spark;
        // Whether an arena's objects run (only their fans let out sparks).
        std::function<bool(const std::string&)> IsLive = [](const std::string&) { return true; };

        int Count() const { return static_cast<int>(_fans.size()); }

        // fan_create (0x413a94): a fan on the arena's hotspot hotspot. With
        // type 6, strength is the time to rise through the box.
        void Create(const std::string& arena, int hotspot, const std::string& name, int param, int type, float strength);

        // The fans for a full save.
        nlohmann::json Snapshot() const;

        // The fans of a full save, as they were.
        void Restore(const nlohmann::json& data);

        // fan_remove (0x413fa0).
        void Remove(const std::string& arena, const std::string& name);

        // fan_enable (0x4140e4): sets or clears bit 0 of the fan's mask.
        void Enable(const std::string& arena, const std::string& name, Power power);

        // Each tick: the fans of the live arenas let out their sparks (enabled or not).
        void Update();

        // The vertical speed (u/s) for something at point going up or down at
        // vz, or NaN when no fan of the arena holds it (updraft_query).
        float Query(const std::string& arena, Vector3 point, float vz, int mask, float dt);

    private:
        // The DTI records fans stand on.
        static constexpr uint FanHotspot = 7;
        // The bottom of the box is lowered by half a unit.
        static constexpr float BoxDrop = 0.5f;
        // The fan whose strength operand is the time to rise through the box (every level's).
        static constexpr int TypeTimed = 6;
        // Bit 0 of the mask: enabled (fan_enable).
        static constexpr int Enabled = 1;
        // A spark with a chance of 1 in 8 a tick, just above the bottom of the box.
        static constexpr int SparkChance = 8;
        static constexpr float SparkLift = 0.25f;
        // Type 6 fans lift at full strength up to 5 units below their top, then less and less
        // (0.2 per unit); the box reaches 5 units above the top.
        static constexpr float Top = 5.0f;
        static constexpr float TopFade = 0.2f;
        // In the last 2 units a wobble of ±2 u/s (0.1 per query) is added (0x490d7c).
        static constexpr float WobbleZone = 2.0f;
        static constexpr float WobbleLimit = 2.0f;
        static constexpr float WobbleStep = 0.1f;
        // The vertical speed rises by (target + 64) × dt towards the fan's speed.
        static constexpr float LiftAcceleration = 64.0f;

        class Fan
        {
        public:
            std::string Name;
            std::string Arena;
            int Param = 0;
            int Type = 0;
            // Upward speed at full strength (u/s).
            float Strength = 0.0f;
            int Mask = -1;
            Vector3 BoxStart;
            Vector3 BoxEnd;
        };

        std::vector<Formats::Dti::ArenaEntry> _arenas;
        std::shared_ptr<Random> _rng;
        std::vector<std::shared_ptr<Fan>> _fans;
        float _wobble = 0.0f;
        bool _wobbleUp = false;

        static bool Holds(const Fan& fan, Vector3 point);

        // One fan's effect (0x413d14): a target speed by type and height, approached at
        // (target + 64) u/s².
        float Lift(const Fan& fan, float z, float vz, float dt);

        // Swings the shared wobble between ±2 by 0.1 per call.
        float Wobble();
    };
}
