#include "Mdk.Game/Scripts/Debris.h"

#include <cmath>
#include <limits>
#include <span>

#include "Mdk.Formats/Model.h"
#include "Mdk.Game/FrameBlend.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ScriptMath.h"

namespace Mdk::Game::Scripts
{
    using Objects::MdkObject;

    Vector3 Debris::Piece::CenterAt(float alpha) const
    {
        return _ticked.has_value() ? FrameBlend::Lerp(_ticked->Center, Center, alpha) : Center;
    }

    Quaternion Debris::Piece::OrientationAt(float alpha) const
    {
        return _ticked.has_value() && alpha < 1.0f ? Quaternion::Slerp(_ticked->Orientation, Orientation, alpha) : Orientation;
    }

    void Debris::Piece::Reset()
    {
        Arena = "";
        Names = nullptr;
        Materials.clear();
        Corners.clear();
        Uvs.clear();
        ColourBase = 0;
        ColourRange = 0;
        TrailEvery = 0;
        TrailTicks = 0;
        Ticks = 0;
        Center = Vector3::Zero();
        Velocity = Vector3::Zero();
        Orientation = Quaternion::Identity();
        Spin = Quaternion::Identity();
        _ticked.reset();
    }

    Debris::Debris(std::shared_ptr<Random> rng)
        : rng(std::move(rng))
    {
        for (auto i = 0; i < MaxPieces; i++)
        {
            _free.push(std::make_shared<Piece>());
        }
    }

    std::shared_ptr<Debris::Piece> Debris::Take()
    {
        if (_free.empty())
        {
            return std::make_shared<Piece>();
        }

        auto piece = _free.top();
        _free.pop();
        piece->Reset();
        return piece;
    }

    void Debris::Shatter(const std::string& arena, int group, float life, float size, float speed, Vector3 point, Vector3 direction)
    {
        auto shape = GroupFacets ? GroupFacets(arena, group) : std::nullopt;
        if (!shape.has_value() || shape->Facets.empty())
        {
            return;
        }

        const auto& facets = shape->Facets;

        // The squared distance to the farthest corner of the group's box, axis by axis.
        auto min = facets[0].A;
        auto max = min;
        for (const auto& f : facets)
        {
            min = Vector3::Min(min, Vector3::Min(f.A, Vector3::Min(f.B, f.C)));
            max = Vector3::Max(max, Vector3::Max(f.A, Vector3::Max(f.B, f.C)));
        }

        auto shot = Shot{arena, shape->Names, size * size * 0.25f, life, speed, point,
            direction == Vector3::Zero() ? Vector3::Zero() : Vector3::Normalize(direction),
            Vector3::Max(Vector3::Abs(min - point), Vector3::Abs(max - point)).LengthSquared()};
        for (const auto& f : facets)
        {
            std::array<Vector3, 3> c = {f.A, f.B, f.C};
            std::array<Vector2, 3> uv = {f.UvA, f.UvB, f.UvC};
            Split(c, uv, f.Material, shot);
        }
    }

