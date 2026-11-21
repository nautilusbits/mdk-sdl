#include "game/objects/MdkObject.h"

#include <cmath>
#include <limits>

#include "core/DotNet.h"
#include "game/Angles.h"
#include "game/FrameBlend.h"
#include "game/scripts/ScriptMath.h"
#include "game/scripts/ScriptRuntime.h"

namespace Mdk::Game::Objects
{
    using Formats::Box;
    using Formats::Model;
    using Formats::ModelAnimation;

    std::map<MdkObject::ModelKey, std::vector<MdkObject::Parts>> MdkObject::Baked;
    std::map<MdkObject::ModelKey, std::vector<std::vector<std::optional<Box>>>> MdkObject::BoundsCache;
    std::map<std::shared_ptr<Model>, MdkObject::Parts> MdkObject::RestPoses;
    std::mutex MdkObject::BakedLock;
    std::mutex MdkObject::BoundsLock;

    std::optional<Collision::Solids::Footing> MdkObject::Footing() const
    {
        // A wall: no 0x10, 0x800 (damp_collide_move 0x465e34); a floor: 0x100, no 0x10
        // (damp_platform_floor 0x41d2c4). E.g. the ridden snowboard (0x800900): a floor only.
        auto wall = (Flags & (FlagNotSolid | FlagNotSolid2)) == 0;
        auto floor = (Flags & (FlagNotSolid | FlagPlatform)) == FlagPlatform;
        if (wall)
        {
            return floor ? Collision::Solids::Footing::Platform : Collision::Solids::Footing::Wall;
        }

        if (floor)
        {
            return Collision::Solids::Footing::Floor;
        }

        return std::nullopt;
    }

    Matrix4x4 MdkObject::RotationOf(float roll, float pitch, float yaw)
    {
        return Matrix4x4::CreateRotationX(DegreesToRadians(roll))
            * Matrix4x4::CreateRotationY(-DegreesToRadians(pitch))
            * Matrix4x4::CreateRotationZ(DegreesToRadians(yaw));
    }

    Matrix4x4 MdkObject::Transform() const
    {
        return Matrix4x4::CreateScale(Scale) * ((Flags & FlagRolling) != 0 ? RollingBasis : Rotation()) * Matrix4x4::CreateTranslation(Position + Vector3::UnitZ() * Lift());
    }

    void MdkObject::Remember()
    {
        _ticked = Ticked{Position, Yaw, Pitch, Roll, Arena, Animation, AnimationTime};
    }

    bool MdkObject::Blends(float alpha) const
    {
        return alpha < 1.f && _ticked.has_value() && _ticked->Arena == Arena && !FrameBlend::Jumped(_ticked->Position, Position, Scripts::ScriptRuntime::Tick);
    }

    Vector3 MdkObject::PositionAt(float alpha) const
    {
        return Blends(alpha) ? FrameBlend::Lerp(_ticked->Position, Position, alpha) : Position;
    }

    float MdkObject::YawAt(float alpha) const
    {
        return Blends(alpha) ? FrameBlend::LerpAngle(_ticked->Yaw, Yaw, alpha) : Yaw;
    }

    Matrix4x4 MdkObject::TransformAt(float alpha) const
    {
        if (!Blends(alpha))
        {
            return Transform();
        }

        const auto& from = *_ticked;
        auto rotation = (Flags & FlagRolling) != 0
            ? RollingBasis
            : RotationOf(FrameBlend::LerpAngle(from.Roll, Roll, alpha), FrameBlend::LerpAngle(from.Pitch, Pitch, alpha), FrameBlend::LerpAngle(from.Yaw, Yaw, alpha));
        return Matrix4x4::CreateScale(Scale) * rotation * Matrix4x4::CreateTranslation(PositionAt(alpha) + Vector3::UnitZ() * Lift());
    }

