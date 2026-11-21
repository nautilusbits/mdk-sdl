#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/Numerics.h"
#include "game/scripts/Ribbon.h"

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;

    // A twister of the tornado item (0x40741c): it spirals out of the tornado for 2 seconds,
    // then splits into one twister per object of the arena (0x407774), each chasing its object
    // (0x407974) for 5 seconds, bouncing off walls and hurting whatever it's inside by 2 health per
    // tick. A port of godot-mdk's twister.gd; drawn as a Ribbon along its last positions.
    //
    //   spiral (720°/s, 15 out and up at 1440°) ──► split ──► chase: v = 0.9 v + 20 towards the target
    class Twister
    {
    public:
        Twister(ScriptRuntime& runtime, std::string arena, Vector3 origin, float yaw);

        std::string Arena;
        Vector3 Position;
        // The ribbon along its last positions (0x439690).
        std::shared_ptr<Scripts::Ribbon> Ribbon = std::make_shared<Scripts::Ribbon>();

        // One tick; twisters gets the split ones. Returns false when it's gone.
        bool Tick(std::vector<std::shared_ptr<Twister>>& twisters);

    private:
        static constexpr float SpiralRate = 720.0f;
        static constexpr float SpiralEnd = 1440.0f;
        static constexpr float SpiralSize = 15.0f;
        static constexpr float SpiralSpeed = 200.0f;
        // 150 ticks of life, a tick less for each tick inside an object.
        static constexpr int Lifetime = 150;
        static constexpr float Keep = 0.9f;
        static constexpr float Pull = 20.0f;
        static constexpr int Damage = 2;
        // Stops this far off a wall it meets.
        static constexpr float WallGap = 0.1f;
        static constexpr int HitEvent = -1;

        ScriptRuntime* _runtime;
        float _yaw;
        Vector3 _origin;
        float _angle = 0.0f;
        Vector3 _velocity;
        std::shared_ptr<Objects::MdkObject> _target;
        bool _chasing = false;
        int _life = Lifetime;

        void Spiral(std::vector<std::shared_ptr<Twister>>& twisters);

        // The objects it's inside lose 2 health a tick (and the twister a tick of life).
        void HurtObjects();

        // The spiral is over: one twister for each object of the arena (0x407774).
        void Split(std::vector<std::shared_ptr<Twister>>& twisters);

        void MoveTo(Vector3 next);

        // A wall's normal turned against the move.
        static Vector3 Facing(Vector3 normal, Vector3 move) { return Vector3::Dot(normal, move) > 0.0f ? -normal : normal; }
    };
}
