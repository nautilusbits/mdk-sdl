#include "game/FrameBlend.h"

#include "formats/Model.h"
#include "game/Angles.h"
#include "game/kurt/Kurt.h"
#include "game/kurt/Scope.h"
#include "game/objects/MdkObject.h"
#include "game/Viewer.h"

namespace Mdk::Game
{
    using Kurt::Scope;
    using Objects::MdkObject;

    void FrameBlend::Record(Kurt::Kurt& kurt, const std::shared_ptr<MdkObject>& platform, const std::shared_ptr<MdkObject>& driven, int tick)
    {
        // What follows him (the board he stands on) doesn't carry him.
        auto carrier = platform != driven ? platform : nullptr;

        // The carrier a step ago: before this step's tick if one ran (objects move at ticks).
        auto was = tick != _ticks ? 0.f : 1.f;
        _ticks = tick;
        auto now = Pose{ kurt.Feet, kurt.Yaw, kurt.Teleports, carrier, carrier != nullptr ? carrier->Position : Vector3::Zero(),
            carrier != nullptr ? carrier->Yaw : 0.f, carrier != nullptr ? carrier->PositionAt(was) : Vector3::Zero(),
            carrier != nullptr ? carrier->YawAt(was) : 0.f };
        auto sight = Sight{ kurt.Sniping, kurt.Scope->Zoom, kurt.Scope->Pitch, kurt.Scope->ClipTime };
        _sightWas = !_previous.has_value() ? sight : _sight;
        _sight = sight;
        _previous = !_previous.has_value() || now.Teleports != _current.Teleports ? now : _current;
        _current = now;
        _driven = driven;
    }

    void FrameBlend::Turn(float degrees)
    {
        if (!_previous.has_value())
        {
            return;
        }

        _previous->Yaw += degrees;
        _current.Yaw += degrees;
    }

    void FrameBlend::Update(Kurt::Kurt& kurt, float step, float tick)
    {
        _tick = tick;
        std::tie(Feet, Yaw) = _blending == Blending::Off || !_previous.has_value()
            ? std::pair<Vector3, float>(kurt.Feet, kurt.Yaw)
            : KurtAt(*_previous, _current, step, tick);
        _lag = Feet - kurt.Feet;
        auto& scope = *kurt.Scope;
        std::tie(Zoom, ScopePitch, ClipTime) = _blending == Blending::Off || !_previous.has_value()
            ? std::tuple<float, float, float>(scope.Zoom, scope.Pitch, scope.ClipTime)
            : ScopeAt(scope, _sightWas, _sight, step);
    }

    std::tuple<float, float, float> FrameBlend::ScopeAt(const Scope& scope, const Sight& from, const Sight& to, float step)
    {
        if (from.Sniping != to.Sniping)
        {
            return { scope.Zoom, scope.Pitch, scope.ClipTime };
        }

        auto pitch = Math::Clamp(scope.Pitch + Lerp(from.Pitch, to.Pitch, step) - to.Pitch, -Scope::PitchLimit, Scope::PitchLimit);
        auto clip = to.Clip > from.Clip ? scope.ClipTime : Lerp(from.Clip, to.Clip, step);
        return { Lerp(from.Zoom, to.Zoom, step), pitch, clip };
    }

    Matrix4x4 FrameBlend::TransformOf(MdkObject& obj)
    {
        if (_blending == Blending::Off)
        {
            return obj.Transform();
        }

        return &obj == _driven.get() ? obj.Transform() * Matrix4x4::CreateTranslation(_lag) : obj.TransformAt(_tick);
    }

    Vector3 FrameBlend::PositionOf(MdkObject& obj)
    {
        if (_blending == Blending::Off)
        {
            return obj.Position;
        }

        return &obj == _driven.get() ? obj.Position + _lag : obj.PositionAt(_tick);
    }

    const FrameBlend::PoseParts& FrameBlend::PoseOf(MdkObject& obj)
    {
        if (_blending == Blending::Off)
        {
            return obj.PoseParts();
        }

        auto frames = obj.FramesAt(_tick);
        if (!frames.has_value())
        {
            return obj.PoseParts();
        }

        const auto& from = *frames->From;
        const auto& to = *frames->To;
        auto share = frames->Share;
        auto [found, added] = _poses.try_emplace(obj.Model.get());
        auto& scratch = found->second;
        if (added)
        {
            scratch.Pose.resize(to.size());
            scratch.Parts.resize(to.size());
        }

        auto& pose = scratch.Pose;
        auto& parts = scratch.Parts;
        for (size_t p = 0; p < to.size(); p++)
        {
            // A part the animation leaves still is the model's own.
            if (from[p] == to[p])
            {
                pose[p] = to[p];
                continue;
            }

            if (parts[p].size() != to[p].size())
            {
                parts[p].resize(to[p].size());
            }

            for (size_t v = 0; v < to[p].size(); v++)
            {
                parts[p][v] = Vector3::Lerp(from[p][v], to[p][v], share);
            }

            pose[p] = parts[p];
        }

        return pose;
    }

    Vector3 FrameBlend::Carried(MdkObject& obj, Vector3 point)
    {
        if (_blending == Blending::Off)
        {
            return point;
        }

        auto turn = &obj == _driven.get() ? 0.f : obj.YawAt(_tick) - obj.Yaw;
        return PositionOf(obj) + Vector3::Transform(point - obj.Position, Matrix4x4::CreateRotationZ(DegreesToRadians(turn)));
    }

    std::pair<Vector3, float> FrameBlend::KurtAt(const Pose& from, const Pose& to, float step, float tick)
    {
        auto carrier = to.Carrier != nullptr ? to.Carrier : from.Carrier;
        if (carrier == nullptr)
        {
            return Jumped(from.Feet, to.Feet, Viewer::Step) ? std::pair(to.Feet, to.Yaw) : std::pair(Lerp(from.Feet, to.Feet, step), LerpAngle(from.Yaw, to.Yaw, step));
        }

        auto [nearPoint, nearYaw] = from.Carrier == carrier ? std::pair(from.Feet - from.CarrierAt, from.Yaw - from.CarrierYaw)
            : std::pair(from.Feet - to.CarrierWas, from.Yaw - to.CarrierYawWas);
        auto [farPoint, farYaw] = to.Carrier == carrier ? std::pair(to.Feet - to.CarrierAt, to.Yaw - to.CarrierYaw)
            : std::pair(to.Feet - carrier->Position, to.Yaw - carrier->Yaw);
        auto on = Jumped(nearPoint, farPoint, Viewer::Step) ? farPoint : Lerp(nearPoint, farPoint, step);
        auto turned = LerpAngle(nearYaw, farYaw, step);
        return { carrier->PositionAt(tick) + on, carrier->YawAt(tick) + turned };
    }

    float FrameBlend::LerpAngle(float from, float to, float t)
    {
        if (t >= 1.f)
        {
            return to;
        }

        auto turn = MathF::IEEERemainder(to - from, 360.f);
        return from + turn * t;
    }
}
