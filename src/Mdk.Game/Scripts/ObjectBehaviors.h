#pragma once

#include <array>
#include <string>
#include <string_view>

#include "Mdk.Game/Scripts/ScriptRuntime.h"

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    // Built-in behaviours of some kinds of objects, run by the engine besides their scripts
    // (the object update 0x43c7dc): doors (flag 0x100000, 0x43cc68), pickups (flag 0x200000,
    // 0x43daf4) and the holy cow (0x440074). A port of godot-mdk's object_behaviors.gd.
    //
    //   door:  CLOSED ─(Kurt near)─► OPENING ─(anim done)─► OPEN ─(Kurt away)─► CLOSING ─► CLOSED
    class ObjectBehaviors
    {
    public:
        explicit ObjectBehaviors(ScriptRuntime& runtime) : _runtime(&runtime) {}

        // Door states (obj+0x312): the low 4 bits are the engine's, the others set by door_set_flags.
        static constexpr int DoorEngineBits = 0xF;
        static constexpr int DoorOpen = 0x1;
        static constexpr int DoorOpening = 0x2;
        static constexpr int DoorClosing = 0x4;
        static constexpr int DoorClosed = 0x8;
        // The door is not solid while open (object flag 0x10).
        static constexpr int DoorOpenNotSolid = 0x10;
        static constexpr int DoorStaysOpen = 0x20;
        static constexpr int DoorLocked = 0x40;
        static constexpr int DoorLockHidden = 0x100;

    private:
        // Door state bits kept when it starts moving (0xF0), and when it opens (0x70).
        static constexpr int DoorScriptBits = 0xF0;
        static constexpr int DoorOpenKept = 0x70;
        static constexpr std::string_view LockPart = "LOCK";
        static constexpr std::string_view HatchPrefix = "HC";
        static constexpr std::string_view NoSound = "NONE";

        // Falling pickups open a chute at this speed.
        static constexpr float PickupChuteSpeed = -15.0f;
        static constexpr float PickupTurnSpeed = 180.0f;
        // Taken pickups spin this much faster while they shrink away in 30 ticks.
        static constexpr float CollectedSpin = 4.0f;
        static constexpr float ShrinkTicks = 30.0f;
        // Pickups float this high once they've landed.
        static constexpr float PickupHover = 1.5f;
        static constexpr std::string_view Chute = "SW_CHUTE";
        static constexpr int ChuteFlags = 0x820;
        static constexpr int AttachCommand = 74;
        // The chute shrinks away at this scale.
        static constexpr float ChuteGone = 0.2f;
        // Pickups that don't spin.
        static constexpr std::array<std::string_view, 3> Still = {"SW_H150", "SW_SEAL", "SW_SBONE"};
        // The pickup that runs away (0x43daf4): it starts within 20 units, stops beyond √1000
        // (across), runs at 40 units/s and turns at 270°/s; idle it replays its animation with a chance
        // of 72 in 32768 per tick.
        static constexpr std::string_view Runner = "SW_H150";
        static constexpr float RunnerStart = 20.0f;
        static constexpr float RunnerStopSquared = 1000.0f;
        static constexpr float RunnerSpeed = 40.0f;
        static constexpr float RunnerTurn = 270.0f;
        static constexpr int RunnerIdleChance = 72;
        static constexpr int ChanceRange = 32768;
        // The holy cow (0x440074): 50 units/s towards its target, 1000 damage to objects, 10 to
        // Kurt, a blast hit (event -3, type -4).
        static constexpr float CowSpeed = 50.0f;
        static constexpr int CowDamage = 1000;
        static constexpr int CowKurtDamage = 10;
        static constexpr int CowHitEvent = -3;
        static constexpr int CowHitType = -4;

        static constexpr float Dt = ScriptRuntime::Tick;
        // A door moved to its other side turns around (0x43ca00).
        static constexpr float DoorTurn = 180.0f;
        static constexpr float FullTurn = 360.0f;
        // Ticks a door's closing may take when shut at once.
        static constexpr int MaxShutTicks = 1000;

    public:
        // Opens the door when Kurt comes closer than its distance, closes it when he goes away.
        void UpdateDoor(Objects::MdkObject& obj);

        // Shuts a door at once, its closing played to the end (unless it stays open).
        void Shut(Objects::MdkObject& door);

        // Moves a door into the arena on its other side, turned around (0x43ca00): e.g. the
        // door CDANT_1 → DANT_2 at yaw 90 becomes DANT_2 → CDANT_1 at yaw 270.
        static void MoveDoor(Objects::MdkObject& door);

        // Sets up a new door (spawn_connector): masks of its LOCK and HC... parts.
        void SetupDoor(Objects::MdkObject& obj);

        // Pickups fall with a chute, then float and spin; taken ones shrink away in 30 ticks.
        void UpdatePickup(Objects::MdkObject& obj);

    private:
        // A falling pickup: it lands (SW_H150 on the floor, the others floating), or opens its chute.
        void Fall(Objects::MdkObject& obj);

        // SW_H150 runs away (0x43daf4): when Kurt comes within 20 units it plays H150_R and runs
        // at 40 units/s, turning away from him at up to 270°/s, until he's 31.6 units away (across); idle,
        // it plays H150_I now and then (about every 15 s).
        //   IDLE ──(Kurt within 20)──► RUN ──(Kurt beyond 31.6)──► IDLE
        void UpdateRunner(Objects::MdkObject& obj);

    public:
        // The holy cow (0x440074): falling, it slides towards its target at 50 units/s on each
        // axis and hits once what it lands on (1000 damage to objects, 10 to Kurt); it blows up 0.5 s
        // after landing.
        void UpdateCow(Objects::MdkObject& obj);

    private:
        // What the cow overlaps: objects of its arena lose 1000 health (a blast hit), Kurt 10.
        void CowHits(Objects::MdkObject& obj);

        // An opening door shows the arena behind it (0x43cc68): its other side, or its own arena,
        // whichever is neither Kurt's nor already the active second arena.
        void ShowBehind(Objects::MdkObject& obj);

        void Play(Objects::MdkObject& obj, const std::string& name);

        ScriptRuntime* _runtime;
    };
}
