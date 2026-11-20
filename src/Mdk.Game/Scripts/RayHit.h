#pragma once

#include <string>

#include "Mdk.Core/Numerics.h"

namespace Mdk::Game::Scripts
{
    // ScriptRuntime.RayHit, declared apart because Effects and Debris (held by ScriptRuntime)
    // use it; ScriptRuntime::RayHit names it.
    // A ray's hit: point, plane normal, arena and triangle group.
    struct RayHit
    {
        Vector3 Point;
        Vector3 Normal;
        std::string Arena;
        int Group = 0;

        bool operator==(const RayHit&) const = default;
    };
}
