#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Game/Kurt/Inventory.h"
#include "Mdk.Game/Stream/StreamTube.h"
#include "Mdk.Game/Stream/TubeBasis.h"
#include "Mdk.Game/Stream/WatcomRandom.h"

namespace Mdk::Game::Stream
{
    // Something flying along the tube: where along it (segments), across it, its speed
    // (segments/s), its yaw around the tube's forward axis and its animation frame.
    class Thing
    {
    public:
        float T = 0;
        float X = 0;
        float Z = 0;
        float Speed = 0;
        float Yaw = 0;
        float Frame = 0;
    };

    // Kurt's steering keys: -1, 0 or 1 to the right and up.
    struct Steering
    {
        int Right = 0;
        int Up = 0;

        bool operator==(const Steering&) const = default;
    };

    // An animation's length and speed (1 is 30 frames per second).
    struct Clip
    {
        int FrameCount = 0;
        float Speed = 0;

        bool operator==(const Clip&) const = default;
    };

    // The screen's fade (as the fall's screen shader): towards white by Whiten
    // (1 = none), the red of death, then darkened by Dark (1 = none).
    struct Fade
    {
        float Whiten = 0;
        float Dark = 0;
        float Red = 0;

        bool operator==(const Fade&) const = default;
    };

    // What happened during an update, for the screen's sounds and messages.
    enum class FlightEvent { BonesCame, KurtDied, WallHit, BonusTaken, Ended };

    // How the stream ended: Kurt goes on, or he died in the Gunter tube (game over).
    enum class Outcome { Survived, Died };

    // The stream's play (game state 5, each frame 0x4352ac; stream.gd): Kurt flies down the
    // tube, steering, drifting on bends and bouncing off its walls; the lights fly back, the health
    // bonus walks ahead, then Bones' crane picks Kurt up (or Gunter leads him to the planet). No
    // drawing: StreamScreen shows it.
    //   fade in ─► Kurt (steer, drift, walls) ─► others ─► camera ─┐  segment 177 or 1 health:
    //      ▲                                                       │  Bones (normal) / tube's end (Gunter)
    //      └───────────────────────────────────────────────────────┘  ─► fade out ─► Ended
    class Flight
    {
    public:
        static constexpr float TicksPerSecond = 30.f;
        static constexpr float NeutralYaw = 90.f;
        // Bones comes after this segment (normal variant); the rescue ends past this animation frame.
        static constexpr int RescueSegment = 177;
        static constexpr int LowHealth = 1;

    private:
        static constexpr int BlinkMask = 31;
        static constexpr float MaxDelta = 0.1f;
        // Kurt's speed (segments/s), its recovery (segments/s²), its loss per hit and its minimum.
        static constexpr float Speed = 6.f;
        static constexpr float SpeedRecovery = 1.f;
        static constexpr float SpeedLoss = 0.9f;
        static constexpr float SpeedMin = 4.5f;
        // Kurt starts this far into the tube, and the tube moves on when he's this far past its tail + 1.
        static constexpr float KurtStart = 0.75f;
        // Steering (°/s, ° around the neutral yaw 90 and pitch 0), and the sideways speed at full turn.
        static constexpr float TurnRate = 180.f;
        static constexpr float TurnLimit = 45.f;
        static constexpr float SideSpeed = 25.f;
        // On bends Kurt keeps going straight (a lagging direction) and drifts outwards at this rate.
        static constexpr float Drift = 100.f;
        static constexpr float DirectionLag = 0.75f;
        // Without control he drifts back to the axis (units/s).
        static constexpr float AxisReturn = 6.25f;
        static constexpr float WallMargin = 1.5f;
        // Health lost per wall hit: a base and a random extra (0 or 1 times the step) per difficulty.
        static constexpr std::array<int, 3> HitDamage = { 2, 2, 4 };
        static constexpr std::array<int, 3> HitExtra = { 0, 1, 2 };
        static constexpr int RescueEndFrame = 80;
        // The health bonus: where it starts, its speed, its reach and the health it gives.
        static constexpr float BonusStart = 16.f;
        static constexpr float BonusSpeed = 5.48f;
        static constexpr float BonusReach = 5.f;
        static constexpr int BonusHealth = 150;
        static constexpr float GuntaStart = 5.f;
        // The camera: from the axis behind Kurt and a point past him, looking two segments ahead.
        static constexpr float CameraBack = 0.75f;
        static constexpr float CameraAhead = 2.f;
        static constexpr float CameraBlend = 0.4f;
        static constexpr float CameraPast = 1.375f;
        // The background scrolls with the view's turns (0x438bfc) over its 600 x 360 pixels.
        static constexpr float BackgroundWidth = 600.f;
        static constexpr float BackgroundHeight = 360.f;
        static constexpr float ScrollX = 300.f;
        static constexpr float ScrollY = 180.f;
        // The fade lasts a second; on death it goes on to 2 (red, then black).
        static constexpr float FadeDeath = 2.f;
        static constexpr std::array<const char*, 7> HurtSounds = { "HURT1", "HURT2", "HURT3", "HURT4", "HURT5", "HURT6", "HURT7" };

    public:
        // A sprite the tube spawned and its place.
        struct Sprite
        {
            StreamTube::Spawn Spawn;
            std::shared_ptr<Stream::Thing> Thing;
        };

        Flight(std::shared_ptr<StreamTube> tube, StreamTube::Kind kind, Game::Kurt::Difficulty difficulty, std::shared_ptr<WatcomRandom> random,
            int health, Clip kurt, Clip rescue, Clip other);

