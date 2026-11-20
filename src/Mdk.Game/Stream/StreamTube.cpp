#include "Mdk.Game/Stream/StreamTube.h"

namespace Mdk::Game::Stream
{
    using Game::Kurt::Difficulty;

    StreamTube::StreamTube(int index, Difficulty difficulty, Kind kind, std::shared_ptr<WatcomRandom> random)
        : _kind(kind), _random(std::move(random))
    {
        auto half = index >> 1;
        MaxTurn = index + TurnBase[static_cast<int>(difficulty)];
        MinRadius = StartRadius - half;
        switch (difficulty)
        {
        case Difficulty::Easy:
            MaxRadius = 17.f - half;
            break;
        case Difficulty::Normal:
            MaxRadius = 17.f - index;
            break;
        default:
            MaxRadius = 13.f - half;
            break;
        }

        BuildRamp();
        for (auto i = 0; i < Rings; i++)
        {
            _bases[i] = TubeBasis::Identity();
            _points[i] = {};
            _colours[i] = {};
            _normals[i] = {};
            _distances[i] = {};
        }

        // 31 rings ahead of Kurt.
        _shadeFrom = _random->Next() & (RampSize - 1);
        _shadeTo = _random->Next() & (RampSize - 1);
        for (auto i = 0; i < Rings - 1; i++)
        {
            Generate();
        }
    }

    void StreamTube::BuildRamp()
    {
        auto previous = RampStart;
        auto k = 0;
        for (const auto& key : RampKeys)
        {
            for (auto i = 0; i < RampSteps; i++)
            {
                auto blend = [i](int to, int from) { return (to * i + from * (RampSteps - i)) / RampSteps; };
                _ramp[k++] = Rgb{static_cast<byte>(blend(key.R, previous.R)), static_cast<byte>(blend(key.G, previous.G)), static_cast<byte>(blend(key.B, previous.B))};
            }

            previous = key;
        }
    }

    void StreamTube::Advance()
    {
        Tail++;
        Generate();
    }

    std::vector<StreamTube::Spawn> StreamTube::TakeSpawns()
    {
        auto spawns = std::move(_spawns);
        _spawns = {};
        return spawns;
    }

    bool StreamTube::Contains(float t) const
    {
        auto s = ToInt(MathF::Floor(t));
        return s >= Tail && s < Head;
    }

    Vector3 StreamTube::Centre(float t) const
    {
        auto s = ToInt(MathF::Floor(t));
        return Vector3::Lerp(_origins[s & SlotMask], _origins[(s + 1) & SlotMask], t - s);
    }

    TubeBasis StreamTube::Frame(float t, Vector3 up) const
    {
        auto y = Vector3::Normalize(Centre(t + 1.f) - Centre(t));
        auto x = Vector3::Normalize(Vector3::Cross(y, up));
        return TubeBasis{x, y, Vector3::Cross(x, y)};
    }

    Vector3 StreamTube::Place(float t, float x, float z) const
    {
        auto s = ToInt(MathF::Floor(t));
        auto slot = s & SlotMask;
        return _origins[slot] + _bases[slot].Apply(Vector3(x, SegmentLength * (t - s), z));
    }

    float StreamTube::WallHit(Vector3 position, float t, float margin) const
    {
        auto slot = ToInt(MathF::Floor(t)) & SlotMask;
        const auto& normals = _normals[slot];
        const auto& distances = _distances[slot];
        for (size_t i = 0; i < normals.size(); i++)
        {
            auto d = Vector3::Dot(normals[i], position) + distances[i] - margin;
            if (d > 0.f)
            {
                continue;
            }

            auto towards = Vector3::Dot(normals[i], position - Centre(t));
            return towards != 0.f ? d / towards : 0.f;
        }

        return -1.f;
    }

    std::vector<int> StreamTube::DrawnSegments() const
    {
        std::vector<int> segments;
        for (auto n = Head - 2; n >= Tail; n--)
        {
            segments.push_back(n);
        }

        return segments;
    }

    void StreamTube::AddTriangles(int n, Vector3 eye, std::vector<TubeVertex>& into) const
    {
        auto slot = n & SlotMask;
        const auto& older = _points[slot];
        const auto& newer = _points[(n + 1) & SlotMask];
        auto olderLevel = LevelOf(n);
        auto newerLevel = LevelOf(n + 1);

        // The newest ring has no colours yet: the first ramp colour at the farthest level.
        auto newest = n + 2 >= Head;
        auto olderColour = [&](int j) { return ColourOf(n, j); };
        auto newerColour = [&](int j) { return newest ? 0 : ColourOf(n + 1, j); };
        if (newest)
        {
            newerLevel = FarLevel();
        }

        const auto& normals = _normals[slot];
        const auto& distances = _distances[slot];
        for (auto j = 0; j < Points; j++)
        {
            auto j1 = (j + 1) % Points;
            for (auto i = 0; i < 2; i++)
            {
                auto plane = j * 2 + i;
                if (Vector3::Dot(normals[plane], eye) + distances[plane] < 0.f)
                {
                    continue;
                }

                into.push_back(TubeVertex{older[j], olderColour(j), olderLevel});
                if (i == 0)
                {
                    into.push_back(TubeVertex{newer[j1], newerColour(j1), newerLevel});
                    into.push_back(TubeVertex{older[j1], olderColour(j1), olderLevel});
                    continue;
                }

                into.push_back(TubeVertex{newer[j], newerColour(j), newerLevel});
                into.push_back(TubeVertex{newer[j1], newerColour(j1), newerLevel});
            }
        }
    }

