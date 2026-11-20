#include "Mdk.Game/Fall/FallSim.h"

#include <cmath>
#include <limits>
#include <tuple>

namespace Mdk::Game::Fall
{
    namespace
    {
        // C#'s Vector3 indexer.
        float Axis(Vector3 v, int axis)
        {
            return axis == 0 ? v.X : (axis == 1 ? v.Y : v.Z);
        }
    }

    FallSim::FallSim(Setup setup, std::shared_ptr<Mdk::Random> random)
        : _setup(std::move(setup)), _random(std::move(random))
    {
        auto i = _setup.Index;
        auto d = static_cast<int>(_setup.Difficulty);
        BeamSpeed = SteerMax * (1.f + BeamFactor[d] * i);
        MissilesPerDetection = MissilesBase + i / MissileDivisor[d];
        Spread = SpreadBase[d] - i;
        MissileGap = MissileGapBase - MissileGapStep[d] * i;
        RadarGap = RadarGapBase - RadarGapStep[d] * i;
        _pickupNames = _setup.Pickups;
        Inventory->Difficulty = _setup.Difficulty;
    }

    void FallSim::Update(float delta, Vector2 steer)
    {
        if (State != Outcome::Running)
        {
            return;
        }

        auto dt = MathF::Min(delta, MaxDelta);
        _tickFraction += dt * TicksPerSecond;
        auto ticks = ToInt(_tickFraction);
        _tickFraction -= ticks;
        Ticks = ticks;
        if (_introLeft > 0)
        {
            UpdateIntro(ticks);
            return;
        }

        UpdateFall(dt, ticks, steer);
    }

    // --- The intro in space (0x41106c) ---

    void FallSim::UpdateIntro(int ticks)
    {
        _introLeft -= ticks;
        auto left = Math::Max(_introLeft, 0);

        // Fade in from black, then plain, then fade to white.
        if (left > IntroFadeIn)
        {
            SetScreen(1.f, 1.f - (left - IntroFadeIn) / IntroFadeTicks);
        }
        else if (left < IntroFadeOut)
        {
            SetScreen(1.f - (IntroFadeOut - left) / IntroFadeTicks, 1.f);
        }
        else
        {
            SetScreen(1.f, 1.f);
        }

        if (left < IntroWindStart)
        {
            Wind = left < IntroWindFull ? WindFull : ((IntroWindStart - left) * IntroWindSteps / IntroFadeOut) << WindShift;
            HazeFrame = (HazeFrame + ticks) % HazeFrames;
        }

        CameraPosition = Vector3::Zero();

        // Kurt flies in from the camera to 10 units below it, easing out.
        if (left < IntroFadeIn)
        {
            KurtVisible = true;
            AnimateKurt(ticks);
            auto f = 1.f - MathF::Pow(1.f - MathF::Min((IntroFadeIn - left) / IntroFadeTicks, 1.f), 2.f);
            KurtPosition = IntroKurtStart() + IntroKurtMove() * f;
        }

        if (_introLeft <= 0)
        {
            StartFall();
        }
    }

    void FallSim::StartFall()
    {
        _introLeft = 0;
        Time = 0.f;
        StartRadars();
        StartPickups();
        Wind = WindFull;
        KurtPosition = Vector3(0.f, 0.f, KurtStartZ);
        if (_setup.Index > BonesIndex)
        {
            BonesPosition = Vector3(0.f, 0.f, KurtStartZ + BonesAbove);
        }

        if (_setup.Index == FirstMessageIndex && Message)
        {
            Message(FirstMessage);
        }
    }

    // --- The fall (0x4114a4) ---

    void FallSim::UpdateFall(float dt, int ticks, Vector2 steer)
    {
        Time += dt;
        UpdateKurt(dt, ticks, steer);
        UpdateCamera(dt);
        UpdateRadar(dt, ticks);
        UpdateMissiles(dt, ticks);
        UpdateExplosions(ticks);
        UpdatePickups(dt, ticks);
        UpdateBones(dt, ticks);
        UpdatePalette(dt, ticks);
        HazeFrame = (HazeFrame + ticks) % HazeFrames;
        CrawlerFrame = std::fmod(CrawlerFrame + ticks * CrawlerRate, static_cast<float>(CrawlerFrames));

        if (Dying || State != Outcome::Running)
        {
            return;
        }

        if (Time > EndTime)
        {
            State = Outcome::Landed;
        }
    }

    void FallSim::UpdateKurt(float dt, int ticks, Vector2 steer)
    {
        AnimateKurt(ticks);
        auto velocity = Vector2(_kurtVelocity.X, _kurtVelocity.Y);
        auto p = Vector2(KurtPosition.X, KurtPosition.Y);
        if (Time <= SteerTime && !Dying)
        {
            velocity.X = Steer(velocity.X, MathF::Sign(steer.X), dt);
            velocity.Y = Steer(velocity.Y, MathF::Sign(steer.Y), dt);
        }
        else if (Time > SteerTime)
        {
            if (!_kurtFinished)
            {
                _kurtFinished = true;
                if (Sound)
                {
                    Sound("K_FINISH");
                }
            }

            // Pulled back to the centre: v = (v − 2 p) / 2 a tick.
            for (auto i = 0; i < ticks; i++)
            {
                velocity = (velocity - 2.f * p) / 2.f;
            }
        }

        std::tie(p, velocity) = Clamp(p + velocity * dt, velocity);
        KurtPosition = Vector3(p, KurtPosition.Z - KurtSpeed * dt);
        _kurtVelocity = Vector3(velocity, -KurtSpeed);
    }

