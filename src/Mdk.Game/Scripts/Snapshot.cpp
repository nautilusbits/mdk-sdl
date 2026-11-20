#include "Mdk.Game/Scripts/Snapshot.h"

#include <cmath>
#include <span>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Sha256.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/ModelAnimation.h"
#include "Mdk.Game/Objects/MdkObject.h"

namespace Mdk::Game::Scripts
{
    namespace
    {
        // Matrix4x4's cells in C#'s m[k / 4, k % 4] order (row by row).
        constexpr float Matrix4x4::* Cells[16] = {
            &Matrix4x4::M11, &Matrix4x4::M12, &Matrix4x4::M13, &Matrix4x4::M14,
            &Matrix4x4::M21, &Matrix4x4::M22, &Matrix4x4::M23, &Matrix4x4::M24,
            &Matrix4x4::M31, &Matrix4x4::M32, &Matrix4x4::M33, &Matrix4x4::M34,
            &Matrix4x4::M41, &Matrix4x4::M42, &Matrix4x4::M43, &Matrix4x4::M44,
        };

        // C#'s FieldInfo.IsInitOnly: a readonly field that isn't an array is not set back.
        enum class Field { Writable, ReadOnly };

        // MdkObject's public instance fields in C# declaration order (reflection's order),
        // without the Skipped ones (Model, LoopSound, TrackedVoice).
        template <typename Visit>
        void Fields(Objects::MdkObject& o, Visit&& visit)
        {
            visit("TypeName", o.TypeName, Field::Writable);
            visit("InstanceId", o.InstanceId, Field::Writable);
            visit("Arena", o.Arena, Field::Writable);
            visit("Position", o.Position, Field::Writable);
            visit("Yaw", o.Yaw, Field::Writable);
            visit("Pitch", o.Pitch, Field::Writable);
            visit("Roll", o.Roll, Field::Writable);
            visit("Scale", o.Scale, Field::Writable);
            visit("SpawnPosition", o.SpawnPosition, Field::Writable);
            visit("PreviousPosition", o.PreviousPosition, Field::Writable);
            visit("Health", o.Health, Field::Writable);
            visit("MaxHealth", o.MaxHealth, Field::Writable);
            visit("Indestructible", o.Indestructible, Field::Writable);
            visit("Flags", o.Flags, Field::Writable);
            visit("ScriptFlags", o.ScriptFlags, Field::Writable);
            visit("ContactFlags", o.ContactFlags, Field::Writable);
            visit("Variables", o.Variables, Field::Writable);
            visit("Linked", o.Linked, Field::Writable);
            visit("Leader", o.Leader, Field::Writable);
            visit("Dead", o.Dead, Field::Writable);
            visit("HiddenParts", o.HiddenParts, Field::Writable);
            visit("LockedParts", o.LockedParts, Field::Writable);
            visit("TextureFrame", o.TextureFrame, Field::Writable);
            visit("Visible", o.Visible, Field::Writable);
            visit("CowTarget", o.CowTarget, Field::Writable);
            visit("Restart", o.Restart, Field::Writable);
            visit("WaitTime", o.WaitTime, Field::Writable);
            visit("WaitResume", o.WaitResume, Field::Writable);
            visit("DeathScript", o.DeathScript, Field::Writable);
            visit("CommandTarget", o.CommandTarget, Field::Writable);
            visit("GosubReturns", o.GosubReturns, Field::Writable);
            visit("GosubRestarts", o.GosubRestarts, Field::Writable);
            visit("LevelTimers", o.LevelTimers, Field::Writable);
            visit("HitEvent", o.HitEvent, Field::Writable);
            visit("HitType", o.HitType, Field::Writable);
            visit("HitDirection", o.HitDirection, Field::Writable);
            visit("WeakPrefix", o.WeakPrefix, Field::Writable);
            visit("WeakPrefixLength", o.WeakPrefixLength, Field::Writable);
            visit("PartHealth", o.PartHealth, Field::Writable);
            visit("PartMaxHealth", o.PartMaxHealth, Field::Writable);
            visit("Priority", o.Priority, Field::Writable);
            visit("ObeyLevel", o.ObeyLevel, Field::Writable);
            visit("TargetMode", o.TargetMode, Field::Writable);
            visit("Value2c0", o.Value2c0, Field::Writable);
            visit("Value104", o.Value104, Field::Writable);
            visit("MoveCommand", o.MoveCommand, Field::Writable);
            visit("MoveParameter", o.MoveParameter, Field::Writable);
            visit("MoveDestination", o.MoveDestination, Field::Writable);
            visit("StuckCount", o.StuckCount, Field::Writable);
            visit("StuckTicks", o.StuckTicks, Field::Writable);
            visit("StuckMoved", o.StuckMoved, Field::Writable);
            visit("BankingYaw", o.BankingYaw, Field::Writable);
            visit("Waypoint", o.Waypoint, Field::Writable);
            visit("AttachPoints", o.AttachPoints, Field::Writable);
            visit("HeightOffset", o.HeightOffset, Field::Writable);
            visit("Speed", o.Speed, Field::Writable);
            visit("MaxSpeed", o.MaxSpeed, Field::Writable);
            visit("Acceleration", o.Acceleration, Field::Writable);
            visit("Deceleration", o.Deceleration, Field::Writable);
            visit("Friction", o.Friction, Field::Writable);
            visit("Gravity", o.Gravity, Field::Writable);
            visit("Velocity", o.Velocity, Field::Writable);
            visit("Push", o.Push, Field::Writable);
            visit("Parameter", o.Parameter, Field::Writable);
            visit("ParameterTimer", o.ParameterTimer, Field::Writable);
            visit("TouchDamage", o.TouchDamage, Field::Writable);
            visit("Path", o.Path, Field::Writable);
            visit("PathTime", o.PathTime, Field::Writable);
            visit("PathSpeed", o.PathSpeed, Field::Writable);
            visit("PathSpeeds", o.PathSpeeds, Field::Writable);
            visit("PathStop", o.PathStop, Field::Writable);
            visit("PathOrigin", o.PathOrigin, Field::Writable);
            visit("PathYawOffset", o.PathYawOffset, Field::Writable);
            visit("FrameSound", o.FrameSound, Field::Writable);
            visit("FrameSoundFrame", o.FrameSoundFrame, Field::Writable);
            visit("EffectFrames", o.EffectFrames, Field::Writable);
            visit("EffectTime", o.EffectTime, Field::Writable);
            visit("ThrownKind", o.ThrownKind, Field::Writable);
            visit("ItemTicks", o.ItemTicks, Field::Writable);
            visit("BlastRange", o.BlastRange, Field::Writable);
            visit("ShotPart", o.ShotPart, Field::Writable);
            visit("ShotPoint", o.ShotPoint, Field::Writable);
            visit("RollingBasis", o.RollingBasis, Field::Writable);
            visit("RollRadius", o.RollRadius, Field::Writable);
            visit("SwingPivot", o.SwingPivot, Field::Writable);
            visit("SwingSpeed", o.SwingSpeed, Field::Writable);
            visit("SwingGain", o.SwingGain, Field::Writable);
            visit("SwingLength", o.SwingLength, Field::Writable);
            visit("SwingYaw", o.SwingYaw, Field::Writable);
            visit("RopeColor", o.RopeColor, Field::Writable);
            visit("RopeMask", o.RopeMask, Field::Writable);
            visit("RopePoints", o.RopePoints, Field::Writable);
            visit("Attached", o.Attached, Field::Writable);
            visit("DoorAnimations", o.DoorAnimations, Field::Writable);
            visit("DoorState", o.DoorState, Field::Writable);
            visit("DoorDistance", o.DoorDistance, Field::Writable);
            visit("DoorSounds", o.DoorSounds, Field::Writable);
            visit("LockParts", o.LockParts, Field::Writable);
            visit("HatchParts", o.HatchParts, Field::Writable);
            visit("Connects", o.Connects, Field::Writable);
            visit("TrackedSound", o.TrackedSound, Field::Writable);
            visit("LoopSoundName", o.LoopSoundName, Field::Writable);
            visit("Labels", o.Labels, Field::Writable);
            visit("Animation", o.Animation, Field::Writable);
            visit("AnimationTime", o.AnimationTime, Field::Writable);
            visit("AnimationFrame", o.AnimationFrame, Field::Writable);
            visit("AnimationRate", o.AnimationRate, Field::Writable);
            visit("AnimationEndFrame", o.AnimationEndFrame, Field::Writable);
        }

