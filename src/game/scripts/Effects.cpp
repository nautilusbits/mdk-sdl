#include "game/scripts/Effects.h"

#include <algorithm>

#include "game/FrameBlend.h"
#include "game/ListCopy.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/ScriptRuntime.h"

namespace Mdk::Game::Scripts
{
    using Objects::MdkObject;

    Vector3 Effects::Effect::PositionAt(float alpha) const
    {
        if (_ticked.has_value() && !FrameBlend::Jumped(*_ticked, Position, ScriptRuntime::Tick))
        {
            return FrameBlend::Lerp(*_ticked, Position, alpha);
        }

        return Position;
    }

    int Effects::Effect::Frame() const
    {
        return Math::Clamp(Frames - 1 - ToInt(Life * Speed) % Frames, 0, Frames - 1);
    }

    void Effects::Attach(MdkObject& obj, int slot, int towards)
    {
        constexpr float WoundScale = 10.0f;
        if (_wounds.ContainsKey({&obj, slot}))
        {
            return;
        }

        auto effect = Create(obj.Arena, WoundTexture, Kind::Wound, obj.ReferencePoint(slot), WoundScale, 0.5f);
        if (effect == nullptr)
        {
            return;
        }

        effect->Owner = obj.shared_from_this();
        effect->Slot = slot;
        effect->Towards = towards;
        effect->Life = static_cast<float>(effect->Frames * 2 - 1);
        _wounds[{&obj, slot}] = effect;
    }

    void Effects::Detach(MdkObject& obj, int slot)
    {
        std::shared_ptr<Effect> effect;
        if (_wounds.TryGetValue({&obj, slot}, effect))
        {
            Remove(*effect);
        }
    }

    void Effects::SpawnDrop(const std::string& arena, Vector3 point, Vector3 velocity, float scale)
    {
        auto effect = Create(arena, _rng->Next(2) == 0 ? BigDrop : SmallDrop, Kind::Drop, point, scale, 0.25f);
        if (effect == nullptr)
        {
            return;
        }

        effect->Velocity = velocity;
        effect->Life = static_cast<float>(effect->Frames * 4 - 1);
    }

    void Effects::SpawnTrail(const std::string& arena, Vector3 point)
    {
        auto effect = Create(arena, TrailTexture, Kind::Trail, point, TrailScale, 1.0f);
        if (effect != nullptr)
        {
            effect->Life = TrailLife;
        }
    }

    void Effects::SpawnBubble(const std::string& arena, Vector3 point)
    {
        auto scale = (_rng->Next(RandRange) - RandHalf) * BubbleScaleSpread + BubbleScale;
        auto effect = Create(arena, BubbleTexture, Kind::Bubble, point, scale, 0.5f);
        if (effect != nullptr)
        {
            effect->Life = BubbleLife;
        }
    }

    bool Effects::HasEffects(const std::string& arena) const
    {
        return std::any_of(_effects.begin(), _effects.end(), [&](const auto& e) { return e->Arena == arena; });
    }

    std::shared_ptr<Effects::Effect> Effects::Create(const std::string& arena, std::string_view texture, Kind kind, Vector3 point,
        float scale, float speed)
    {
        if (static_cast<int>(_effects.size()) >= MaxEffects)
        {
            return nullptr;
        }

        auto frames = FrameCount ? FrameCount(arena, std::string(texture)) : 0;
        if (frames == 0)
        {
            return nullptr;
        }

        auto effect = std::make_shared<Effect>();
        effect->Kind = kind;
        effect->Arena = arena;
        effect->Texture = std::string(texture);
        effect->Frames = frames;
        effect->Position = point;
        effect->Scale = scale;
        effect->Speed = speed;
        _effects.push_back(effect);
        return effect;
    }

    void Effects::Remove(Effect& effect)
    {
        if (effect.Owner != nullptr && _wounds.GetValueOrDefault({effect.Owner.get(), effect.Slot}).get() == &effect)
        {
            _wounds.Remove({effect.Owner.get(), effect.Slot});
        }

        auto it = std::find_if(_effects.begin(), _effects.end(), [&](const auto& e) { return e.get() == &effect; });
        if (it != _effects.end())
        {
            _effects.erase(it);
        }
    }

