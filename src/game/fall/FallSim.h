#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "game/fall/FallMissile.h"
#include "game/kurt/Inventory.h"

namespace Mdk::Game::Fall
{
    // The fall before a level (fall.gd; game state 2, fall_3d.c: init 0x410018, each
    // frame 0x4114a4, the intro 0x41106c). See godot-mdk docs/gameplay.md, "The fall".
    //   intro in space (150 ticks) ──► fall (33 s) ───────────────────────────► Landed (the level)
    //                                   │ steering until 30 s, dark from 31 s
    //                                   │ radar sees Kurt ──► missiles ──► hits ──► health 0 ──► GameOver
    //                                   └ pickups fall on chutes ──► inventory
    // Kurt falls at 66.67 u/s from z 5270; the camera is 10 units above him. Pure logic: the screen
    // draws it (FallView) and plays its sounds (Sound).
    class FallSim
    {
    public:
        enum class Outcome { Running, Landed, GameOver };

        enum class KurtAnimation { Fall, Hit };

        // A model animation's speed (1 = 30 frames/s) and length.
        struct Animation
        {
            float Speed = 0;
            int FrameCount = 0;

            bool operator==(const Animation&) const = default;
        };

        // What a fall starts from: the level index (0-4), the difficulty, the pickups of
        // FALLPU_n and the animations KURTANIM, KURT_HIT, BONESANM.
        struct Setup
        {
            int Index = 0;
            Kurt::Difficulty Difficulty = Kurt::Difficulty::Normal;
            std::vector<std::string> Pickups;
            Animation Kurt;
            Animation KurtHit;
            Animation Bones;

            bool operator==(const Setup&) const = default;
        };

        static constexpr float TicksPerSecond = 30.f;
        static constexpr int IntroTicks = 150;
        // Kurt steers until 30 s, the view goes black from 31 s, the fall ends at 33 s.
        static constexpr float SteerTime = 30.f;
        static constexpr float EndTime = 33.f;
        static constexpr float KurtStartZ = 5270.f;
        static constexpr float KurtSpeed = 2000.f / 30.f;
        // Steering (vel_accel_dt): 11.765 u/s per tick up to 117.65 u/s, within ±58.82 × ±35.29.
        static constexpr float SteerStep = 200.f / 17.f;
        static constexpr float SteerMax = 2000.f / 17.f;
        static Vector2 Limit() { return Vector2(1000.f / 17.f, 600.f / 17.f); }
        // The wind level (0x5209b8): 0xC00 is full, the haze's level is it >> 8.
        static constexpr int WindFull = 0xC00;
        static constexpr int WindShift = 8;
        static constexpr int HazeFrames = 16;
        static constexpr int CrawlerFrames = 8;

        FallSim(Setup setup, std::shared_ptr<Mdk::Random> random);

        // A sound to play (FALL3D.SNI).
        std::function<void(const std::string&)> Sound;
        // A message to show (MDKFONT.FTI text name, zoomed, 3 seconds).
        std::function<void(const std::string&)> Message;

        float BeamSpeed = 0;
        int MissilesPerDetection = 0;
        float Spread = 0;
        int MissileGap = 0;
        int RadarGap = 0;
        Outcome State = Outcome::Running;
        // Whole ticks (1/30 s) of the last update.
        int Ticks = 0;
        bool InIntro() const { return _introLeft > 0; }
        // Ticks left of the intro (0 once the fall started).
        int IntroLeft() const { return Math::Max(_introLeft, 0); }
        // Seconds since the intro ended.
        float Time = 0;
        bool Dying = false;
        int Health = Kurt::Inventory::MaxHealth;
        std::shared_ptr<Kurt::Inventory> Inventory = std::make_shared<Kurt::Inventory>();
        // The pickups taken, in order (they go on to the level).
        const std::vector<std::string>& Collected() const { return _collected; }
        Vector3 KurtPosition{};
        bool KurtVisible = false;
        KurtAnimation KurtPose = KurtAnimation::Fall;
        float KurtFrame = 0;
        Vector3 CameraPosition{};
        std::optional<Vector3> BonesPosition;
        float BonesFrame = 0;
        int Wind = 0;
        int HazeFrame = 0;
        float CrawlerFrame = 0;
        // The screen towards white (1 none), darkened (1 none) and red (0 none).
        float Whiten = 1.f;
        float Dark = 0;
        float Red = 0;
        // The wind loop's volume.
        float WindVolume() const { return static_cast<float>(Wind) / WindFull * WindSoundVolume; }
        // The minecrawler's grind: from 2/3 to full over 30 s.
        float GrindVolume() const { return (MathF::Min(Time / SteerTime, 1.f) + 2.f) / 3.f; }

