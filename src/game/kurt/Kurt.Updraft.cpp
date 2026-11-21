#include "game/kurt/Kurt.h"

#include <cmath>
#include <limits>

// Kurt in the fans' updrafts (damp_gravity, kurt.gd _update_updraft): inside a fan's box the fan
// sets his vertical speed and opens his chute, with the FAN sound looping; out of them he can't
// rise faster than 40 u/s.

namespace Mdk::Game::Kurt
{
    void Kurt::UpdateUpdraft(float delta)
    {
        auto vz = Updraft && Health > 0 ? Updraft(VerticalSpeed, delta) : std::numeric_limits<float>::quiet_NaN();
        if (std::isnan(vz))
        {
            _mixer.StopVoice(_fanVoice);
            _fanVoice = 0;
            InUpdraft = false;
            if (Current != State::Knocked && VerticalSpeed > UpdraftExitSpeed)
            {
                VerticalSpeed = UpdraftExitSpeed;
            }

            return;
        }

        VerticalSpeed = vz;
        if (!ChuteOpen)
        {
            ChuteOpen = true;
            _mixer.Play("CHUTEOUT");
        }

        if (!InUpdraft)
        {
            InUpdraft = true;
            _fanVoice = _mixer.PlayLooped(FanSound);
        }
    }
}
