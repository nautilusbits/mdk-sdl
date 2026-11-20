#pragma once

#include <optional>
#include <string_view>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Game/Hud/BomberOverlay.h"
#include "Mdk.Game/Objects/MdkObject.h"

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;

    // The XE bomber ride of level 7's DANT_5 (0x46bf40; bomber.gd, godot-mdk
    // docs/gameplay.md "The XE bomber"): Kurt sits in the flyer while its script flies it along a path;
    // the view looks straight down from it and a cursor aims XBN_BOMBs at the ground.
    //
    //   mount ──► view sinks 50 → 0 u above Kurt in 2 s (then the XE isn't drawn)
    //         ──► script unlocks (flag 0x4000000 cleared) ──► cursor, bombs, HUD
    //         ──► script clears the rideable flag ──► Kurt drops from the XE (Rides)
    class Bomber
    {
    public:
        // The cursor (0x573b0c): starts at the centre, kept in a box; keys accelerate it by 1/3
        // px/tick² up to 10 px/tick, it brakes by 2/3 px/tick²; the mouse moves it by 1/3 of its motion.
        static constexpr Vector2 CursorCentre = Vector2(300.0f, 180.0f);

        // The XE's health is kept at 10000; what it loses goes to Kurt (0x46a77c).
        static constexpr int Health = 10000;

        Bomber(ScriptRuntime& runtime, std::shared_ptr<Objects::MdkObject> xe);

    private:
        // The view (0x4183f0): 50 units above Kurt at the mount, sinking 25 units/s.
        static constexpr float CameraHeight = 50.0f;
        static constexpr float CameraDescent = 25.0f;
        static constexpr Vector2 CursorMin = Vector2(128.0f, 64.0f);
        static constexpr Vector2 CursorMax = Vector2(472.0f, 296.0f);
        static constexpr float CursorAccel = 1.0f / 3.0f;
        static constexpr float CursorSpeed = 10.0f;
        static constexpr float CursorBrake = 2.0f / 3.0f;
        static constexpr float MouseScale = 1.0f / 3.0f;
        // Bombs (0x573c64): 10, one back per second.
        static constexpr int Bombs = 10;
        static constexpr float RefillTime = 1.0f;
        // The aim: a ray from Kurt through the cursor with the view's focal length (600 / zoom
        // 2.4), as long as 1000 times that direction; the bomb starts 5 below Kurt and falls by 32 u/s²
        // (the XE's gravity) onto the hit point, or for 2.5 s when nothing is hit.
        static constexpr float Focal = 250.0f;
        static constexpr float RayLength = 1000.0f;
        static constexpr float DropBelow = 5.0f;
        static constexpr float FallGravity = 32.0f;
        static constexpr float MissTime = 2.5f;
        // The bomb: a thrown item of kind 0x81 (0x43deac) living 900 ticks, flags 0x818a6.
        static constexpr std::string_view Bomb = "XBN_BOMB";
        static constexpr int BombTicks = 900;
        static constexpr int BombFlags = 0x818a6;
        static constexpr std::string_view DropSound = "DROP";
        // Objects the aim passes through (not solid, not a target).
        static constexpr int Unaimed = Objects::MdkObject::FlagNotSolid | Objects::MdkObject::FlagNotTarget;

        ScriptRuntime* _runtime;
        std::shared_ptr<Objects::MdkObject> _xe;
        Vector2 _speed;
        float _refill = RefillTime;
        bool _fireHeld = false;

    public:
        // Height of the view above Kurt.
        float ViewHeight = CameraHeight;
        Vector2 Cursor = CursorCentre;
        int BombsLeft = Bombs;

        // Whether the script still holds the controls (and the HUD is off).
        bool Locked() const;

        // Whether the view is inside the XE, which then isn't drawn.
        bool HidesXe() const { return ViewHeight == 0.0f; }

        // The HUD's sight, or null while locked.
        std::optional<Hud::BomberSight> Shown() const;

        // Each step (Kurt's ride): Kurt goes with the XE, the view sinks, the cursor moves and drops bombs.
        void Update(Engine::Platform::Input& input, float delta);

        // A cursor speed after a step: accelerating with the key (0x4688d0: the other way
        // starts over), else braking.
        static float CursorStep(float speed, float axis, float ticks);

        // The view's ray through a cursor pixel: right is (s, −c), the top of the view the heading (c, s).
        static Vector3 AimDirection(Vector2 cursor, float yaw);

        // The time to fall height from rest (2.5 s when it isn't below).
        static float FallTime(float height);

    private:
        void MoveCursor(Engine::Platform::Input& input, float delta);

        void RefillBombs(float delta);

        // A bomb from under the XE, thrown so it falls onto the point under the cursor.
        //
        //        Kurt ●───────► (vx, vy, 0)
        //             ╎  ╲ falls by gravity for t = √(2h / g)
        //             ╎     ╲
        //   ──────────┴───────✕ hit point (ray through the cursor)
        void Drop();

        // Where the ray through the cursor meets an object of Kurt's arena or the arena.
        Vector3 Aim(Vector3 kurt, Vector2 cursor, float yaw, bool& hit);

        // Where the segment first meets a part of an object of Kurt's arena (alive, not flagged
        // 0x30: the hidden XE doesn't count), or null.
        std::optional<Vector3> FirstObject(Vector3 start, Vector3 end);

        // Hits on Kurt went to the XE (TakesHits); Kurt takes them, and if
        // he dies the XE does too (its death script drops him).
        void PassDamage();

        static float Axis(Engine::Platform::Input& input, Engine::Platform::Key positive, Engine::Platform::Key negative);
    };
}