        template <typename Visit>
        void Fields(GameStats& o, Visit&& visit)
        {
            visit("Shots", o.Shots, Field::Writable);
            visit("ShotHits", o.ShotHits, Field::Writable);
            visit("SniperShots", o.SniperShots, Field::Writable);
            visit("SniperHits", o.SniperHits, Field::Writable);
            visit("HeadShots", o.HeadShots, Field::Writable);
            visit("Enemies", o.Enemies, Field::Writable);
            visit("Kills", o.Kills, Field::Writable);
            visit("TownFlags", o.TownFlags, Field::Writable);
            visit("StrikeUsed", o.StrikeUsed, Field::Writable);
        }

        // ArenaState's readonly arrays are std::arrays, filled in place by Decode.
        template <typename Visit>
        void Fields(ScriptRuntime::ArenaState& o, Visit&& visit)
        {
            visit("Name", o.Name, Field::ReadOnly);
            visit("Controller", o.Controller, Field::ReadOnly);
            visit("HitScripts", o.HitScripts, Field::ReadOnly);
            visit("Variables", o.Variables, Field::Writable);
            visit("Flags", o.Flags, Field::Writable);
            visit("Started", o.Started, Field::Writable);
            visit("GroupHitFlags", o.GroupHitFlags, Field::Writable);
            visit("GroupHitMasks", o.GroupHitMasks, Field::Writable);
            visit("GroupHitScripts", o.GroupHitScripts, Field::Writable);
            visit("GroupCounters", o.GroupCounters, Field::Writable);
        }
    }

