#include "Mdk.Game/Scripts/ScriptRuntime.h"

#include <algorithm>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/Scripts/GameStats.h"

namespace Mdk::Game::Scripts
{
    const std::unordered_set<std::string> ScriptRuntime::ScriptedTargets = {"XEARTH", "XFORK"};

    int ScriptRuntime::KurtDamage(MdkObject& obj, int damage)
    {
        return KurtDamage(obj, damage, obj.Health);
    }

    int ScriptRuntime::KurtDamage(MdkObject& obj, int damage, int health)
    {
        if (Lethality == Scripts::Lethality::Normal || damage <= 0 || !IsOneHitTarget(obj))
        {
            return damage;
        }

        return std::max(damage, health);
    }

    bool ScriptRuntime::IsOneHitTarget(MdkObject& obj)
    {
        return GameStats::IsEnemy(obj.TypeName) && !ScriptedTargets.contains(Str::ToUpper(obj.TypeName));
    }
}
