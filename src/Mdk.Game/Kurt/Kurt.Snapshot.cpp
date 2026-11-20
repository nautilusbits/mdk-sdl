#include "Mdk.Game/Kurt/Kurt.h"

#include "Mdk.Game/Kurt/Scope.h"

namespace Mdk::Game::Kurt
{
    nlohmann::json Kurt::Snapshot() const
    {
        auto slots = nlohmann::json::array();
        for (const auto& s : Inventory->Slots())
        {
            slots.push_back(nlohmann::json::array({ static_cast<int>(s.Item), s.Count }));
        }

        auto ammo = nlohmann::json::array();
        for (auto a : Inventory->Ammo())
        {
            ammo.push_back(a);
        }

        return nlohmann::json{
            { "position", nlohmann::json::array({ Feet.X, Feet.Y, Feet.Z }) },
            { "yaw", Yaw },
            { "health", Health },
            { "invulnerable", Invulnerable },
            { "clip_rounds", Scope->ClipRounds },
            { "clip_time", Scope->ClipTime },
            { "inventory", nlohmann::json{
                { "slots", slots },
                { "selected", Inventory->Selected },
                { "ammo", ammo },
                { "selected_ammo", Inventory->SelectedAmmo },
                { "super_chain_gun", Inventory->SuperChainGun },
            } },
        };
    }

    void Kurt::Restore(const nlohmann::json& data)
    {
        const auto& p = data.at("position");
        Teleport(Vector3(p.at(0).get<float>(), p.at(1).get<float>(), p.at(2).get<float>()), data.at("yaw").get<float>());
        Health = data.at("health").get<int>();
        Invulnerable = data.at("invulnerable").get<float>();
        Scope->ClipRounds = data.at("clip_rounds").get<int>();
        Scope->ClipTime = data.at("clip_time").get<float>();
        const auto& inventory = data.at("inventory");
        std::vector<Game::Kurt::Inventory::Slot> slots;
        for (const auto& s : inventory.at("slots"))
        {
            slots.push_back({ static_cast<Game::Kurt::Inventory::Item>(s.at(0).get<int>()), s.at(1).get<int>() });
        }

        std::vector<int> ammo;
        for (const auto& a : inventory.at("ammo"))
        {
            ammo.push_back(a.get<int>());
        }

        Inventory->Restore(slots, inventory.at("selected").get<int>(), ammo, inventory.at("selected_ammo").get<int>(),
            inventory.at("super_chain_gun").get<int>());
    }
}
