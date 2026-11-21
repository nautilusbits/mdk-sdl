#include "game/scripts/Fans.h"

#include <format>
#include <limits>

#include "core/Console.h"

namespace Mdk::Game::Scripts
{
    Fans::Fans(std::vector<Formats::Dti::ArenaEntry> arenas, std::shared_ptr<Random> rng)
        : _arenas(std::move(arenas)), _rng(std::move(rng))
    {
    }

    void Fans::Create(const std::string& arena, int hotspot, const std::string& name, int param, int type, float strength)
    {
        const Formats::Dti::Record* record = nullptr;
        for (const auto& a : _arenas)
        {
            if (a.Name != arena)
            {
                continue;
            }

            for (const auto& r : a.Records)
            {
                if (r.Type == FanHotspot && r.Id == hotspot)
                {
                    record = &r;
                    break;
                }
            }

            break;
        }

        if (record == nullptr)
        {
            Console::Error().WriteLine(std::format("Cannot find fan hotspot id {} for {}", hotspot, name));
            return;
        }

        auto fan = std::make_shared<Fan>();
        fan->Name = name;
        fan->Arena = arena;
        fan->Param = param;
        fan->Type = type;
        fan->Strength = strength;
        fan->BoxStart = record->Position - Vector3(0.0f, 0.0f, BoxDrop);
        fan->BoxEnd = record->BoxEnd;
        if (type == TypeTimed)
        {
            fan->Strength = (record->BoxEnd.Z - record->Position.Z) / (strength - BoxDrop);
        }

        _fans.push_back(fan);
    }

    nlohmann::json Fans::Snapshot() const
    {
        auto array = nlohmann::json::array();
        for (const auto& f : _fans)
        {
            array.push_back({
                {"name", f->Name},
                {"arena", f->Arena},
                {"param", f->Param},
                {"type", f->Type},
                {"strength", f->Strength},
                {"mask", f->Mask},
                {"box", nlohmann::json::array({f->BoxStart.X, f->BoxStart.Y, f->BoxStart.Z, f->BoxEnd.X, f->BoxEnd.Y, f->BoxEnd.Z})},
            });
        }

        return array;
    }

    void Fans::Restore(const nlohmann::json& data)
    {
        _fans.clear();
        for (const auto& entry : data)
        {
            if (!entry.is_object())
            {
                continue;
            }

            std::vector<float> box;
            for (const auto& n : entry.at("box"))
            {
                box.push_back(n.get<float>());
            }

            auto fan = std::make_shared<Fan>();
            fan->Name = entry.at("name").get<std::string>();
            fan->Arena = entry.at("arena").get<std::string>();
            fan->Param = entry.at("param").get<int>();
            fan->Type = entry.at("type").get<int>();
            fan->Strength = entry.at("strength").get<float>();
            fan->Mask = entry.at("mask").get<int>();
            fan->BoxStart = Vector3(box.at(0), box.at(1), box.at(2));
            fan->BoxEnd = Vector3(box.at(3), box.at(4), box.at(5));
            _fans.push_back(fan);
        }
    }

    void Fans::Remove(const std::string& arena, const std::string& name)
    {
        for (auto i = 0; i < static_cast<int>(_fans.size()); i++)
        {
            if (_fans[i]->Arena == arena && _fans[i]->Name == name)
            {
                _fans.erase(_fans.begin() + i);
                return;
            }
        }
    }

    void Fans::Enable(const std::string& arena, const std::string& name, Power power)
    {
        for (const auto& fan : _fans)
        {
            if (fan->Arena != arena || fan->Name != name)
            {
                continue;
            }

            fan->Mask = power == Power::On ? fan->Mask | Enabled : fan->Mask & ~Enabled;
        }
    }

    void Fans::Update()
    {
        for (const auto& fan : _fans)
        {
            if (!IsLive(fan->Arena) || _rng->Next(SparkChance) != 0)
            {
                continue;
            }

            auto x = fan->BoxStart.X + static_cast<float>(_rng->NextDouble()) * (fan->BoxEnd.X - fan->BoxStart.X);
            auto y = fan->BoxStart.Y + static_cast<float>(_rng->NextDouble()) * (fan->BoxEnd.Y - fan->BoxStart.Y);
            if (Spark)
            {
                Spark(fan->Arena, Vector3(x, y, fan->BoxStart.Z + SparkLift));
            }
        }
    }

    float Fans::Query(const std::string& arena, Vector3 point, float vz, int mask, float dt)
    {
        auto found = false;
        for (const auto& fan : _fans)
        {
            if (fan->Arena != arena || (fan->Mask & mask) == 0 || !Holds(*fan, point))
            {
                continue;
            }

            found = true;
            vz = Lift(*fan, point.Z, vz, dt);
        }

        return found ? vz : std::numeric_limits<float>::quiet_NaN();
    }

    bool Fans::Holds(const Fan& fan, Vector3 point)
    {
        return point.X >= fan.BoxStart.X && point.X <= fan.BoxEnd.X
            && point.Y >= fan.BoxStart.Y && point.Y <= fan.BoxEnd.Y
            && point.Z >= fan.BoxStart.Z && point.Z <= fan.BoxEnd.Z + Top;
    }

    float Fans::Lift(const Fan& fan, float z, float vz, float dt)
    {
        auto height = fan.Param == 0 ? (z - fan.BoxStart.Z) / (fan.BoxEnd.Z - fan.BoxStart.Z) : 1.0f;

        // Types 1-5 fade with the relative height (param ≠ 0: as at the top).
        auto factor = 1.0f;
        switch (fan.Type)
        {
            case 1:
                factor = 1.0f - height * height;
                break;
            case 2:
                factor = (1.0f - height) * (1.0f - height);
                break;
            case 3:
                factor = 1.0f - height;
                break;
            case 4:
                factor = 1.0f - height * height * height;
                break;
            case 5:
                factor = MathF::Pow(1.0f - height, 3.0f);
                break;
            case TypeTimed:
                if (fan.BoxEnd.Z - z < Top)
                {
                    factor = 1.0f - (z - (fan.BoxEnd.Z - Top)) * TopFade;
                }

                break;
            default:
                break;
        }

        auto target = fan.Strength * factor;
        if (fan.Type == TypeTimed)
        {
            if (fan.BoxEnd.Z - z < WobbleZone)
            {
                target += Wobble();
            }

            // A faster upward speed is halved towards the target.
            if (target < vz)
            {
                vz = (vz + target) * 0.5f;
            }
        }
        else if (factor < 0.0f)
        {
            target = -LiftAcceleration;
        }

        return vz < target ? MathF::Min(vz + (target + LiftAcceleration) * dt, target) : vz;
    }

    float Fans::Wobble()
    {
        _wobble += _wobbleUp ? WobbleStep : -WobbleStep;
        if (!_wobbleUp && _wobble < -WobbleLimit)
        {
            _wobbleUp = true;
        }
        else if (_wobbleUp && _wobble > WobbleLimit)
        {
            _wobbleUp = false;
        }

        return _wobble;
    }
}