    Snapshot::Snapshot(std::vector<std::shared_ptr<MdkObject>> objects, const Dictionary<std::string, std::shared_ptr<MdkObject>>& fixedObjects,
        FindAnimation findAnimation)
        : _objects(std::move(objects))
    {
        for (int i = 0; i < static_cast<int>(_objects.size()); i++)
        {
            if (_objects[i] != nullptr)
            {
                _index[_objects[i].get()] = i;
            }
        }

        _fixed = fixedObjects;
        for (const auto& [key, obj] : fixedObjects)
        {
            _fixedKeys[obj.get()] = key;
        }

        _findAnimation = std::move(findAnimation);
    }

    std::string Snapshot::Hash(const std::string& json)
    {
        auto bytes = std::span<const byte>(reinterpret_cast<const byte*>(json.data()), json.size());
        return Convert::ToHexStringLower(Sha256::HashData(bytes)).substr(0, HashHexDigits);
    }

    nlohmann::json Snapshot::Pack(MdkObject& obj)
    {
        auto data = nlohmann::json::object();
        Fields(obj, [&](const char* name, auto& field, Field) { data[name] = Encode(field); });
        return data;
    }

    nlohmann::json Snapshot::Pack(GameStats& obj)
    {
        auto data = nlohmann::json::object();
        Fields(obj, [&](const char* name, auto& field, Field) { data[name] = Encode(field); });
        return data;
    }

    nlohmann::json Snapshot::Pack(ScriptRuntime::ArenaState& obj)
    {
        auto data = nlohmann::json::object();
        Fields(obj, [&](const char* name, auto& field, Field) { data[name] = Encode(field); });
        return data;
    }

    void Snapshot::Unpack(MdkObject& obj, const nlohmann::json& data, MdkObject* owner)
    {
        auto* ownerOf = owner != nullptr ? owner : &obj;
        Fields(obj, [&](const char* name, auto& field, Field kind) {
            auto node = data.find(name);
            if (node == data.end() || kind == Field::ReadOnly)
            {
                return;
            }

            Decode(*node, field, ownerOf);
        });
    }

    void Snapshot::Unpack(GameStats& obj, const nlohmann::json& data, MdkObject* owner)
    {
        Fields(obj, [&](const char* name, auto& field, Field kind) {
            auto node = data.find(name);
            if (node == data.end() || kind == Field::ReadOnly)
            {
                return;
            }

            Decode(*node, field, owner);
        });
    }

    void Snapshot::Unpack(ScriptRuntime::ArenaState& obj, const nlohmann::json& data, MdkObject* owner)
    {
        Fields(obj, [&](const char* name, auto& field, Field kind) {
            auto node = data.find(name);
            if (node == data.end() || kind == Field::ReadOnly)
            {
                return;
            }

            Decode(*node, field, owner);
        });
    }

    nlohmann::json Snapshot::Encode(int value) const
    {
        return value;
    }

    nlohmann::json Snapshot::Encode(bool value) const
    {
        return value;
    }

    nlohmann::json Snapshot::Encode(const std::string& value) const
    {
        return value;
    }

    nlohmann::json Snapshot::Encode(float value) const
    {
        return Number(value);
    }

    nlohmann::json Snapshot::Encode(Vector2 value) const
    {
        return nlohmann::json::array({Number(value.X), Number(value.Y)});
    }

    nlohmann::json Snapshot::Encode(Vector3 value) const
    {
        return nlohmann::json::array({Number(value.X), Number(value.Y), Number(value.Z)});
    }