    void Debris::Split(std::span<Vector3> c, std::span<Vector2> uv, int material, const Shot& shot)
    {
        if (static_cast<int>(_pieces.size()) >= MaxPieces)
        {
            return;
        }

        if (Vector3::Cross(c[1] - c[0], c[1] - c[2]).LengthSquared() > shot.MaxCross)
        {
            auto a = Longest(c);
            auto b = (a + 1) % 3;
            auto o = (a + 2) % 3;
            auto middle = (c[a] + c[b]) * 0.5f;
            auto middleUv = (uv[a] + uv[b]) * 0.5f;
            std::array<Vector3, 3> first = {c[a], middle, c[o]};
            std::array<Vector2, 3> firstUv = {uv[a], middleUv, uv[o]};
            Split(first, firstUv, material, shot);
            std::array<Vector3, 3> second = {middle, c[b], c[o]};
            std::array<Vector2, 3> secondUv = {middleUv, uv[b], uv[o]};
            Split(second, secondUv, material, shot);
            return;
        }

        auto piece = Take();
        piece->Arena = shot.Arena;
        piece->Names = shot.Names;
        piece->Center = (c[0] + c[1] + c[2]) / 3.0f;
        piece->Materials.push_back(material);
        for (auto corner : c)
        {
            piece->Corners.push_back(corner - piece->Center);
        }

        piece->Uvs.insert(piece->Uvs.end(), uv.begin(), uv.end());

        // Slower farther from the point.
        auto f = shot.Far > 0.0f ? 1.0f - Vector3::DistanceSquared(piece->Center, shot.Point) / shot.Far : 1.0f;
        if (shot.Direction != Vector3::Zero())
        {
            piece->Velocity = shot.Direction * shot.Speed * f;
        }
        else if (piece->Center == shot.Point)
        {
            piece->Velocity = Vector3(0.0f, 0.0f, shot.Speed * f);
        }
        else
        {
            piece->Velocity = Vector3::Normalize(piece->Center - shot.Point) * shot.Speed * f;
        }

        piece->Spin = RandomSpin(ShatterSpinDegrees * f);
        piece->Ticks = ToInt(MathF::Round(shot.Life * TicksPerSecond));
        _pieces.push_back(piece);
    }

    int Debris::Longest(std::span<const Vector3> c)
    {
        auto longest = 0;
        for (auto k = 1; k < 3; k++)
        {
            if (Vector3::DistanceSquared(c[(k + 1) % 3], c[k]) > Vector3::DistanceSquared(c[(longest + 1) % 3], c[longest]))
            {
                longest = k;
            }
        }

        return longest;
    }

    void Debris::Spark(const std::string& arena, Vector3 point, int count, float size, int colour, int range, float speed, Launch launch)
    {
        for (auto i = 0; i < count && static_cast<int>(_pieces.size()) < MaxPieces; i++)
        {
            auto piece = Take();
            piece->ColourBase = colour;
            piece->ColourRange = range;
            auto s = (1.0f + RandomHalf() * Rand15) * size;
            auto& corners = _sparkCorners;
            for (auto c = 0; c < static_cast<int>(SparkCorners.size()); c++)
            {
                auto x = static_cast<float>(RandomHalf());
                auto y = static_cast<float>(RandomHalf());
                auto z = static_cast<float>(RandomHalf());
                corners[c] = (SparkCorners[c] + Vector3(x, y, z) * CornerJitter) * s;
            }

            for (auto k : SparkFaces)
            {
                piece->Corners.push_back(corners[k]);
            }

            auto x = RandomHalf() * Rand14;
            auto y = RandomHalf() * Rand14;
            auto z = RandomHalf() * Rand15;
            piece->Center = point + Vector3(x, y, z);
            Start(*piece, arena, speed);

            // The fans' sparks stand exactly at their point (0x414230 sets it after 0x4052d4).
            if (launch == Launch::Still)
            {
                piece->Velocity = Vector3::Zero();
                piece->Center = point;
            }

            // Only the bigger sparks can trail.
            if (s < 1.0f)
            {
                piece->TrailEvery = 0;
            }

            _pieces.push_back(piece);
        }
    }

    void Debris::BreakUp(MdkObject& obj, const Formats::Model& model)
    {
        auto turn = Matrix4x4::CreateRotationX(ScriptMath::ToRadians(obj.Roll)) * Matrix4x4::CreateRotationZ(ScriptMath::ToRadians(obj.Yaw));
        auto velocity = obj.Position - obj.PreviousPosition;
        // C# shares the model's list; one copy shared by this break-up's pieces.
        auto names = std::make_shared<const std::vector<std::string>>(model.Materials);
        for (const auto& part : model.PartList)
        {
            if (static_cast<int>(_pieces.size()) >= MaxPieces)
            {
                return;
            }

            auto index = obj.FindPart(part->Name);
            if (index >= 0 && (obj.HiddenParts & (1 << (index & 31))) != 0)
            {
                continue;
            }

            auto piece = Take();
            piece->Names = names;
            piece->Materials.insert(piece->Materials.end(), part->TriangleMaterials.begin(), part->TriangleMaterials.end());
            for (auto i : part->TriangleIndices)
            {
                piece->Corners.push_back(Vector3::Transform(part->Vertices[i], turn));
            }

            piece->Uvs.insert(piece->Uvs.end(), part->TriangleUvs.begin(), part->TriangleUvs.end());
            piece->Center = obj.Position + Vector3(0.0f, 0.0f, BreakUpRise);
            Start(*piece, obj.Arena, 1.0f);
            piece->Velocity += velocity;
            _pieces.push_back(piece);
        }
    }

