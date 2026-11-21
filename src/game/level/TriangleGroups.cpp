#include "game/level/TriangleGroups.h"

#include <format>

namespace Mdk::Game::Level
{
    void TriangleGroups::Add(const std::shared_ptr<Formats::Arena>& arena)
    {
        Mdk::Dictionary<int, std::shared_ptr<Group>> groups;
        for (auto t = 0; t < arena->TriangleCount(); t++)
        {
            auto number = static_cast<int>(arena->TriangleFlags[t] >> GroupShift);
            auto* found = groups.Find(number);
            auto group = found != nullptr ? *found : (groups[number] = std::make_shared<Group>(number));
            group->Triangles.push_back(t);
            Disable(*arena, t);
        }

        _arenas[arena->Name] = groups;
        _data[arena->Name] = arena;
    }

    void TriangleGroups::Disable(Formats::Arena& arena, int t)
    {
        if (arena.ClipFlag != 0 || (arena.TriangleFlags[t] & Disabled) == 0)
        {
            return;
        }

        arena.TriangleFlags[t] |= static_cast<uint>(State::Hidden | State::NotSolid);
    }

    std::vector<std::shared_ptr<TriangleGroups::Group>> TriangleGroups::Of(const std::string& arena) const
    {
        std::vector<std::shared_ptr<Group>> list;
        const auto* groups = _arenas.Find(arena);
        if (groups == nullptr)
        {
            return list;
        }

        for (const auto& [number, group] : *groups)
        {
            list.push_back(group);
        }

        return list;
    }

    std::shared_ptr<TriangleGroups::Group> TriangleGroups::Get(const std::string& arena, int number) const
    {
        const auto* groups = _arenas.Find(arena);
        if (groups == nullptr)
        {
            return nullptr;
        }

        return groups->GetValueOrDefault(number);
    }

    void TriangleGroups::SetState(const std::string& arena, int number, int operation)
    {
        auto group = Get(arena, number);
        if (group == nullptr || number == 0)
        {
            return;
        }

        State state;
        switch (static_cast<Operation>(operation))
        {
            case Operation::HideAndRelease:
                state = group->State | State::Hidden | State::NotSolid;
                break;
            case Operation::Hide:
                state = group->State | State::Hidden;
                break;
            case Operation::Show:
                state = group->State & ~State::Hidden;
                break;
            case Operation::Release:
                state = group->State | State::NotSolid;
                break;
            case Operation::Solidify:
                state = group->State & ~State::NotSolid;
                break;
            default:
                state = State::None;
                break;
        }

        SetFlags(arena, *group, state);
    }

    void TriangleGroups::SetFlags(const std::string& arena, Group& group, State state)
    {
        group.State = state;

        // The original keeps the state in the triangles' flags, where the collisions read it.
        auto& flags = _data.At(arena)->TriangleFlags;
        constexpr auto StateBits = static_cast<uint>(State::Hidden | State::NotSolid);
        for (auto t : group.Triangles)
        {
            flags[t] = (flags[t] & ~StateBits) | static_cast<uint>(group.State);
        }

        if (Changed)
        {
            Changed(arena, group.Number);
        }
    }

    nlohmann::json TriangleGroups::Snapshot() const
    {
        auto data = nlohmann::json::object();
        for (const auto& [arena, groups] : _arenas)
        {
            auto changed = nlohmann::json::object();
            for (const auto& [number, group] : groups)
            {
                if (group->State == State::None && !group->Material.has_value())
                {
                    continue;
                }

                auto material = group->Material.has_value() ? nlohmann::json(*group->Material) : nlohmann::json(nullptr);
                changed[std::format("{}", group->Number)] = nlohmann::json::array({static_cast<int>(group->State), material});
            }

            if (!changed.empty())
            {
                data[arena] = changed;
            }
        }

        return data;
    }

    void TriangleGroups::Restore(const nlohmann::json& data)
    {
        for (const auto& [arena, groups] : data.items())
        {
            for (const auto& [number, node] : groups.items())
            {
                auto group = Get(arena, Str::ParseInt(number));
                if (group == nullptr)
                {
                    continue;
                }

                group->Material = node[1].is_null() ? std::nullopt : std::optional<int>(node[1].get<int>());
                SetFlags(arena, *group, static_cast<State>(node[0].get<int>()));
            }
        }
    }

    void TriangleGroups::SetMaterial(const std::string& arena, int number, int value)
    {
        auto group = Get(arena, number);
        if (group == nullptr || number == 0)
        {
            return;
        }

        group->Material = value;
        if (Changed)
        {
            Changed(arena, number);
        }
    }
}
