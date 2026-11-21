#pragma once

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "game/kurt/Inventory.h"
#include "game/stream/TubeBasis.h"
#include "game/stream/WatcomRandom.h"

namespace Mdk::Game::Stream
{
    // A corner of the tube's wall: its place, its colour in the ramp (0-63) and its distance
    // level (0-5, see StreamTube::Rgba).
    struct TubeVertex
    {
        Vector3 Position{};
        int Colour = 0;
        int Level = 0;

        bool operator==(const TubeVertex&) const = default;
    };

    // An RGB(A) colour of bytes.
    struct Rgb
    {
        byte R = 0;
        byte G = 0;
        byte B = 0;

        bool operator==(const Rgb&) const = default;
    };

    struct Rgba8
    {
        byte R = 0;
        byte G = 0;
        byte B = 0;
        byte A = 0;

        bool operator==(const Rgba8&) const = default;
    };

    // The stream's tube (0x434838; stream_tube.gd): a ring buffer of 32 rings of 16 points,
    // one ring per segment of 10 units, each turned from the previous one by three randomly walking
    // angles. The segment between two rings has 32 wall planes facing in, and a colour per point. See
    // godot-mdk docs/gameplay.md, "The stream".
    //   ring:   tail ... head - 1          (head - tail = 31 while generating)
    //   slot:   ring & 31
    //
    //         P[n+1][j] ---- P[n+1][j+1]      two triangles (and planes) per quad,
    //            |    \            |          32 per segment
    //         P[n][j] ------ P[n][j+1]
    class StreamTube
    {
    public:
        // The normal tube, or the one after LEVEL8 that straightens and ends at a planet.
        enum class Kind { Normal, Gunter };

        // What Advance asks the stream to create.
        enum class SpawnKind { Light, Planet };

        // A light or the planet: where along the tube (segments), across it, its size and speed
        // (segments/s).
        struct Spawn
        {
            SpawnKind Kind = SpawnKind::Light;
            float T = 0;
            float X = 0;
            float Z = 0;
            float Size = 0;
            float Speed = 0;

            bool operator==(const Spawn&) const = default;
        };

        static constexpr int Rings = 32;
        static constexpr int Points = 16;
        static constexpr float SegmentLength = 10.f;
        static constexpr int LastSegment = 186;
        // Colours: 64 ramp entries; the shade target changes every 10 segments.
        static constexpr int RampSize = 64;

        // The oldest ring still alive (≈ Kurt's segment), and the next ring to make.
        int Tail = 0;
        int Head = 0;
        float MaxTurn = 0;
        float MinRadius = 0;
        float MaxRadius = 0;

        // index: the level just played (0-4).
        StreamTube(int index, Game::Kurt::Difficulty difficulty, Kind kind, std::shared_ptr<WatcomRandom> random);

        // The 64 colours of the walls.
        const std::array<Rgb, RampSize>& Ramp() const { return _ramp; }

        // Kurt passed a segment: the oldest ring goes, a new one comes.
        void Advance();

        // The lights and the planet made since the last call.
        std::vector<Spawn> TakeSpawns();

        // Whether t (in segments) is within the rings still alive.
        bool Contains(float t) const;

        // The axis at t (0x436668).
        Vector3 Centre(float t) const;

        // The frame at t (0x4366f4): forward along the axis, right from the camera's up.
        TubeBasis Frame(float t, Vector3 up) const;

        // A point of segment t at (x, z) across it.
        Vector3 Place(float t, float x, float z) const;

        // The walls (0x43637c): for the first plane of segment t that position is within margin
        // of, the fraction of the way to the axis that puts it back; -1 when it touches none.
        float WallHit(Vector3 position, float t, float margin) const;

        // The segments drawn, far to near (0x436b00).
        std::vector<int> DrawnSegments() const;

        // The triangles of segment n facing eye.
        void AddTriangles(int n, Vector3 eye, std::vector<TubeVertex>& into) const;

        // A vertex's colour (0x436b00): its ramp colour and its level's alpha (of 256), the
        // RGBA table entry 64 level + colour, Gouraud-blended across the triangle.
        Rgba8 Rgba(const TubeVertex& v) const;

        // Ring n's centre, a point, a colour and a wall plane of its segment
        // (tests and the reference dump).
        Vector3 Origin(int n) const { return _origins[n & SlotMask]; }
        Vector3 Point(int n, int j) const { return _points[n & SlotMask][j]; }
        int Colour(int n, int j) const { return ColourOf(n, j); }
        std::pair<Vector3, float> Plane(int n, int i) const { return { _normals[n & SlotMask][i], _distances[n & SlotMask][i] }; }
        Vector3 Angles() const { return Vector3(_angles[0], _angles[1], _angles[2]); }
        float Radius() const { return static_cast<float>(_radius); }

