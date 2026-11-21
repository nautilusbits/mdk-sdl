#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include "core/Dictionary.h"
#include "core/DotNet.h"
#include "core/Flags.h"
#include "formats/Arena.h"

namespace Mdk::Game::Level
{
    // The triangles of each arena that share a group number (the top byte of their flags),
    // which scripts show, hide, retexture or make destructible together. Group 0 is the rest of the
    // arena and never changes. The drawing (LevelView) and, later, the collisions read it.
    class TriangleGroups
    {
    public:
        enum class State
        {
            None = 0,
            // Not drawn (triangle flag 0x10).
            Hidden = 0x10,
            // Not collided with (triangle flag 0x20).
            NotSolid = 0x20,
        };

        // group_set_state operations.
        enum class Operation
        {
            HideAndRelease = 0,
            Hide = 2,
            Show = 3,
            Release = 4,
            Solidify = 5,
        };

        // The triangle flag the original's arena activation (0x40d46c(a,1)) turns into not drawn
        // and not solid (all in group 0: NONE walls, stray floors). The 1996 demo's clip flag instead.
        static constexpr uint Disabled = 0x2;

        class Group
        {
        public:
            explicit Group(int number) : Number(number) {}

            int Number;
            std::vector<int> Triangles;
            TriangleGroups::State State = State::None;
            // The material value every triangle got from group_set_texture, if any.
            std::optional<int> Material;
        };

        // A group changed: its arena and number.
        std::function<void(const std::string&, int)> Changed;

        void Add(const std::shared_ptr<Formats::Arena>& arena);

        std::vector<std::shared_ptr<Group>> Of(const std::string& arena) const;

        std::shared_ptr<Group> Get(const std::string& arena, int number) const;

        // group_set_state: 0 hides and releases, 2/3 hide/show, 4/5 release/solidify,
        // others show and solidify.
        void SetState(const std::string& arena, int number, int operation);

        // The groups scripts changed, for a full save: arena → group → [state, material or null].
        nlohmann::json Snapshot() const;

        // Puts the groups of a full save back.
        void Restore(const nlohmann::json& data);

        // group_set_texture: every triangle of the group takes material value.
        void SetMaterial(const std::string& arena, int number, int value);

    private:
        static constexpr int GroupShift = 24;

        Mdk::Dictionary<std::string, Mdk::Dictionary<int, std::shared_ptr<Group>>> _arenas;
        Mdk::Dictionary<std::string, std::shared_ptr<Formats::Arena>> _data;

        // A retail triangle flagged Disabled is hidden and not solid (e.g. LEVEL8
        // GUNT_2's invisible diagonal wall).
        static void Disable(Formats::Arena& arena, int t);

        void SetFlags(const std::string& arena, Group& group, State state);
    };

    MDK_FLAGS(TriangleGroups::State)
}