        // One frame of delta seconds; steer is -1, 0 or 1 per axis (x right, y up the screen).
        void Update(float delta, Vector2 steer);

        // One axis of Kurt's steering (vel_accel_dt 0x409270 with a key, 0x4687a4 without): a key
        // accelerates by a step a tick, the opposite key first resets the speed to one step, no key
        // brakes by a step a tick.
        static float Steer(float speed, int direction, float dt);

        // Whether a move crosses Kurt's box (0x45ef80).
        static bool CrossesBox(Vector3 centre, Vector3 from, Vector3 to);

        // ======== FallSim.Missiles.cs ========

        // An explosion: where, its random yaw (radians) and its age in ticks.
        class Explosion
        {
        public:
            Explosion(Vector3 position, float yaw) : Position(position), Yaw(yaw) {}

            Vector3 Position;
            float Yaw;
            float Ticks = 0;
        };

        // An explosion lasts 26 ticks (the frames of its texture).
        static constexpr int ExplosionTicks = 26;

        const std::vector<std::shared_ptr<FallMissile>>& Missiles() const { return _missiles; }
        const std::vector<std::shared_ptr<Explosion>>& Explosions() const { return _explosions; }

        // ======== FallSim.Pickups.cs ========

        // The pickups (0x4128fc, 0x41275c) of FALLPU_n, dropped one by one from above Kurt
        // (the list's last first): they fall at twice his speed, open a chute after 1-3 seconds and brake
        // to 50 u/s, spinning. Kurt takes the ones that cross his box until 30 s.
        class Pickup
        {
        public:
            Pickup(std::string name, Vector3 position, int fallTicks) : Name(std::move(name)), Position(position), FallTicks(fallTicks) {}

            std::string Name;
            Vector3 Position;
            Vector3 Velocity = Vector3(0.f, 0.f, -PickupFallSpeed);
            // Yaw in degrees.
            float Yaw = 0;
            bool Chute = false;
            int FallTicks;
            int Ticks = 0;
        };

        const std::vector<std::shared_ptr<Pickup>>& Pickups() const { return _pickups; }

        // ======== FallSim.Radar.cs ========

        enum class RadarState { Extend, Track, Retract };

        static constexpr float RadarRadius = 10.f;

        bool RadarActive = false;
        RadarState Radar = RadarState::Extend;
        // The beam's spot and radius.
        Vector3 RadarSpot{};
        float RadarSpotRadius = 0;