    private:
        static constexpr int SlotMask = Rings - 1;
        static constexpr int PlanesPerSegment = Points * 2;
        static constexpr float StartRadius = 10.f;
        // The Gunter tube straightens after this segment and stops after the last one.
        static constexpr int StraightenSegment = 168;
        // (rand() - 0x4000) × WalkStep is ±1.
        static constexpr int RandHalf = 0x4000;
        static constexpr double WalkStep = 6.10352e-5;
        // Ring points are jittered by ±10 %: (rand() - 0x4000 + Jitter) / Jitter.
        static constexpr double Jitter = 163840.0;
        static constexpr float TurnDamping = 0.8f;
        static constexpr double ShadeStep = 0.1;
        // Vertex alpha (of 256) by distance from the tail (0x5744d8): levels 0-5 every 5 segments.
        static constexpr std::array<int, 6> Alpha = { 0x5A, 0x55, 0x50, 0x3C, 0x28, 0x0F };
        static constexpr int LevelSegments = 5;
        static constexpr int FarLevel() { return static_cast<int>(Alpha.size()) - 1; }

        struct RampColour
        {
            int R;
            int G;
            int B;
        };

        // The ramp (0x491ccc): a start colour, then 8 keys of 8 steps.
        static constexpr RampColour RampStart = { 222, 206, 90 };
        static constexpr std::array<RampColour, 8> RampKeys = { RampColour{ 140, 123, 33 }, RampColour{ 123, 49, 8 },
            RampColour{ 198, 165, 132 }, RampColour{ 132, 123, 140 }, RampColour{ 214, 198, 231 }, RampColour{ 198, 165, 132 },
            RampColour{ 123, 49, 8 }, RampColour{ 222, 206, 90 } };
        static constexpr int RampSteps = 8;
        // The difficulty's limits (0x433b50): max turn = index + base, radius 10 - index/2 to max.
        static constexpr std::array<float, 3> TurnBase = { 6.f, 8.f, 10.f };
        // Lights (0x434f64): in 3 segments of 4, up to ±4 units off the axis, 4-8 in size,
        // flying back at 3-7 segments/s. The planet (0x4350dc) is 3 segments before the end, size 36.
        static constexpr int LightChance = 3;
        static constexpr float LightSize = 4.f;
        static constexpr float LightSpeed = -3.f;
        static constexpr double SizeUnit = 8192.0;
        static constexpr double OffsetUnit = 4096.0;
        static constexpr int PlanetBack = 3;
        static constexpr float PlanetSize = 36.f;

        Kind _kind;
        std::shared_ptr<WatcomRandom> _random;
        std::array<TubeBasis, Rings> _bases{};
        std::array<Vector3, Rings> _origins{};
        std::array<std::vector<Vector3>, Rings> _points;
        std::array<std::vector<int>, Rings> _colours;
        std::array<std::vector<Vector3>, Rings> _normals;
        std::array<std::vector<float>, Rings> _distances;
        std::array<Rgb, RampSize> _ramp{};
        std::vector<Spawn> _spawns;
        // The turn per segment about x, y (forward: roll) and z, in degrees.
        std::array<float, 3> _angles{};
        double _radius = StartRadius;
        int _shadeFrom = 0;
        int _shadeTo = 0;
        double _shade = 0;
        bool _planetSpawned = false;

        // The ramp: each key blended from the previous one in 8 steps.
        void BuildRamp();

        // Ring n's distance level: 0 at the tail, one more every 5 segments.
        int LevelOf(int n) const { return n > Tail ? Math::Min((n - Tail - 1) / LevelSegments, FarLevel()) : 0; }

        int ColourOf(int n, int j) const;

        // Makes ring Head (0x434838): its frame follows the previous one turned by the three
        // angles, its points are jittered, and the segment before it gets its wall planes and colours.
        void Generate();

        // The angles walk randomly by ±1° per segment (the Gunter tube straightens at the end),
        // in the order z, x, y; then the radius by ±1.
        void WalkAngles();

        static double MoveToward(double from, double to, double step)
        {
            return Math::Abs(to - from) <= step ? to : from + Math::Sign(to - from) * step;
        }

        // Ring n's frame: one segment along the previous ring's forward axis, turned by the
        // angles, its columns normalised.
        void TurnRing(int n);

        // The ring's 16 points around its axis (point 0 on top), each coordinate jittered by ±10 %.
        void MakePoints(int slot);

        // A light in the segment before ring n, in 3 segments of 4.
        void SpawnLight(int n);

        // The planet at the end of the Gunter tube, once.
        void SpawnPlanet(int n);

        // The wall planes (two per quad, facing in) and the colours of the segment from ring
        // p to p + 1.
        void BuildSegment(int p);

        static void SetPlane(std::vector<Vector3>& normals, std::vector<float>& distances, int i, Vector3 a, Vector3 b, Vector3 c);
    };
}
