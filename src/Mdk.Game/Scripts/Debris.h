#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Game/Scripts/RayHit.h"

namespace Mdk::Formats
{
    class Model;
}

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    // Flying pieces (a port of godot-mdk's debris.gd): shattered triangle groups
    // (shatter_group, 0x40c828), sparks (0x4052d4: small tetrahedra in palette colours) and the parts of
    // an object's break-up model (0x405900). They fly, spin, fall, bounce off the arena, ride the fans'
    // updrafts and vanish after their lifetime (0x4061d8); some leave a smoke trail (0x406004).
    //
    //   shatter ─┐                       ┌─► hit: bounce (v -= 1.4 (v·n) n), life -20
    //   spark   ─┼─► piece ─► each tick ─┤
    //   break up ┘                       └─► free: fall, updraft, trail puff
    class Debris
    {
    public:
        // Gravity in units per tick² (about 64 units/s²).
        static constexpr float Gravity = 0.284444f * 0.25f;

    private:
        // Speed kept along the surface normal when bouncing (restitution 0.4).
        static constexpr float Bounce = 1.4f;
        // Life lost at each bounce, in ticks.
        static constexpr int BounceTicks = 20;

    public:
        // Most pieces alive at once (the original shares a pool of effects).
        static constexpr int MaxPieces = 600;
        // Sparks and pieces live 60-123 ticks; one in 4 leaves a smoke trail every 1-2 ticks.
        static constexpr int LifeMin = 60;

    private:
        static constexpr int TrailChance = 4;
        static constexpr int LifeShift = 9;
        // rand() is 0-32767; centred values are rand() - 0x4000.
        static constexpr int RandRange = 32768;
        static constexpr int RandHalf = 0x4000;
        static constexpr float Rand14 = 1.0f / 16384.0f;
        static constexpr float Rand15 = 1.0f / 32768.0f;
        // Upward bias of a spark's launch (rand() - 0x800).
        static constexpr int LaunchUp = 0x800;
        // Spin: about ±14° per tick (0x46de70); a shatter's spin scales by 0.000854492.
        static constexpr int SpinBias = 0x41c2;
        static constexpr float SpinDegrees = 28.0f / RandRange;
        static constexpr float ShatterSpinDegrees = 0.000854492f;
        // A spark's corners are jittered by ±0.33 before scaling.
        static constexpr float CornerJitter = 2e-5f;
        // Pieces start 2 units above the exploding object.
        static constexpr float BreakUpRise = 2.0f;
        static constexpr float TicksPerSecond = 30.0f;
        // A hit this close to the start is the surface the piece rests on: a segment starting
        // on a plane crosses nothing (0x421470), so the piece goes on through it.
        static constexpr float OnSurface = 1e-3f;
        // A spark's tetrahedron (0x404b00) and its 4 faces.
        static constexpr std::array<Vector3, 4> SparkCorners = {
            Vector3(0.0f, 0.0f, 0.5f), Vector3(0.5f, 0.0f, -0.5f), Vector3(-0.5f, 0.5f, -0.5f), Vector3(-0.5f, -0.5f, -0.5f)};
        static constexpr std::array<int, 12> SparkFaces = {0, 2, 1, 0, 3, 2, 0, 1, 3, 1, 2, 3};

    public:
        // How a spark starts: flying off at random, or still at its point (the fans' sparks).
        enum class Launch { Random, Still };

        // A triangle of a shattered group: corners, UVs (texels) and material value.
        struct Facet
        {
            Vector3 A;
            Vector3 B;
            Vector3 C;
            Vector2 UvA;
            Vector2 UvB;
            Vector2 UvC;
            int Material = 0;

            bool operator==(const Facet&) const = default;
        };

        // A triangle group's facets and the material names their values index.
        // Names is shared with the pieces it makes (C# shares the list).
        struct Group
        {
            std::shared_ptr<const std::vector<std::string>> Names;
            std::vector<Facet> Facets;
        };

        // A flying piece: triangles around its centre (3 corners each), textured by material
        // values indexing Names, or a spark (no names) in palette colours
        // ColourBase + ColourRange × facing.
        class Piece
        {
        public:
            std::string Arena;
            // A triangle group's facets and the material names their values index.
            std::shared_ptr<const std::vector<std::string>> Names;
            std::vector<int> Materials;
            std::vector<Vector3> Corners;
            std::vector<Vector2> Uvs;
            int ColourBase = 0;
            int ColourRange = 0;
            // A smoke puff every this many ticks (0: none).
            int TrailEvery = 0;
            int TrailTicks = 0;
            Vector3 Center;
            // Units per tick.
            Vector3 Velocity;
            Quaternion Orientation = Quaternion::Identity();
            // Turn per tick.
            Quaternion Spin = Quaternion::Identity();
            int Ticks = 0;

            bool IsSpark() const { return Names == nullptr; }

            // A tick starts: frames until the next are drawn from here to the tick's.
            void Remember() { _ticked = Ticked{Center, Orientation}; }

            // The place and turn alpha of the way from the last tick's
            // start to now (a frame between ticks).
            Vector3 CenterAt(float alpha) const;

            Quaternion OrientationAt(float alpha) const;

            // Back to a new piece's state (taken again from the pool).
            void Reset();

        private:
            struct Ticked
            {
                Vector3 Center;
                Quaternion Orientation;
            };

            std::optional<Ticked> _ticked;
        };

        // All pieces are made with the level (MaxPieces), then reused.
        explicit Debris(std::shared_ptr<Random> rng);

        // The arena's nearest hit along a segment: (arena, from, to).
        std::function<std::optional<RayHit>(const std::string&, Vector3, Vector3)> Ray;
        // Leaves a smoke puff (see SpawnTrail).
        std::function<void(const std::string&, Vector3)> Trail;
        // The fans' push: (arena, point, vertical speed u/s, seconds) → the new speed, or NaN.
        std::function<float(const std::string&, Vector3, float, float)> Updraft;
        // The triangles of an arena's group, or null.
        std::function<std::optional<Group>(const std::string&, int)> GroupFacets;

        const std::vector<std::shared_ptr<Piece>>& Pieces() const { return _pieces; }

        // Shatters a triangle group: life in seconds, pieces no larger than
        // size / 4, speed (units per tick) at point, falling off to 0
        // at the group's farthest corner; the pieces fly along direction, or away from
        // the point when it's zero.
        void Shatter(const std::string& arena, int group, float life, float size, float speed, Vector3 point, Vector3 direction);

        // Sparks (0x41e8f4, 0x4052d4) at point: size 0.5 or
        // 1.0, palette colours colour to colour + range (e.g. 3, 3:
        // green), velocity scaled by speed.
        void Spark(const std::string& arena, Vector3 point, int count, float size, int colour, int range, float speed = 1.0f,
            Launch launch = Launch::Random);

        // An object's break-up (0x405900): one piece per part of its break-up model, turned like
        // the object (yaw and bank) 2 units higher, thrown with its velocity plus a spark's; parts hidden
        // on the object stay behind.
        void BreakUp(Objects::MdkObject& obj, const Formats::Model& model);

        // Whether an arena has any piece or spark.
        bool HasPieces(const std::string& arena) const;

        int PieceCount() const { return static_cast<int>(_pieces.size()); }

        // Moves the pieces by ticks (0x4061d8).
        void Update(float ticks);

    private:
        // A shatter's settings; a zero Direction means radial.
        struct Shot
        {
            std::string Arena;
            std::shared_ptr<const std::vector<std::string>> Names;
            float MaxCross = 0.0f;
            float Life = 0.0f;
            float Speed = 0.0f;
            Vector3 Point;
            Vector3 Direction;
            float Far = 0.0f;
        };

        std::shared_ptr<Random> rng;
        std::stack<std::shared_ptr<Piece>> _free;
        // A spark's corners before they go into its piece.
        std::array<Vector3, SparkCorners.size()> _sparkCorners{};
        std::vector<std::shared_ptr<Piece>> _pieces;

        std::shared_ptr<Piece> Take();

        // Splits a triangle at the middle of its longest side until it's small enough (0x40cbe0),
        // then makes it a piece.
        void Split(std::span<Vector3> c, std::span<Vector2> uv, int material, const Shot& shot);

        // The corner starting the longest side.
        static int Longest(std::span<const Vector3> c);

        // A spark's start (0x4052d4): thrown mostly upwards (±1, ±1, -0.125...1.875 units per
        // tick), a random spin, 60-123 ticks of life, one in 4 trailing smoke.
        void Start(Piece& piece, const std::string& arena, float speed);

        // A turn of (rand() - 0x41c2) × degrees about each axis.
        Quaternion RandomSpin(float degrees);

        int RandomHalf();

        // Flies freely (falling) or bounces off what it hits, stopping at the contact; the fans
        // lift it either way (0x4061d8).
        void Move(Piece& piece, float ticks);
    };
}