    private:
        static constexpr float MaxDelta = 0.1f;
        static constexpr float FadeTime = 31.f;
        static constexpr float FadeLength = 2.f;
        static constexpr float CameraAbove = 10.f;
        // After 30 s the camera brakes to a stop in 2 s.
        static constexpr float CameraBrake = KurtSpeed / 2.f;
        // The camera follows Kurt's x and y by 0.85.
        static constexpr float CameraFollow = 0.85f;
        // Kurt's collision box: ±4, ±4, ±5 (+0x198...+0x1ac).
        static Vector3 KurtBox() { return Vector3(4.f, 4.f, 5.f); }
        static constexpr float BonesSpeed = 2000.f / 27.f;
        static constexpr float BonesAbove = 20.f;
        static constexpr float WindSoundVolume = 0.625f;
        // The wind follows the camera's braking: -vz × 0.015 × 3072.
        static constexpr float WindPerSpeed = 0.015f * WindFull;
        // The minecrawler's frames run at 15 per second.
        static constexpr float CrawlerRate = 0.5f;
        static constexpr const char* FirstMessage = "FALL_T1";
        static constexpr int FirstMessageIndex = 0;
        // Bones falls with Kurt above this index (LEVEL8).
        static constexpr int BonesIndex = 3;
        // Intro timeline (ticks left): fade in from black until 90, Kurt flies in from 90 over 60,
        // the wind rises from 120 to 60, fade to white from 60.
        static constexpr int IntroFadeIn = 90;
        static constexpr int IntroFadeOut = 60;
        static constexpr float IntroFadeTicks = 60.f;
        static constexpr int IntroWindStart = 120;
        static constexpr int IntroWindFull = 61;
        static constexpr int IntroWindSteps = 12;
        static Vector3 IntroKurtStart() { return Vector3(-30.f, -10.f, 0.f); }
        static Vector3 IntroKurtMove() { return Vector3(30.f, 10.f, -10.f); }
        // Difficulty (0x410018) by Easy, Normal, Hard; i is the level index.
        // Radar beam speed: 117.65 × (1 + factor × i).
        static constexpr std::array<float, 3> BeamFactor = { 0.1f, 0.2f, 1.f / 3.f };
        // Missiles per detection: 2 + i / divisor.
        static constexpr std::array<int, 3> MissileDivisor = { 5, 3, 2 };
        static constexpr int MissilesBase = 2;
        // Missile aim spread: base - i.
        static constexpr std::array<float, 3> SpreadBase = { 7.5f, 6.5f, 5.5f };
        // Ticks between missiles: 32 - step × i (+ 0-31).
        static constexpr std::array<int, 3> MissileGapStep = { 1, 3, 5 };
        static constexpr int MissileGapBase = 32;
        // Ticks before the next radar: 63 - step × i (+ 0-63).
        static constexpr std::array<int, 3> RadarGapStep = { 3, 7, 9 };
        static constexpr int RadarGapBase = 63;
        static constexpr float FlashRate = 3.f;
        static constexpr float SettleRate = 0.5f;
        static constexpr float Flicker = 0.9f;
        // A flicker starts on 1 tick in 32.
        static constexpr int FlickerChance = 32;
        static constexpr float HitBrightness = 3.f;
        static constexpr float BrightnessEpsilon = 1e-5f;
        // Dying: the red rises by 5/255 a tick while the screen is at full brightness.
        static constexpr float RedPerTick = 5.f / 255.f;

        Setup _setup;
        std::shared_ptr<Mdk::Random> _random;
        int _introLeft = IntroTicks;
        float _tickFraction = 0;
        float _cameraSpeed = -KurtSpeed;
        bool _kurtFinished = false;
        bool _bonesPassed = false;
        // Palette effects (0x5209c8 and its target and rate).
        float _brightness = 0;
        float _target = 1.f;
        float _rate = SettleRate;
        std::vector<std::string> _collected;
        Vector3 _kurtVelocity{};
        float _bonesTicks = 0;

        // --- The intro in space (0x41106c) ---
        void UpdateIntro(int ticks);

        void StartFall();

        // --- The fall (0x4114a4) ---
        void UpdateFall(float dt, int ticks, Vector2 steer);

        void UpdateKurt(float dt, int ticks, Vector2 steer);

        // Hitting a limit zeroes that speed.
        static std::pair<Vector2, Vector2> Clamp(Vector2 p, Vector2 velocity);

        // KURTANIM loops; a hit plays KURT_HIT once (looping KURTANIM forever once he's dying).
        void AnimateKurt(int ticks);

        // The camera (0x413440): above Kurt until 30 s, then it brakes to a stop in 2 s.
        void UpdateCamera(float dt);

        // --- Bones (0x4133b8) ---
        void UpdateBones(float dt, int ticks);

        // --- Palette effects (0x5209c8, 0x410cc0, 0x410c28) ---
        void UpdatePalette(float dt, int ticks);

        // The brightness fades in over the first second, flashes on detections, launches and
        // hits, and flickers now and then.
        void Flash(float dt, int ticks);

        void SetScreen(float whiten, float dark);

        // A random point within Kurt's limits.
        Vector2 RandomPoint();

        // Uniform in ±range.
        float RandomRange(float range);

        // ======== FallSim.Missiles.cs ========