    Rgba8 StreamTube::Rgba(const TubeVertex& v) const
    {
        const auto& c = _ramp[v.Colour];
        return Rgba8{c.R, c.G, c.B, static_cast<byte>(Alpha[v.Level])};
    }

    int StreamTube::ColourOf(int n, int j) const
    {
        const auto& colours = _colours[n & SlotMask];
        return j < static_cast<int>(colours.size()) ? colours[j] : 0;
    }

    void StreamTube::Generate()
    {
        auto n = Head;
        if (_kind == Kind::Gunter && n > LastSegment)
        {
            SpawnPlanet(n);
            return;
        }

        auto slot = n & SlotMask;
        if (n == Tail)
        {
            _bases[slot] = TubeBasis::Identity();
            _origins[slot] = Vector3::Zero();
        }
        else
        {
            TurnRing(n);
            SpawnLight(n);
        }

        MakePoints(slot);
        if (n != Tail)
        {
            BuildSegment(n - 1);
        }

        Head++;
        WalkAngles();
    }

    void StreamTube::WalkAngles()
    {
        for (auto axis : {2, 0, 1})
        {
            auto angle = static_cast<double>(_angles[axis]);
            if (_kind == Kind::Gunter && Head > StraightenSegment)
            {
                angle = MoveToward(angle, 0.0, 1.0);
            }
            else
            {
                angle += (_random->Next() - RandHalf) * WalkStep;
            }

            if (Math::Abs(angle) > MaxTurn)
            {
                angle *= TurnDamping;
            }

            _angles[axis] = static_cast<float>(angle);
        }

        _radius = Math::Clamp<double>(_radius + (_random->Next() - RandHalf) * WalkStep, MinRadius, MaxRadius);
    }

    void StreamTube::TurnRing(int n)
    {
        auto slot = n & SlotMask;
        auto previous = (n - 1) & SlotMask;
        auto turn = TubeBasis::Rotation(Vector3::UnitX(), _angles[0]) * TubeBasis::Rotation(Vector3::UnitY(), _angles[1])
            * TubeBasis::Rotation(Vector3::UnitZ(), _angles[2]);
        auto basis = _bases[previous] * turn;
        _origins[slot] = _origins[previous] + SegmentLength * _bases[previous].Y;
        _bases[slot] = TubeBasis{Vector3::Normalize(basis.X), Vector3::Normalize(basis.Y), Vector3::Normalize(basis.Z)};
    }

    void StreamTube::MakePoints(int slot)
    {
        std::vector<Vector3> points(Points);
        for (auto j = 0; j < Points; j++)
        {
            auto angle = Math::Tau * j / Points;
            auto jx = (_random->Next() - RandHalf + Jitter) / Jitter;
            auto jz = (_random->Next() - RandHalf + Jitter) / Jitter;
            auto local = Vector3(static_cast<float>(_radius * Math::Sin(angle) * jx), 0.f, static_cast<float>(_radius * Math::Cos(angle) * jz));
            points[j] = _origins[slot] + _bases[slot].Apply(local);
        }

        _points[slot] = std::move(points);
    }

    void StreamTube::SpawnLight(int n)
    {
        if ((_random->Next() & LightChance) == 0)
        {
            return;
        }

        auto size = LightSize + _random->Next() / SizeUnit;
        auto x = (_random->Next() - RandHalf) / OffsetUnit;
        auto z = (_random->Next() - RandHalf) / OffsetUnit;
        auto speed = LightSpeed - _random->Next() / SizeUnit;
        _spawns.push_back(Spawn{SpawnKind::Light, n - 1.f, static_cast<float>(x), static_cast<float>(z), static_cast<float>(size), static_cast<float>(speed)});
    }

    void StreamTube::SpawnPlanet(int n)
    {
        if (_planetSpawned)
        {
            return;
        }

        _planetSpawned = true;
        _spawns.push_back(Spawn{SpawnKind::Planet, static_cast<float>(n - PlanetBack), 0.f, 0.f, PlanetSize, 0.f});
    }

    void StreamTube::BuildSegment(int p)
    {
        auto slot = p & SlotMask;
        const auto& older = _points[slot];
        const auto& newer = _points[(p + 1) & SlotMask];
        auto shade = static_cast<int>((1.0 - _shade) * _shadeFrom + _shade * _shadeTo);
        std::vector<int> colours(Points);
        std::vector<Vector3> normals(PlanesPerSegment);
        std::vector<float> distances(PlanesPerSegment);
        for (auto j = 0; j < Points; j++)
        {
            // A triangle wave around the ring, one step further each segment: spiral stripes.
            auto x = (p + 1 + j) & SlotMask;
            auto wave = x < Points ? x : SlotMask - x;
            colours[j] = (wave + shade) & (RampSize - 1);

            auto j1 = (j + 1) % Points;
            SetPlane(normals, distances, j * 2, older[j], newer[j1], older[j1]);
            SetPlane(normals, distances, j * 2 + 1, older[j], newer[j], newer[j1]);
        }

        _colours[slot] = std::move(colours);
        _normals[slot] = std::move(normals);
        _distances[slot] = std::move(distances);

        // The shade drifts to a new random target every 10 segments.
        _shade += ShadeStep;
        if (_shade > 1.0)
        {
            _shade = 0.0;
            _shadeFrom = _shadeTo;
            _shadeTo = _random->Next() & (RampSize - 1);
        }
    }

    void StreamTube::SetPlane(std::vector<Vector3>& normals, std::vector<float>& distances, int i, Vector3 a, Vector3 b, Vector3 c)
    {
        auto normal = Vector3::Normalize(Vector3::Cross(b - a, c - b));
        normals[i] = normal;
        distances[i] = -Vector3::Dot(normal, a);
    }
}
