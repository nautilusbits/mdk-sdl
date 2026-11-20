#include "Mdk.Game/Scripts/ScriptRuntime.h"

#include <algorithm>
#include <stdexcept>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Scripts/AirStrike.h"
#include "Mdk.Game/Scripts/EndLevel.h"
#include "Mdk.Game/Scripts/Fans.h"
#include "Mdk.Game/Scripts/Items.h"
#include "Mdk.Game/Scripts/Rides.h"
#include "Mdk.Game/Scripts/Snapshot.h"

namespace Mdk::Game::Scripts
{
    namespace
    {
        // C#'s JsonObject indexer: a missing key reads as null.
        const nlohmann::json& Node(const nlohmann::json& data, const char* key)
        {
            static const nlohmann::json Null;
            auto it = data.find(key);
            return it != data.end() ? *it : Null;
        }
    }

    bool ScriptRuntime::CanSnapshot()
    {
        return Cutscene == 0 && !LevelOver && EndLevel == nullptr && !AirStrike->IsActive() && Rides->Ridden == nullptr && Kurt->Health > 0
            && CurrentArena.length() != 0;
    }

    std::string ScriptRuntime::Capture()
    {
        auto data = nlohmann::json::object();
        data["kurt"] = Kurt->Snapshot();
        data["level"] = Snapshot();
        return data.dump();
    }

    void ScriptRuntime::Load(const std::string& json)
    {
        auto data = nlohmann::json::parse(json);
        Restore(data.at("level"));
        Kurt->Restore(data.at("kurt"));

        // The first tick's move starts where Kurt is: it crosses no connection.
        _previousKurtPosition = Kurt->Feet;
    }

    nlohmann::json ScriptRuntime::Snapshot()
    {
        std::vector<std::shared_ptr<MdkObject>> objects;
        for (const auto& o : Objects)
        {
            if (!o->Dead)
            {
                objects.push_back(o);
            }
        }

        auto packer = Scripts::Snapshot(objects, FixedObjects(), [this](MdkObject* obj, const std::string& name) { return FindAnimationNamed(obj, name); });
        auto data = nlohmann::json::object();
        data["global_variables"] = packer.Encode(GlobalVariables);
        data["global_flags"] = GlobalFlags;
        data["alarm_ticks"] = AlarmTicks;
        data["sky_mode"] = SkyMode;
        data["option"] = Option;
        data["town_ticks"] = TownTicks;
        data["town_total"] = _townTotal;
        data["current_arena"] = CurrentArena;
        data["second_arena"] = SecondArena;
        data["second_active"] = SecondActive;
        data["next_instance"] = _nextInstance;
        data["camera_track_pitch"] = packer.Encode(CameraTrackPitch);
        data["camera_track_ticks"] = CameraTrackTicks;
        data["shatter_point"] = packer.Encode(ShatterPoint);
        data["shatter_direction"] = packer.Encode(ShatterDirection);
        data["alien_target"] = packer.Encode(AlienTarget);
        data["strike_used"] = AirStrike->UsedUp;
        data["stats"] = packer.Pack(*Stats);

        auto arenas = nlohmann::json::object();
        for (const auto& [name, state] : _arenas)
        {
            auto entry = packer.Pack(*state);
            entry["controller"] = packer.Pack(*state->Controller);
            entry["hit_scripts"] = packer.Pack(*state->HitScripts);
            arenas[name] = entry;
        }

        data["arenas"] = arenas;
        auto packed = nlohmann::json::array();
        for (const auto& o : objects)
        {
            packed.push_back(PackObject(packer, *o));
        }

        data["objects"] = packed;
        data["fans"] = Fans->Snapshot();
        data["items"] = nlohmann::json{{"bomb", packer.Encode(Items->Bomb)}, {"decoy", packer.Encode(Items->Decoy)}};
        data["groups"] = _groups->Snapshot();
        return data;
    }

    void ScriptRuntime::Restore(const nlohmann::json& data)
    {
        // The arenas' script objects exist first, so references to them resolve.
        const auto& arenas = data.at("arenas");
        for (const auto& [name, _] : arenas.items())
        {
            GetArenaState(name);
        }

        // The objects next, so that references between them resolve.
        std::vector<std::shared_ptr<MdkObject>> created;
        for (const auto& e : data.at("objects"))
        {
            created.push_back(Recreate(e));
        }

        Objects.clear();
        for (const auto& obj : created)
        {
            if (obj != nullptr)
            {
                Objects.push_back(obj);
            }
        }

        auto packer = Scripts::Snapshot(created, FixedObjects(), [this](MdkObject* obj, const std::string& name) { return FindAnimationNamed(obj, name); });

        RestoreFields(data, packer);
        packer.Unpack(*Stats, data.at("stats"));
        for (const auto& [name, entry] : arenas.items())
        {
            auto state = GetArenaState(name);
            packer.Unpack(*state, entry);
            packer.Unpack(*state->Controller, entry.at("controller"));
            packer.Unpack(*state->HitScripts, entry.at("hit_scripts"));
        }

        const auto& objects = data.at("objects");
        for (int i = 0; i < static_cast<int>(created.size()); i++)
        {
            if (created[i] != nullptr)
            {
                packer.Unpack(*created[i], objects[i].at("variables"));
            }
        }

        Fans->Restore(data.at("fans"));
        const auto& items = data.at("items");
        packer.Decode(Node(items, "bomb"), Items->Bomb, nullptr);
        packer.Decode(Node(items, "decoy"), Items->Decoy, nullptr);
        _groups->Restore(data.at("groups"));

        // An arena reached by a teleport is shown again.
        EnterArena(CurrentArena);
        if (SecondArena.length() != 0)
        {
            EnterArena(SecondArena);
        }
    }