        static constexpr int MissileGapRandom = 32;
        static constexpr std::array<const char*, 2> ExplodeSounds = { "EXPLODE1", "EXPLODE2" };
        static constexpr std::array<const char*, 7> HitSounds = { "K_HIT1", "K_HIT2", "K_HIT3", "K_HIT4", "K_HIT5", "K_HIT6", "K_HIT7" };
        // Damage of a hit: 4 on easy, 4 + 0-7 on normal, twice that on hard.
        static constexpr int HitDamage = 4;
        static constexpr int HitDamageRandom = 8;
        static constexpr int HardFactor = 2;
        // Screen flashes: a detection dims the target brightness by 0.5 (not below 0.75), a launch by
        // 0.2 (not below 0.5).
        static constexpr float DetectDim = 0.5f;
        static constexpr float DetectFloor = 0.75f;
        static constexpr float LaunchDim = 0.2f;
        static constexpr float LaunchFloor = 0.5f;

        std::vector<std::shared_ptr<FallMissile>> _missiles;
        std::vector<std::shared_ptr<Explosion>> _explosions;
        int _missileQueue = 0;
        int _missileWait = 0;

        void UpdateMissiles(float dt, int ticks);

        void Launch();

        // A missile hits Kurt: an explosion, damage and a sound; at 0 health he's dying.
        void Hit(FallMissile& missile);

        // Explosions follow Kurt down while their frames play.
        void UpdateExplosions(int ticks);

        // ======== FallSim.Pickups.cs ========

        static constexpr float PickupFallSpeed = 2.f * KurtSpeed;
        static constexpr float PickupChuteSpeed = 50.f;
        // Degrees per second under the chute.
        static constexpr float PickupSpin = 30.f;
        // Dropped 15 units above Kurt, within 95 % of his limits.
        static constexpr float PickupAbove = 15.f;
        static constexpr float PickupSpread = 0.95f;
        // The first pickup comes 31-62 ticks after the intro, the next ones 31-158 ticks apart; each
        // opens its chute after 30-93 ticks.
        static constexpr int PickupWait = 31;
        static constexpr int PickupFirstRandom = 32;
        static constexpr int PickupNextRandom = 128;
        static constexpr int ChuteTicks = 30;
        static constexpr int ChuteRandom = 64;
        static constexpr std::array<const char*, 2> CollectSounds = { "K_COLL1", "K_COLL2" };

        std::vector<std::string> _pickupNames;
        std::vector<std::shared_ptr<Pickup>> _pickups;
        int _pickupWait = 0;

        void StartPickups();

        void UpdatePickups(float dt, int ticks);

        // Falls, then brakes to 50 u/s and spins under its chute.
        void MovePickup(Pickup& pickup, float dt, int ticks);

        void Take(Pickup& pickup);

        void Spawn(const std::string& name);

        // ======== FallSim.Radar.cs ========

        static constexpr float RadarExtendSpeed = 3000.f;
        static constexpr float RadarRetractSpeed = 1500.f;
        static constexpr float RadarSee = 15.f;
        // The beam's spot stays 3 units below Kurt.
        static constexpr float RadarBelow = 3.f;
        // Tracking: v = 0.75 v + 0.25 × beam speed × unit(target - spot).
        static constexpr float RadarInertia = 0.75f;
        // A new target after 30 ticks, or once within 2.94 units (on both axes).
        static constexpr int RadarTargetTicks = 30;
        static constexpr float RadarNear = 2.94f;
        // Choices of the next target: at most 9 of Kurt and the pickups, and 3 random points.
        static constexpr int RadarChoices = 9;
        static constexpr int RadarRandomChoices = 3;
        // The first radar comes 7-22 ticks after the intro, later ones the gap + 0-63 ticks after.
        static constexpr int RadarFirstWait = 7;
        static constexpr int RadarFirstRandom = 16;
        static constexpr int RadarGapRandom = 64;

        int _radarWait = 0;
        float _radarHeight = 0;
        Vector2 _radarTarget{};
        Vector2 _radarVelocity{};
        int _radarTicks = 0;

        void StartRadars() { _radarWait = _random->Next(RadarFirstRandom) + RadarFirstWait; }

        void UpdateRadar(float dt, int ticks);

        // The next radar once its wait is over (none after 30 s).
        bool StartRadar(int ticks);

        // The spot rises towards Kurt, its xy and radius growing with it.
        void Extend(float dt);

        void Track(float dt, int ticks);

        void Retract(float dt);

        // Kurt was seen (K_SEEN): the radar retracts, missiles are queued and the screen flashes.
        void Detected();

        // The next point the radar goes to (0x412a38): Kurt, a falling pickup or a random point.
        Vector2 ChooseTarget();
    };
}