    nlohmann::json Snapshot::Encode(const Matrix4x4& value) const
    {
        auto array = nlohmann::json::array();
        for (int k = 0; k < 16; k++)
        {
            array.push_back(Number(value.*Cells[k]));
        }

        return array;
    }

    nlohmann::json Snapshot::Encode(MdkObject::PointPair value) const
    {
        return nlohmann::json::array({value.A, value.B});
    }

    nlohmann::json Snapshot::Encode(const std::shared_ptr<MdkObject>& value) const
    {
        if (value == nullptr)
        {
            return nullptr;
        }

        int i;
        if (_index.TryGetValue(value.get(), i))
        {
            return nlohmann::json{{ObjectKey, i}};
        }

        std::string key;
        if (_fixedKeys.TryGetValue(value.get(), key))
        {
            return nlohmann::json{{FixedKey, key}};
        }

        return nullptr;
    }

    nlohmann::json Snapshot::Encode(const std::shared_ptr<Formats::ModelAnimation>& value) const
    {
        if (value == nullptr)
        {
            return nullptr;
        }

        return nlohmann::json{{AnimationKey, value->Name}};
    }

    nlohmann::json Snapshot::Encode(const std::shared_ptr<Formats::Model>&) const
    {
        return nullptr;
    }

    nlohmann::json Snapshot::Number(float f)
    {
        if (std::isfinite(f))
        {
            return f;
        }

        return Str::FromFloat(f);
    }

    float Snapshot::Float(const nlohmann::json& node)
    {
        if (node.is_string())
        {
            float value = 0.0f;
            if (!Str::TryParseFloat(node.get<std::string>(), value))
            {
                throw std::invalid_argument("Not a float: " + node.get<std::string>());
            }

            return value;
        }

        return node.is_null() ? 0.0f : node.get<float>();
    }

    void Snapshot::Decode(const nlohmann::json& node, int& value, MdkObject*) const
    {
        value = node.is_null() ? 0 : node.get<int>();
    }

    void Snapshot::Decode(const nlohmann::json& node, bool& value, MdkObject*) const
    {
        value = node.is_null() ? false : node.get<bool>();
    }

    void Snapshot::Decode(const nlohmann::json& node, std::string& value, MdkObject*) const
    {
        value = node.is_null() ? std::string() : node.get<std::string>();
    }

    void Snapshot::Decode(const nlohmann::json& node, float& value, MdkObject*) const
    {
        value = Float(node);
    }

    void Snapshot::Decode(const nlohmann::json& node, Vector2& value, MdkObject*) const
    {
        if (node.is_null())
        {
            value = {};
            return;
        }

        value = Vector2(Float(node[0]), Float(node[1]));
    }

    void Snapshot::Decode(const nlohmann::json& node, Vector3& value, MdkObject*) const
    {
        if (node.is_null())
        {
            value = {};
            return;
        }

        value = Vector3(Float(node[0]), Float(node[1]), Float(node[2]));
    }

    void Snapshot::Decode(const nlohmann::json& node, Matrix4x4& value, MdkObject*) const
    {
        // new Matrix4x4() / default: all zeros (C++'s default is the identity).
        Matrix4x4 m;
        for (int k = 0; k < 16; k++)
        {
            m.*Cells[k] = node.is_null() ? 0.0f : Float(node[k]);
        }

        value = m;
    }

    void Snapshot::Decode(const nlohmann::json& node, MdkObject::PointPair& value, MdkObject*) const
    {
        if (node.is_null())
        {
            value = {};
            return;
        }

        value = {node[0].get<int>(), node[1].get<int>()};
    }

    void Snapshot::Decode(const nlohmann::json& node, std::shared_ptr<MdkObject>& value, MdkObject*) const
    {
        value = node.is_null() ? nullptr : Reference(node);
    }

    void Snapshot::Decode(const nlohmann::json& node, std::shared_ptr<Formats::ModelAnimation>& value, MdkObject* owner) const
    {
        if (node.is_null())
        {
            value = nullptr;
            return;
        }

        value = _findAnimation(owner, node.at(AnimationKey).get<std::string>());
    }

    std::shared_ptr<Objects::MdkObject> Snapshot::Reference(const nlohmann::json& data) const
    {
        auto index = data.find(ObjectKey);
        if (index != data.end())
        {
            auto i = index->get<int>();
            return i >= 0 && i < static_cast<int>(_objects.size()) ? _objects[i] : nullptr;
        }

        auto key = data.find(FixedKey);
        return key != data.end() ? _fixed.GetValueOrDefault(key->get<std::string>()) : nullptr;
    }
}