    std::optional<MdkObject::PoseFrames> MdkObject::FramesAt(float alpha) const
    {
        if (!Blends(alpha) || Model == nullptr || Animation == nullptr || Animation->FrameCount <= 0 || _ticked->Animation != Animation)
        {
            return std::nullopt;
        }

        const auto& animation = *Animation;
        auto count = animation.FrameCount;
        auto from = _ticked->AnimationTime;
        auto to = AnimationTime;
        if (to < from && (Flags & FlagLoop) != 0)
        {
            to += count;
        }

        if (to < from || ToInt(MathF::Round(AnimationTime)) % count != AnimationFrame)
        {
            return std::nullopt;
        }

        auto time = FrameBlend::Lerp(from, to, alpha);
        auto frame = ToInt(time);
        const auto& frames = Bake(Model, Animation);
        return PoseFrames{&frames[frame % count], &frames[(frame + 1) % count], time - frame};
    }

    void MdkObject::SetRolling(Rolling rolling)
    {
        if (rolling == Rolling::Off)
        {
            Flags &= ~FlagRolling;
            return;
        }

        if ((Flags & FlagRolling) == 0)
        {
            Flags |= FlagRolling;
            RollingBasis = Rotation();
        }
    }

    void MdkObject::PlayAnimation(const std::shared_ptr<ModelAnimation>& animation, Looping looping)
    {
        if (looping == Looping::Loop)
        {
            Flags |= FlagLoop;
        }
        else
        {
            Flags &= ~FlagLoop;
        }

        if (animation == Animation && AnimationEndFrame != AnimationEnded)
        {
            if (AnimationEndFrame >= 0)
            {
                AnimationEndFrame = NoHoldFrame;
            }

            return;
        }

        Animation = animation;
        AnimationTime = 0.f;
        AnimationFrame = 0;
        AnimationEndFrame = animation != nullptr ? NoHoldFrame : AnimationEnded;
    }

    void MdkObject::RestartAnimation(const std::shared_ptr<ModelAnimation>& animation, Looping looping)
    {
        // Copied first: the argument may be a reference to Animation itself.
        auto next = animation;
        Animation = nullptr;
        PlayAnimation(next, looping);
    }

    void MdkObject::AdvanceAnimation(float delta)
    {
        if (Animation == nullptr || AnimationEndFrame == AnimationEnded)
        {
            return;
        }

        if (AnimationEndFrame >= 0 && AnimationFrame == AnimationEndFrame)
        {
            return;
        }

        auto count = Animation->FrameCount;
        AnimationTime += delta * AnimationRate * Animation->Speed;
        if (AnimationEndFrame >= 0 && AnimationFrame < AnimationEndFrame && ToInt(MathF::Round(AnimationTime)) >= AnimationEndFrame)
        {
            AnimationTime = static_cast<float>(AnimationEndFrame);
        }

        if (AnimationTime >= count - 1)
        {
            if ((Flags & FlagLoop) != 0)
            {
                if (AnimationTime >= count)
                {
                    AnimationTime -= count;
                }
            }
            else
            {
                AnimationTime = static_cast<float>(count - 1);
            }
        }

        auto frame = ToInt(MathF::Round(AnimationTime)) % count;
        if (frame != AnimationFrame)
        {
            // Root motion: each passed frame's motion (model space) joins the push, so the next
            // tick's move collides with the arena (anim_step_frames 0x43ab70): an alien's pose
            // doesn't sink it through the floor.
            auto step = frame > AnimationFrame ? frame - AnimationFrame : frame + count - AnimationFrame;
            if ((Flags & FlagNoRootMotion) != 0)
            {
                step = 0;
            }

            auto turn = Matrix4x4::CreateRotationZ(DegreesToRadians(Yaw));
            for (auto i = 0; i < step; i++)
            {
                auto f = (AnimationFrame + 1 + i) % count;
                Push += Vector3::Transform(Animation->RootMotion[f], turn) / delta;
            }

            AnimationFrame = frame;
        }

        if (AnimationFrame == count - 1 && (Flags & FlagLoop) == 0 && AnimationFrame != AnimationEndFrame)
        {
            AnimationEndFrame = AnimationEnded;
        }
    }

    const MdkObject::Parts& MdkObject::PoseParts() const
    {
        static const Parts None;
        if (Model == nullptr)
        {
            return None;
        }

        return Animation == nullptr ? Rest(Model) : Bake(Model, Animation)[AnimationFrame];
    }

