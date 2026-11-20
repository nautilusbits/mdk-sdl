#pragma once

#include <array>
#include <memory>
#include <string_view>

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    class Bomber;
    class ScriptRuntime;
    class Snowboard;

    // The objects Kurt rides (0x573c30, damp_control 0x466368; rides.gd): the snowboard
    // of level 4 (XSNOWB, Snowboard), the XD2 of level 7's DANT_9
    // (0x46a840) and the XE bomber of its DANT_5 (Bomber). A script makes
    // an object rideable (flag 0x2000000); Kurt gets on when he stands on it (or, for the XD2 and the
    // XE, touches it) without firing, and off when the script clears the flag.
    //
    //   rideable + Kurt on it ──► riding ──(flag cleared)──► off (the XD2: a jump; the board: thrown;
    //                                                        the XE: a drop)
    class Rides
    {
    public:
        // Object flags: rideable, controls locked (the board, the XE), ridden, the rider fires (the XD2).
        static constexpr int FlagRideable = 0x2000000;
        static constexpr int FlagLocked = 0x4000000;

        explicit Rides(ScriptRuntime& runtime) : _runtime(&runtime) {}

        // The object Kurt rides, if any.
        std::shared_ptr<Objects::MdkObject> Ridden;

        // The bomber ride, while Kurt is on an XE.
        std::shared_ptr<Scripts::Bomber> Bomber;

        // Each tick, after Kurt moved: getting on, riding the XD2, getting off.
        void Update();

        // Each step, after the ticks: the XD2 is where Kurt is, Kurt where the XE is. Kurt
        // moves each step, they each tick: else every other frame draws them apart (a ghost).
        void Follow();

        // Whether Kurt rides the snowboard.
        bool OnBoard() const { return _board != nullptr; }

        // Puts Kurt on a walker at once, as touching it does (tests: --ride).
        void RideWalker(Objects::MdkObject& obj);

        // Whether an object isn't drawn: the XE once the view is inside it.
        bool Hides(const Objects::MdkObject& obj) const;

        // Whether hits on Kurt go to the object he rides (the XD2, the XE).
        bool TakesHits() const { return Ridden != nullptr && _board == nullptr; }

        // The ridden object went (0x43d734): Kurt is off, and but for the board he takes 50 damage.
        void Lost(Objects::MdkObject& obj);

    private:
        static constexpr int FlagRidden = 0x80000;
        static constexpr int FlagFiring = 0x1;
        // On the board: Kurt passes through it (+0x800) and it's no platform any more (−0x800100).
        static constexpr int BoardFlagsSet = 0x80800;
        static constexpr int BoardFlagsClear = 0x800100;
        static constexpr std::string_view Board = "XSNOWB";
        static constexpr std::array<std::string_view, 2> Walkers = {"XD", "XD2"};
        static constexpr std::array<std::string_view, 2> Bombers = {"XE", "X_STRIKE"};
        // The XD2: DUMMY at volume 0x2000 while steered, ALERT and the alarm while firing; Kurt
        // takes 50 damage if it goes while he rides it.
        static constexpr std::string_view WalkerSound = "DUMMY";
        static constexpr std::string_view AlarmSound = "ALERT";
        static constexpr int WalkerSoundVolume = 0x2000;
        static constexpr int AlarmTicks = 10;
        static constexpr int LostDamage = 50;

        ScriptRuntime* _runtime;
        std::shared_ptr<Snowboard> _board;
        int _walkerSound = 0;

        void TryMount();

        void MountBoard(Objects::MdkObject& obj);

        // The XD2: Kurt goes where it is, facing its way, and walks it (without strafing or
        // jumping). His moves ignore it (damp_collide_move), so he doesn't collide with it.
        void MountWalker(Objects::MdkObject& obj);

        // The XE (also in the air): Kurt isn't drawn and goes with it.
        void MountBomber(Objects::MdkObject& obj);

        // The XD2 goes with Kurt, animating while it moves; its sound while the keys are held;
        // firing sounds the alarm.
        void UpdateWalker();

        void Dismount();
    };
}