    void ScriptRuntime::RestoreFields(const nlohmann::json& data, Scripts::Snapshot& packer)
    {
        packer.Decode(Node(data, "global_variables"), GlobalVariables, nullptr);
        GlobalFlags = data.at("global_flags").get<int>();
        AlarmTicks = data.at("alarm_ticks").get<int>();
        SkyMode = data.at("sky_mode").get<int>();
        Option = data.at("option").get<int>();
        TownTicks = data.at("town_ticks").get<int>();
        // Saves before 1.2.3 have no total: the current difficulty's.
        const auto& townTotal = Node(data, "town_total");
        _townTotal = townTotal.is_null() ? _townTotal : townTotal.get<int>();
        CurrentArena = data.at("current_arena").get<std::string>();
        SecondArena = data.at("second_arena").get<std::string>();
        SecondActive = data.at("second_active").get<bool>();
        _nextInstance = data.at("next_instance").get<int>();
        packer.Decode(Node(data, "camera_track_pitch"), CameraTrackPitch, nullptr);
        CameraTrackTicks = data.at("camera_track_ticks").get<int>();
        packer.Decode(Node(data, "shatter_point"), ShatterPoint, nullptr);
        packer.Decode(Node(data, "shatter_direction"), ShatterDirection, nullptr);
        packer.Decode(Node(data, "alien_target"), AlienTarget, nullptr);
        AirStrike->UsedUp = data.at("strike_used").get<bool>();
    }

    nlohmann::json ScriptRuntime::PackObject(Scripts::Snapshot& packer, MdkObject& obj)
    {
        auto entry = nlohmann::json{{"type", obj.TypeName}, {"arena", obj.Arena}};
        auto boxed = std::any_of(_boxes.begin(), _boxes.end(), [&](const auto& box) { return box.get() == &obj; });
        if (boxed && obj.Model != nullptr)
        {
            entry["box"] = packer.Encode(obj.Model->Bounds.Max - obj.Model->Bounds.Min);
        }

        entry["variables"] = packer.Pack(obj);
        return entry;
    }

    std::shared_ptr<ScriptRuntime::MdkObject> ScriptRuntime::Recreate(const nlohmann::json& entry)
    {
        auto arena = entry.at("arena").get<std::string>();
        auto type = entry.at("type").get<std::string>();
        auto controller = GetArenaState(arena)->Controller;
        const auto& size = Node(entry, "box");
        if (size.is_array())
        {
            auto box = SpawnBox(*controller, Vector3::Zero(), Vector3(size[0].get<float>(), size[1].get<float>(), size[2].get<float>()), type, 0);
            auto it = std::find(Objects.begin(), Objects.end(), box);
            if (it != Objects.end())
            {
                Objects.erase(it);
            }

            return box;
        }

        auto model = FindModel(arena, type);
        if (model == nullptr)
        {
            return nullptr;
        }

        auto obj = std::make_shared<MdkObject>();
        obj->Arena = arena;
        obj->TypeName = type;
        obj->Model = model;
        return obj;
    }

    Dictionary<std::string, std::shared_ptr<ScriptRuntime::MdkObject>> ScriptRuntime::FixedObjects()
    {
        Dictionary<std::string, std::shared_ptr<MdkObject>> fixedObjects;
        for (const auto& [name, state] : _arenas)
        {
            fixedObjects[std::string(ControllerKey) + name] = state->Controller;
            fixedObjects[std::string(HitScriptsKey) + name] = state->HitScripts;
        }

        return fixedObjects;
    }

    std::shared_ptr<Formats::ModelAnimation> ScriptRuntime::FindAnimationNamed(MdkObject* obj, const std::string& name)
    {
        if (Str::StartsWith(name, CmiAnimationPrefix) && obj != nullptr)
        {
            unsigned offset = 0;
            if (!Str::TryParseHex(std::string_view(name).substr(CmiAnimationPrefix.length()), offset))
            {
                throw std::invalid_argument("Not a hex number: " + name);
            }

            return GetAnimation(*obj, static_cast<int>(offset));
        }

        auto animation = obj != nullptr ? FindArenaAnimation(obj->Arena, name) : nullptr;
        return animation != nullptr ? animation : Items->GetAnimation(name);
    }
}