    const MdkObject::Parts& MdkObject::Rest(const std::shared_ptr<Formats::Model>& model)
    {
        std::lock_guard lock(BakedLock);
        auto found = RestPoses.find(model);
        if (found != RestPoses.end())
        {
            return found->second;
        }

        Parts pose;
        pose.reserve(model->PartList.size());
        for (const auto& p : model->PartList)
        {
            pose.push_back(p->Vertices);
        }

        return RestPoses[model] = std::move(pose);
    }

    Vector3 MdkObject::SoundPosition(Vector3 offset)
    {
        return Position + Scripts::ScriptMath::RotatedZ(offset, Yaw);
    }

    void MdkObject::ForgetPoses()
    {
        {
            std::lock_guard lock(BakedLock);
            Baked.clear();
            RestPoses.clear();
        }

        std::lock_guard lock(BoundsLock);
        BoundsCache.clear();
    }

    MdkObject::Parts MdkObject::Pose() const
    {
        if (Model == nullptr)
        {
            return {};
        }

        const auto& pose = PoseParts();
        if (HiddenParts == 0)
        {
            return pose;
        }

        Parts result;
        result.reserve(pose.size());
        for (size_t i = 0; i < pose.size(); i++)
        {
            result.push_back((HiddenParts & (1 << (static_cast<int>(i) & 31))) != 0 ? std::vector<Vector3>{} : pose[i]);
        }

        return result;
    }

    const std::vector<MdkObject::Parts>& MdkObject::Bake(const std::shared_ptr<Formats::Model>& model, const std::shared_ptr<ModelAnimation>& animation)
    {
        std::lock_guard lock(BakedLock);
        ModelKey key{model, animation};
        auto found = Baked.find(key);
        if (found != Baked.end())
        {
            return found->second;
        }

        return Baked[key] = animation->Bake(*model);
    }

    const std::vector<std::optional<Box>>& MdkObject::PartBounds() const
    {
        static const std::vector<std::optional<Box>> None;
        if (Model == nullptr)
        {
            return None;
        }

        // Shared like Baked: runtimes on other threads (parallel tests) use it too. Every frame of
        // an animation at once: no allocation while it plays.
        ModelKey key{Model, Animation};
        std::lock_guard lock(BoundsLock);
        auto found = BoundsCache.find(key);
        if (found == BoundsCache.end())
        {
            std::vector<std::vector<std::optional<Box>>> frames;
            if (Animation == nullptr)
            {
                frames.push_back(PoseBoxes(Rest(Model)));
            }
            else
            {
                for (const auto& pose : Bake(Model, Animation))
                {
                    frames.push_back(PoseBoxes(pose));
                }
            }

            found = BoundsCache.emplace(key, std::move(frames)).first;
        }

        return found->second[Animation == nullptr ? 0 : AnimationFrame];
    }

    std::vector<std::optional<Box>> MdkObject::PoseBoxes(const Parts& pose)
    {
        std::vector<std::optional<Box>> boxes;
        boxes.reserve(pose.size());
        for (const auto& v : pose)
        {
            if (v.empty())
            {
                boxes.emplace_back(std::nullopt);
                continue;
            }

            auto min = v[0];
            auto max = v[0];
            for (size_t i = 1; i < v.size(); i++)
            {
                min = Vector3::Min(min, v[i]);
                max = Vector3::Max(max, v[i]);
            }

            boxes.emplace_back(Box{min, max});
        }

        return boxes;
    }

    void MdkObject::Prepare(const std::shared_ptr<Formats::Model>& model, const std::shared_ptr<ModelAnimation>& animation)
    {
        MdkObject probe;
        probe.Model = model;
        probe.Animation = animation;
        probe.PartBounds();
    }

    Box MdkObject::PoseBounds() const
    {
        std::optional<Box> result;
        const auto& bounds = PartBounds();
        for (size_t i = 0; i < bounds.size(); i++)
        {
            if ((HiddenParts & (1 << (static_cast<int>(i) & 31))) != 0 || !bounds[i].has_value())
            {
                continue;
            }

            const auto& b = *bounds[i];
            result = result.has_value() ? Box{Vector3::Min(result->Min, b.Min), Vector3::Max(result->Max, b.Max)} : b;
        }

        if (result.has_value())
        {
            return *result;
        }

        return Model != nullptr ? Model->Bounds : Box{};
    }