    void Effects::Update(float ticks)
    {
        auto copy = ListCopy<std::shared_ptr<Effect>>::Of(_effects);
        for (const auto& item : copy)
        {
            auto& effect = *item;
            effect.Remember();
            switch (effect.Kind)
            {
                case Kind::Wound:
                    UpdateWound(effect, ticks);
                    break;
                case Kind::Drop:
                case Kind::Trail:
                    Move(effect, ticks);
                    effect.Life -= ticks;
                    break;
                case Kind::Bubble:
                    UpdateBubble(effect, ticks);
                    break;
                case Kind::Pop:
                    effect.Life -= ticks;
                    break;
            }

            if (effect.Done || (effect.Life <= 0.0f && effect.Kind != Kind::Wound))
            {
                Remove(effect);
            }
        }
    }

    void Effects::UpdateBubble(Effect& effect, float ticks)
    {
        auto dt = ticks / TicksPerSecond;
        auto driftX = static_cast<float>(_rng->Next(RandRange) - RandHalf);
        auto driftY = static_cast<float>(_rng->Next(RandRange) - RandHalf);
        auto drift = Vector3(driftX, driftY, 0.0f) * BubbleDrift;
        effect.Velocity = effect.Velocity + (drift + Vector3(0.0f, 0.0f, BubbleRise)) * dt;
        effect.Scale += BubbleGrowth * dt;
        if (Move(effect, ticks))
        {
            effect.Life = 0.0f;
        }

        effect.Life -= ticks;
        if (effect.Life >= 1.0f)
        {
            return;
        }

        auto frames = FrameCount ? FrameCount(effect.Arena, std::string(PopTexture)) : 0;
        if (frames == 0)
        {
            return;
        }

        effect.Kind = Kind::Pop;
        effect.Texture = std::string(PopTexture);
        effect.Frames = frames;
        effect.Velocity = Vector3::Zero();
        effect.Life = static_cast<float>(frames * 2 - 1);
    }

    void Effects::UpdateWound(Effect& effect, float ticks)
    {
        auto& obj = *effect.Owner;
        if (obj.Dead)
        {
            effect.Done = true;
            return;
        }

        effect.Life -= ticks;
        if (effect.Life < 0.0f)
        {
            effect.Life = static_cast<float>(effect.Frames * 2 - 1);
            effect.Intensity = Math::Max(effect.Intensity - ((_rng->Next(RandRange) & IntensityLossMask) + IntensityLossMin), 0);
        }

        effect.Position = obj.ReferencePoint(effect.Slot);
        if (effect.Burst < 1.0f && _rng->Next(RandRange) < effect.Intensity)
        {
            effect.Burst = BurstTicks;
            auto x = static_cast<float>(RandomHalf());
            auto y = static_cast<float>(RandomHalf());
            auto z = static_cast<float>(RandomHalf());
            effect.Jitter = Vector3(x, y, z) * JitterScale;
        }

        if (effect.Burst <= 0.0f)
        {
            return;
        }

        effect.Burst -= ticks;
        auto towards = obj.ReferencePoint(effect.Towards) - effect.Position;
        auto direction = towards == Vector3::Zero() ? Vector3::Zero() : Vector3::Normalize(towards);
        SpawnDrop(obj.Arena, effect.Position, (direction + effect.Jitter) * BurstSpeed, WoundDropScale);
    }

    int Effects::RandomHalf()
    {
        return _rng->Next(RandRange) - RandHalf;
    }

    bool Effects::Move(Effect& effect, float ticks)
    {
        auto motion = effect.Velocity * ticks;
        if (motion == Vector3::Zero())
        {
            return false;
        }

        auto falls = effect.Kind == Kind::Drop || effect.Kind == Kind::Trail;
        auto hit = Ray ? Ray(effect.Arena, effect.Position, effect.Position + motion) : std::nullopt;
        if (!hit.has_value())
        {
            effect.Position = effect.Position + motion;
            if (falls)
            {
                effect.Velocity.Z -= Gravity * ticks;
            }

            return false;
        }

        effect.Position = hit->Point - Vector3::Normalize(motion) * ContactBackoff;
        if (!falls)
        {
            effect.Velocity = Vector3::Zero();
            return true;
        }

        effect.Velocity = effect.Velocity - hit->Normal * Vector3::Dot(effect.Velocity, hit->Normal) * Bounce;
        effect.Life -= BounceTicks;
        return true;
    }
}