        std::shared_ptr<Thing> Kurt;

        // Kurt plays the rescue (BONESANIM) once Bones has come.
        Clip KurtClip() const { return Bones == nullptr ? _kurtClip : _rescueClip; }
        float Pitch = 0;
        Vector3 KurtPosition{};
        std::shared_ptr<Thing> Bones;
        // The health bonus (SWH150, normal tube) or Gunter carrying Bones (Gunter tube).
        std::shared_ptr<Thing> Other;
        int Health = 0;
        bool Ended = false;
        Stream::Outcome Outcome() const { return Health < LowHealth ? Outcome::Died : Outcome::Survived; }
        // Counts ticks (0-31) for the health's blink.
        int Blink = 0;
        Stream::Fade Fade = { 0.f, 1.f, 0.f };
        float Time = 0;
        // The camera: its eye, forward, right and up (MDK coordinates).
        Vector3 Eye{};
        Vector3 Forward{};
        Vector3 Right{};
        Vector3 Up() const { return _screenUp; }
        // The background's top-left pixel.
        Vector2 BackgroundOffset{};
        // Events and sounds of the last update.
        std::vector<FlightEvent> Events;
        std::vector<std::string> Sounds;
        const std::shared_ptr<StreamTube>& Tube() const { return _tube; }
        StreamTube::Kind Kind() const { return _kind; }
        const std::vector<Sprite>& Sprites() const { return _sprites; }
        int LightCount = 0;

        // Esc: the stream fades out (not in the original).
        void Stop() { _ending = true; }

        // One frame of delta seconds.
        void Update(float delta, Steering steering);

        // Where something is and how it's turned: the tube's frame at its place, turned by its yaw.
        Matrix4x4 Transform(const Thing& thing) const
        {
            return (_tube->Frame(thing.T, _screenUp) * TubeBasis::Rotation(Vector3::UnitZ(), thing.Yaw)).At(_tube->Place(thing.T, thing.X, thing.Z));
        }

        // Kurt's transform: the tube's frame turned by his yaw and pitch.
        Matrix4x4 KurtTransform() const;

        // A looping animation's frame for a thing.
        int OtherFrame(const Thing& thing) const { return _otherClip.FrameCount == 0 ? 0 : ToInt(thing.Frame) % _otherClip.FrameCount; }

        // Kurt's frame: looping while he flies, held at the last of the rescue.
        int KurtFrame() const { return Bones == nullptr ? ToInt(Kurt->Frame) : Math::Min(ToInt(Kurt->Frame), _rescueClip.FrameCount - 1); }

        // Bones' frame of the rescue, held at the last.
        int BonesFrame() const { return Math::Min(ToInt(Bones != nullptr ? Bones->Frame : 0.f), _rescueClip.FrameCount - 1); }

    private:
        std::shared_ptr<StreamTube> _tube;
        StreamTube::Kind _kind;
        Game::Kurt::Difficulty _difficulty;
        std::shared_ptr<WatcomRandom> _random;
        Clip _kurtClip;
        Clip _rescueClip;
        Clip _otherClip;
        std::vector<Sprite> _sprites;
        Vector3 _direction = Vector3::UnitY();
        Vector3 _screenUp = Vector3::UnitZ();
        std::optional<std::vector<Vector3>> _viewRows;
        float _fade = 0;
        bool _ending = false;
        float _tickFraction = 0;

        // Kurt steers until Bones comes or he dies.
        bool Controlled() const { return Bones == nullptr && Health > 0; }

        // The rescue after segment 177 or at 1 health, or the end of the Gunter tube.
        void CheckEnd();

        // The fade (0x520860): in from white (black after LEVEL8) over 1 s, out the same way
        // once the stream ends; on death in the Gunter tube a red flash, then black. False once over.
        bool UpdateFade(float dt);

        // The lights and the planet the tube made: they fly along it. Returns how many.
        int AddSpawns();

        // Moves something along the tube (0x436440); false once it leaves the rings still alive.
        bool Move(Thing& thing, float dt);

        // The lights and the planet, the health bonus or Gunter, and Bones.
        void UpdateOthers(float dt);

        // The walking health bonus (0x435b18): caught within 5 units, it gives 150 health.
        void TakeBonus();

        // Kurt (0x435c4c): speed, the tube moving on, his animation, drift, steering and walls.
        void UpdateKurt(float dt, Steering steering);

        // On bends Kurt keeps going straight and drifts to the outer wall; without control he
        // goes back to the axis.
        void DriftKurt(const TubeBasis& frame, float dt);

        // Steering turns Kurt up to 45° either way and moves him sideways; he turns back when
        // the keys are released. Left and up turn him left and up.
        void Steer(float dt, Steering steering);

        // A wall hit pushes Kurt back towards the axis, turns him inwards, hurts him and slows him down.
        void Collide(Vector3 centre, const TubeBasis& frame);

        // Wall damage by difficulty. At 0 health Kurt dies in the Gunter tube, else Bones comes for him.
        void Hurt();

        // Bones picks Kurt up: both play BONESANIM (the crane, the rope, Bones and Kurt) once.
        void StartRescue();

        // The camera (0x4352ac, 0x436828): behind Kurt and to his side, looking at the axis two
        // segments ahead, its roll carried over from frame to frame.
        void UpdateCamera();

        static float PositiveMod(float value, float modulus) { return std::fmod(std::fmod(value, modulus) + modulus, modulus); }

        static float MoveToward(float from, float to, float step)
        {
            return MathF::Abs(to - from) <= step ? to : from + MathF::Sign(to - from) * step;
        }
    };
}
