#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Game/Scripts/GameStats.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"

namespace Mdk::Formats
{
    class Model;
    class ModelAnimation;
}

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    // Packs and unpacks objects' public fields as JSON for full saves (F2, the original's
    // AREN/ALIE packets; snapshot.gd). Values become plain data:
    //
    //   MdkObject in the object list  → {"#obj": index}
    //   arena script object            → {"#fixed": key}   ("arena:NAME", "hits:NAME")
    //   ModelAnimation                 → {"#anim": name}
    //   Vector3, Matrix4x4, (int, int) → arrays of numbers;  non-finite floats → strings
    //   the model, sound voices        → left out (found again by the type, restarted by name)
    class Snapshot
    {
        using MdkObject = Game::Objects::MdkObject;

    public:
        using FindAnimation = std::function<std::shared_ptr<Formats::ModelAnimation>(MdkObject*, const std::string&)>;

        // objects are referred to by index, fixedObjects
        // (key → object) by key; findAnimation turns an animation's name back into
        // the animation for an object.
        Snapshot(std::vector<std::shared_ptr<MdkObject>> objects, const Dictionary<std::string, std::shared_ptr<MdkObject>>& fixedObjects,
            FindAnimation findAnimation);

        // A short, stable hash of a JSON text (tests compare a save with its reload).
        static std::string Hash(const std::string& json);

        // Pack<T>/Unpack<T>: C# reflects over the public fields; C++ lists each packed type's
        // public fields in the .cpp (in declaration order), one overload per type.

        // The public instance fields of an object, as plain data.
        nlohmann::json Pack(MdkObject& obj);
        nlohmann::json Pack(GameStats& obj);
        nlohmann::json Pack(ScriptRuntime::ArenaState& obj);

        // Sets an object's fields back; read-only arrays are filled in place.
        void Unpack(MdkObject& obj, const nlohmann::json& data, MdkObject* owner = nullptr);
        void Unpack(GameStats& obj, const nlohmann::json& data, MdkObject* owner = nullptr);
        void Unpack(ScriptRuntime::ArenaState& obj, const nlohmann::json& data, MdkObject* owner = nullptr);

        // Encode(object?): one overload per field type (null → JSON null).
        nlohmann::json Encode(int value) const;
        nlohmann::json Encode(bool value) const;
        nlohmann::json Encode(const std::string& value) const;
        nlohmann::json Encode(float value) const;
        nlohmann::json Encode(Vector2 value) const;
        nlohmann::json Encode(Vector3 value) const;
        nlohmann::json Encode(const Matrix4x4& value) const;
        nlohmann::json Encode(MdkObject::PointPair value) const;
        nlohmann::json Encode(const std::shared_ptr<MdkObject>& value) const;
        nlohmann::json Encode(const std::shared_ptr<Formats::ModelAnimation>& value) const;
        nlohmann::json Encode(const std::shared_ptr<Formats::Model>& value) const;

        template <typename T>
        nlohmann::json Encode(const std::vector<T>& list) const
        {
            auto array = nlohmann::json::array();
            for (const auto& item : list)
            {
                array.push_back(Encode(item));
            }

            return array;
        }

        template <typename T, std::size_t N>
        nlohmann::json Encode(const std::array<T, N>& list) const
        {
            auto array = nlohmann::json::array();
            for (const auto& item : list)
            {
                array.push_back(Encode(item));
            }

            return array;
        }

        // A value of type type (null: the type's default); <paramref
        // name="owner"/> is the object it belongs to (animations are looked up in its arena).
        // One overload per field type; a null node gives the type's default.
        void Decode(const nlohmann::json& node, int& value, MdkObject* owner) const;
        void Decode(const nlohmann::json& node, bool& value, MdkObject* owner) const;
        void Decode(const nlohmann::json& node, std::string& value, MdkObject* owner) const;
        void Decode(const nlohmann::json& node, float& value, MdkObject* owner) const;
        void Decode(const nlohmann::json& node, Vector2& value, MdkObject* owner) const;
        void Decode(const nlohmann::json& node, Vector3& value, MdkObject* owner) const;
        void Decode(const nlohmann::json& node, Matrix4x4& value, MdkObject* owner) const;
        void Decode(const nlohmann::json& node, MdkObject::PointPair& value, MdkObject* owner) const;
        void Decode(const nlohmann::json& node, std::shared_ptr<MdkObject>& value, MdkObject* owner) const;
        void Decode(const nlohmann::json& node, std::shared_ptr<Formats::ModelAnimation>& value, MdkObject* owner) const;

        // The arrays and lists objects have (no reflection on element types, for AOT).
        template <typename T>
        void Decode(const nlohmann::json& node, std::vector<T>& list, MdkObject* owner) const
        {
            list.clear();
            if (node.is_null())
            {
                return;
            }

            for (const auto& item : node)
            {
                T value{};
                Decode(item, value, owner);
                list.push_back(std::move(value));
            }
        }

        // Read-only arrays are filled in place (as many as both hold).
        template <typename T, std::size_t N>
        void Decode(const nlohmann::json& node, std::array<T, N>& list, MdkObject* owner) const
        {
            if (node.is_null())
            {
                return;
            }

            for (std::size_t i = 0; i < N && i < node.size(); i++)
            {
                Decode(node[i], list[i], owner);
            }
        }

    private:
        static constexpr std::string_view ObjectKey = "#obj";
        static constexpr std::string_view FixedKey = "#fixed";
        static constexpr std::string_view AnimationKey = "#anim";
        static constexpr int HashHexDigits = 16;
        // Fields rebuilt by their owner rather than saved.
        static constexpr std::array<std::string_view, 3> Skipped = {"Model", "LoopSound", "TrackedVoice"};

        std::vector<std::shared_ptr<MdkObject>> _objects;
        Dictionary<MdkObject*, int> _index;
        Dictionary<std::string, std::shared_ptr<MdkObject>> _fixed;
        Dictionary<MdkObject*, std::string> _fixedKeys;
        FindAnimation _findAnimation;

        // A float, or its text when JSON can't hold it (NaN, infinities).
        static nlohmann::json Number(float f);

        static float Float(const nlohmann::json& node);

        std::shared_ptr<MdkObject> Reference(const nlohmann::json& data) const;
    };
}