    std::pair<Vector2, Vector2> FallSim::Clamp(Vector2 p, Vector2 velocity)
    {
        if (MathF::Abs(p.X) > Limit().X)
        {
            p.X = MathF::Sign(p.X) * Limit().X;
            velocity.X = 0.f;
        }

        if (MathF::Abs(p.Y) > Limit().Y)
        {
            p.Y = MathF::Sign(p.Y) * Limit().Y;
            velocity.Y = 0.f;
        }

        return { p, velocity };
    }

    float FallSim::Steer(float speed, int direction, float dt)
    {
        auto step = SteerStep * TicksPerSecond * dt;
        if (direction == 0)
        {
            return MathF::Abs(speed) <= step ? 0.f : speed - MathF::Sign(speed) * step;
        }

        if (speed * direction < 0.f)
        {
            return direction * SteerStep;
        }

        return Math::Clamp(speed + direction * step, -SteerMax, SteerMax);
    }

    void FallSim::AnimateKurt(int ticks)
    {
        auto animation = KurtPose == KurtAnimation::Hit ? _setup.KurtHit : _setup.Kurt;
        KurtFrame += ticks * animation.Speed;
        if (KurtFrame < animation.FrameCount)
        {
            return;
        }

        if (KurtPose == KurtAnimation::Hit && !Dying)
        {
            KurtPose = KurtAnimation::Fall;
            KurtFrame = 0.f;
            return;
        }

        KurtFrame = std::fmod(KurtFrame, static_cast<float>(animation.FrameCount));
    }

    void FallSim::UpdateCamera(float dt)
    {
        auto z = KurtPosition.Z + CameraAbove;
        if (Time > SteerTime)
        {
            _cameraSpeed = MathF::Min(_cameraSpeed + CameraBrake * dt, 0.f);
            z = CameraPosition.Z + _cameraSpeed * dt;
            Wind = ToInt(-_cameraSpeed * WindPerSpeed);
        }

        CameraPosition = Vector3(CameraFollow * KurtPosition.X, CameraFollow * KurtPosition.Y, z);
    }

    bool FallSim::CrossesBox(Vector3 centre, Vector3 from, Vector3 to)
    {
        auto min = centre - KurtBox();
        auto max = centre + KurtBox();
        if (Vector3::Clamp(to, min, max) == to)
        {
            return true;
        }

        // Slabs: the part of the segment inside each axis's range.
        auto enter = 0.f;
        auto leave = 1.f;
        auto d = to - from;
        for (auto axis = 0; axis < 3; axis++)
        {
            if (MathF::Abs(Axis(d, axis)) < std::numeric_limits<float>::denorm_min())
            {
                if (Axis(from, axis) < Axis(min, axis) || Axis(from, axis) > Axis(max, axis))
                {
                    return false;
                }

                continue;
            }

            auto a = (Axis(min, axis) - Axis(from, axis)) / Axis(d, axis);
            auto b = (Axis(max, axis) - Axis(from, axis)) / Axis(d, axis);
            enter = MathF::Max(enter, MathF::Min(a, b));
            leave = MathF::Min(leave, MathF::Max(a, b));
        }

        return enter <= leave;
    }

    // --- Bones (0x4133b8) ---

    void FallSim::UpdateBones(float dt, int ticks)
    {
        if (!BonesPosition)
        {
            return;
        }

        auto bones = *BonesPosition;
        _bonesTicks += ticks;
        BonesFrame = std::fmod(_bonesTicks * _setup.Bones.Speed, static_cast<float>(_setup.Bones.FrameCount));
        bones.Z = bones.Z - BonesSpeed * dt;
        BonesPosition = bones;
        if (!_bonesPassed && BonesPosition->Z < KurtPosition.Z)
        {
            _bonesPassed = true;
            if (Sound)
            {
                Sound("BONES");
            }
        }
    }

    // --- Palette effects (0x5209c8, 0x410cc0, 0x410c28) ---

    void FallSim::UpdatePalette(float dt, int ticks)
    {
        if (Dying)
        {
            // Red with the skull growing, then black; at 0 it's game over.
            if (_brightness >= 1.f)
            {
                Red = MathF::Min(Red + ticks * RedPerTick, 1.f);
            }

            _brightness -= dt;
            SetScreen(1.f, Math::Clamp(_brightness, 0.f, 1.f));
            if (_brightness <= 0.f)
            {
                State = Outcome::GameOver;
            }

            return;
        }

        Flash(dt, ticks);
        auto dark = Time > FadeTime ? MathF::Max(1.f - (Time - FadeTime) / FadeLength, 0.f) : 1.f;
        SetScreen(Math::Clamp(_brightness, 0.f, 1.f), dark);
    }

    void FallSim::Flash(float dt, int ticks)
    {
        if (Time < 1.f)
        {
            _brightness = Time;
            return;
        }

        if (MathF::Abs(_brightness - _target) > BrightnessEpsilon)
        {
            auto step = _rate * dt;
            _brightness = MathF::Abs(_target - _brightness) <= step ? _target : _brightness + MathF::Sign(_target - _brightness) * step;
            return;
        }

        if (_target < 1.f)
        {
            _target = 1.f;
            _rate = SettleRate;
            return;
        }

        for (auto i = 0; i < ticks; i++)
        {
            if (_random->Next(FlickerChance) != 0)
            {
                continue;
            }

            _target = Flicker;
            _rate = SettleRate;
            break;
        }
    }

    void FallSim::SetScreen(float whiten, float dark)
    {
        Whiten = whiten;
        Dark = dark;
    }

    Vector2 FallSim::RandomPoint()
    {
        // x first: C++ leaves the order of constructor arguments open.
        auto x = RandomRange(Limit().X);
        auto y = RandomRange(Limit().Y);
        return Vector2(x, y);
    }

    float FallSim::RandomRange(float range)
    {
        return (_random->NextSingle() * 2.f - 1.f) * range;
    }
}