    std::optional<float> MdkObject::Floor(int part, Vector3 from, Vector3 to)
    {
        const auto& pose = PoseParts();
        Matrix4x4 inverse;
        if (part >= static_cast<int>(pose.size()) || !Matrix4x4::Invert(Transform(), inverse))
        {
            return std::nullopt;
        }

        auto a = Vector3::Transform(from, inverse);
        auto ab = Vector3::Transform(to, inverse) - a;
        const auto& vertices = pose[part];
        const auto& indices = Model->PartList[part]->TriangleIndices;
        std::optional<float> nearest;
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            auto t = Crossing(a, ab, vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]]);
            if (t.has_value() && *t < nearest.value_or(std::numeric_limits<float>::max()))
            {
                nearest = t;
            }
        }

        if (!nearest.has_value())
        {
            return std::nullopt;
        }

        return Vector3::Transform(a + ab * *nearest, Transform()).Z;
    }

    std::optional<float> MdkObject::Crossing(Vector3 a, Vector3 ab, Vector3 v0, Vector3 v1, Vector3 v2)
    {
        auto e1 = v1 - v0;
        auto e2 = v2 - v0;
        auto p = Vector3::Cross(ab, e2);
        auto det = Vector3::Dot(e1, p);
        if (MathF::Abs(det) < 1e-9f)
        {
            return std::nullopt;
        }

        auto s = a - v0;
        auto u = Vector3::Dot(s, p) / det;
        auto q = Vector3::Cross(s, e1);
        auto v = Vector3::Dot(ab, q) / det;
        auto t = Vector3::Dot(e2, q) / det;
        if (u < 0.f || v < 0.f || u + v > 1.f || t < 0.f || t > 1.f)
        {
            return std::nullopt;
        }

        return t;
    }

    int MdkObject::FindPart(const std::string& name) const
    {
        if (Model == nullptr)
        {
            return -1;
        }

        for (size_t i = 0; i < Model->PartList.size(); i++)
        {
            if (Str::EqualsIgnoreCase(Model->PartList[i]->Name, name))
            {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    Vector3 MdkObject::ReferencePoint(int index) const
    {
        if (Model == nullptr || index >= static_cast<int>(Model->ReferencePoints.size()))
        {
            return Position;
        }

        auto point = Model->ReferencePoints[index];
        if (Animation != nullptr && Animation->FrameCount > 0 && index < static_cast<int>(Animation->ReferencePoints.size()))
        {
            const auto& frames = Animation->ReferencePoints[index];
            point = frames[Math::Clamp(AnimationFrame, 0, static_cast<int>(frames.size()) - 1)];
        }

        return Position + Vector3::Transform(point * Scale, Matrix4x4::CreateRotationZ(DegreesToRadians(Yaw)));
    }

    Vector3 MdkObject::PartCenter(int part) const
    {
        if (part < 0 || Model == nullptr)
        {
            return Position;
        }

        const auto& pose = PoseParts();
        if (part < static_cast<int>(pose.size()) && !pose[part].empty())
        {
            auto min = Vector3(std::numeric_limits<float>::max());
            auto max = Vector3(std::numeric_limits<float>::lowest());
            for (const auto& vertex : pose[part])
            {
                min = Vector3::Min(min, vertex);
                max = Vector3::Max(max, vertex);
            }

            return Vector3::Transform((min + max) / 2.f, Transform());
        }

        const auto& bounds = Model->PartList[part]->Bounds;
        auto centre = (bounds.Min + bounds.Max) / 2.f * Scale;
        return Position + Vector3::Transform(centre, Matrix4x4::CreateRotationZ(DegreesToRadians(Yaw)));
    }

    float MdkObject::YawTo(Vector3 point) const
    {
        auto degrees = RadiansToDegrees(MathF::Atan2(point.Y - Position.Y, point.X - Position.X));
        return std::fmod(std::fmod(degrees, 360.f) + 360.f, 360.f);
    }
}
