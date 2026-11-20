#include "Mdk.Game/Kurt/Kurt.h"

#include <algorithm>

// Kurt on a ride (rides.gd, kurt.gd ride/walk_mode; godot-mdk docs/gameplay.md "Rides"): the
// snowboard and the XE bomber move him instead of his walking (Ride); in the XD2 he walks hidden,
// without strafing or jumping (Walk::Riding). The rides and the end of a level also pick his frames
// and the camera's view.
//   Update ──Ride set?──► the ride's step (board: Glide; bomber: follows the XE)
//          └─Riding────► turn, forward, gravity, no items, no gun (the XD2 follows him)

namespace Mdk::Game::Kurt
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Collision::ArenaSpace;

    void Kurt::Glide(Vector3 velocity, float delta)
    {
        auto move = velocity * delta;
        if (move != Vector3::Zero())
        {
            Feet = Contact(_space.Move(Feet, move, ArenaSpace::Motion::Walk, WalkSlide, Nearby(Feet + move))).Feet;
        }

        if (OnFloor)
        {
            GlueDownhill(move, delta);
        }
        else
        {
            VerticalSpeed = MathF::Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
        }

        Fall(delta);
        UpdateTouched();
    }

    void Kurt::UpdateGun(Input& input, float delta)
    {
        UpdateFiring(input);
        UpdateMuzzle(delta);
    }

    void Kurt::JumpOff()
    {
        Walking = Walk::Normal;
        Visible = true;
        VerticalSpeed = JumpSpeed;
        ForwardSpeed = MaxSpeed;
        OnFloor = false;
        SetState(State::RunJump);
    }

    void Kurt::GetOffBoard(float forward, float strafe)
    {
        Ride = nullptr;
        Pose = std::nullopt;
        TopDownHeight = std::nullopt;
        CameraPivot = DefaultPivot;
        Visible = true;
        ForwardSpeed = forward;
        StrafeSpeed = strafe;
        SetState(State::Still);
    }

    void Kurt::RideWalker(Input& input, float forward, float delta)
    {
        VerticalSpeed = MathF::Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
        Fall(delta);
        Keys = RideKeys{ forward != 0.f || Axis(input, Key::TurnLeft, Key::TurnRight) != 0.f, input.IsDown(Key::Fire) };
        SetState(State::Still);
    }

    const std::vector<Kurt::ArenaContact>& Kurt::TakeContacts()
    {
        _taken.clear();
        _taken.insert(_taken.end(), _contacts.begin(), _contacts.end());
        _contacts.clear();
        return _taken;
    }

    ArenaSpace::Result Kurt::Contact(const ArenaSpace::Result& result)
    {
        if (result.Hit() && result.Bsp != nullptr)
        {
            _contacts.push_back({ result.Bsp->Arena, result.Triangle });
        }

        return result;
    }

    void Kurt::UpdateTouched()
    {
        _touchedNow.clear();
        ArenaSpace::Touching(Feet, Nearby(Feet), _touchedNow);
        if (Platform != nullptr && std::find(_touchedNow.begin(), _touchedNow.end(), Platform) == _touchedNow.end())
        {
            _touchedNow.push_back(Platform);
        }

        _touched = _touchedNow;
    }
}