    void Debris::Start(Piece& piece, const std::string& arena, float speed)
    {
        piece.Arena = arena;
        auto x = RandomHalf() * Rand14;
        auto y = RandomHalf() * Rand14;
        auto z = (rng->Next(RandRange) - LaunchUp) * Rand14;
        piece.Velocity = Vector3(x, y, z) * speed;
        piece.Spin = RandomSpin(SpinDegrees);
        piece.Ticks = (rng->Next(RandRange) >> LifeShift) + LifeMin;
        if (rng->Next(TrailChance) == 0)
        {
            piece.TrailEvery = rng->Next(2) + 1;
        }
    }

    Quaternion Debris::RandomSpin(float degrees)
    {
        auto angle = [&] { return ScriptMath::ToRadians((rng->Next(RandRange) - SpinBias) * degrees); };
        auto yaw = angle();
        auto pitch = angle();
        auto roll = angle();
        return Quaternion::CreateFromYawPitchRoll(yaw, pitch, roll);
    }

    int Debris::RandomHalf()
    {
        return rng->Next(RandRange) - RandHalf;
    }

    bool Debris::HasPieces(const std::string& arena) const
    {
        for (const auto& piece : _pieces)
        {
            if (piece->Arena == arena)
            {
                return true;
            }
        }

        return false;
    }

    void Debris::Update(float ticks)
    {
        for (auto i = static_cast<int>(_pieces.size()) - 1; i >= 0; i--)
        {
            auto piece = _pieces[i];
            piece->Remember();
            piece->Orientation = Quaternion::Normalize(piece->Orientation * piece->Spin);
            Move(*piece, ticks);
            piece->Ticks -= ToInt(MathF::Round(ticks));
            if (piece->Ticks <= 0)
            {
                _pieces.erase(_pieces.begin() + i);
                _free.push(piece);
                continue;
            }

            // The smoke trail (0x406070).
            if (piece->TrailEvery == 0 || !Trail)
            {
                continue;
            }

            piece->TrailTicks += ToInt(MathF::Round(ticks));
            if (piece->TrailTicks >= piece->TrailEvery)
            {
                piece->TrailTicks = 0;
                Trail(piece->Arena, piece->Center);
            }
        }
    }

    void Debris::Move(Piece& piece, float ticks)
    {
        auto motion = piece.Velocity * ticks;
        auto hit = motion != Vector3::Zero() && Ray ? Ray(piece.Arena, piece.Center, piece.Center + motion) : std::nullopt;
        if (hit.has_value() && Vector3::DistanceSquared(hit->Point, piece.Center) > OnSurface * OnSurface)
        {
            piece.Center = hit->Point;
            piece.Velocity -= hit->Normal * Vector3::Dot(piece.Velocity, hit->Normal) * Bounce;
            piece.Ticks -= BounceTicks;
        }
        else
        {
            piece.Center += motion;
            piece.Velocity.Z -= Gravity * ticks;
        }

        // Fans push sparks and pieces (mask 8); their speeds are in units per second.
        auto vz = Updraft ? Updraft(piece.Arena, piece.Center, piece.Velocity.Z * TicksPerSecond, ticks / TicksPerSecond)
                          : std::numeric_limits<float>::quiet_NaN();
        if (!std::isnan(vz))
        {
            piece.Velocity.Z = vz / TicksPerSecond;
        }
    }
}
