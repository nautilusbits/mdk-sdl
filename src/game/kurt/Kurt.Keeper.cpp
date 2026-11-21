#include "game/kurt/Kurt.h"

// A platform that keeps Kurt (flag 0x800000, set_targetable 2): landed on it, he stays on it
// (damp_gravity 0x469efc sets 0x573b8c). Each step his XY is its XY and his feet stay at least as
// high above it as when he landed (0x46634e), whatever the arena's walls; he doesn't walk
// (vel_accel skipped). It lets him go when it loses the flag, or on a teleport or a ride.
// E.g. LEVEL3's hover ship (XGS) flies him through HMO_9's low opening.

namespace Mdk::Game::Kurt
{
    void Kurt::CheckKeeper()
    {
        if (_keeper != nullptr && (!Keeper || !Keeper(_keeper)))
        {
            _keeper = nullptr;
        }
    }

    std::optional<Kurt::PlatformHit> Kurt::KeptPlatform(float dz)
    {
        if (_keeper == nullptr || VerticalSpeed > 0.f || !Keeper)
        {
            return std::nullopt;
        }

        auto at = Keeper(_keeper);
        if (!at)
        {
            return std::nullopt;
        }

        auto top = at->Z + _keptHeight;
        if (Feet.Z + dz > top)
        {
            return std::nullopt;
        }

        return PlatformHit{ top, _keeper };
    }

    void Kurt::KeepOn(Objects::MdkObject* owner)
    {
        if (_keeper != nullptr || !Keeper)
        {
            return;
        }

        auto at = Keeper(owner);
        if (!at)
        {
            return;
        }

        _keeper = owner;
        _keptHeight = Feet.Z - at->Z;
    }

    void Kurt::StayOnKeeper()
    {
        if (_keeper == nullptr || !Keeper)
        {
            return;
        }

        auto at = Keeper(_keeper);
        if (!at)
        {
            return;
        }

        Platform = _keeper;
        auto top = at->Z + _keptHeight;
        Feet = Vector3(at->X, at->Y, MathF::Max(Feet.Z, top));
        if (Feet.Z > top)
        {
            return;
        }

        OnFloor = true;
        VerticalSpeed = MathF::Max(VerticalSpeed, 0.f);
    }
}
