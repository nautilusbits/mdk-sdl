#include "Mdk.Game/Scripts/Rides.h"

#include <algorithm>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/Bomber.h"
#include "Mdk.Game/Scripts/ScriptMath.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Mdk.Game/Scripts/Snowboard.h"

namespace Mdk::Game::Scripts
{
    using Objects::MdkObject;

    void Rides::Update()
    {
        if (Ridden == nullptr)
        {
            TryMount();
            return;
        }

        auto off = (Ridden->Flags & FlagRideable) == 0 || Ridden->Dead || (_board != nullptr && _runtime->Kurt->Health == 0);
        if (off)
        {
            Dismount();
            return;
        }

        if (_board == nullptr && Bomber == nullptr)
        {
            UpdateWalker();
        }
    }

    void Rides::Follow()
    {
        if (Ridden == nullptr || _board != nullptr)
        {
            return;
        }

        auto& kurt = *_runtime->Kurt;
        if (Bomber != nullptr)
        {
            kurt.Feet = Ridden->Position;
            kurt.Yaw = Ridden->Yaw;
            return;
        }

        Ridden->Position = kurt.Feet;
        Ridden->Yaw = ScriptMath::Wrap360(kurt.Yaw);
    }

    void Rides::RideWalker(MdkObject& obj)
    {
        obj.Flags |= FlagRideable;
        MountWalker(obj);
    }

    bool Rides::Hides(const MdkObject& obj) const
    {
        return Bomber != nullptr && &obj == Ridden.get() && Bomber->HidesXe();
    }

    void Rides::Lost(MdkObject& obj)
    {
        if (&obj != Ridden.get())
        {
            return;
        }

        auto hurts = _board == nullptr;
        Dismount();
        if (hurts)
        {
            _runtime->Kurt->Hurt(LostDamage);
        }
    }

    void Rides::TryMount()
    {
        auto& kurt = *_runtime->Kurt;
        if (kurt.Firing)
        {
            return;
        }

        for (auto* obj : kurt.Touched())
        {
            if (obj == nullptr || obj->Dead || (obj->Flags & FlagRideable) == 0)
            {
                continue;
            }

            auto type = Str::ToUpper(obj->TypeName);
            if (type == Board)
            {
                MountBoard(*obj);
                return;
            }

            if (std::find(Walkers.begin(), Walkers.end(), type) != Walkers.end() && kurt.OnFloor)
            {
                MountWalker(*obj);
                return;
            }

            if (std::find(Bombers.begin(), Bombers.end(), type) != Bombers.end())
            {
                MountBomber(*obj);
                return;
            }
        }
    }

    void Rides::MountBoard(MdkObject& obj)
    {
        Ridden = obj.shared_from_this();
        obj.Flags = (obj.Flags | BoardFlagsSet) & ~BoardFlagsClear;
        _runtime->Kurt->LeaveSniper();
        _board = std::make_shared<Snowboard>(*_runtime, Ridden);
        _runtime->Kurt->Ride = [board = _board](Engine::Platform::Input& input, float delta) { board->Update(input, delta); };
    }

    void Rides::MountWalker(MdkObject& obj)
    {
        auto& kurt = *_runtime->Kurt;
        Ridden = obj.shared_from_this();
        obj.Flags |= FlagRidden;
        kurt.StopFiring();
        kurt.Feet = obj.Position;
        kurt.Yaw = obj.Yaw;
        kurt.ForwardSpeed = 0.0f;
        kurt.StrafeSpeed = 0.0f;
        kurt.Walking = Game::Kurt::Kurt::Walk::Riding;
        kurt.Visible = false;
    }

    void Rides::MountBomber(MdkObject& obj)
    {
        auto& kurt = *_runtime->Kurt;
        Ridden = obj.shared_from_this();
        kurt.StopFiring();
        kurt.Visible = false;
        Bomber = std::make_shared<Scripts::Bomber>(*_runtime, Ridden);
        kurt.Ride = [bomber = Bomber](Engine::Platform::Input& input, float delta) { bomber->Update(input, delta); };
    }

    void Rides::UpdateWalker()
    {
        auto& kurt = *_runtime->Kurt;
        auto& walker = *Ridden;
        walker.Position = _runtime->KurtPosition;
        walker.Yaw = _runtime->KurtYaw;
        auto moving = kurt.ForwardSpeed != 0.0f || kurt.TurnRate != 0.0f;
        walker.AnimationRate = moving ? MdkObject::AnimationFps : 0.0f;

        auto& mixer = *_runtime->Mixer;
        if (kurt.Keys.Steer && !mixer.IsVoicePlaying(_walkerSound))
        {
            _walkerSound = mixer.Play(std::string(WalkerSound), Audio::SoundMixer::Start::New);
            mixer.SetVolume(_walkerSound, WalkerSoundVolume);
        }
        else if (!kurt.Keys.Steer)
        {
            mixer.StopVoice(_walkerSound);
            _walkerSound = 0;
        }

        if (kurt.Keys.Fire)
        {
            walker.Flags |= FlagFiring;
            mixer.Play(std::string(AlarmSound), Audio::SoundMixer::Start::Once);
            _runtime->AlarmTicks = AlarmTicks;
        }
        else if ((walker.Flags & FlagFiring) != 0)
        {
            walker.Flags &= ~FlagFiring;
            _runtime->AlarmTicks = 0;
        }
    }

    void Rides::Dismount()
    {
        auto& kurt = *_runtime->Kurt;
        if (_board != nullptr)
        {
            _board->GetOff();
            _board = nullptr;
        }
        else if (Bomber != nullptr)
        {
            // Kurt drops from where the XE is, and it shows again.
            Ridden->Flags &= ~MdkObject::FlagNotSolid;
            kurt.GetOffBoard(0.0f, 0.0f);
            Bomber = nullptr;
        }
        else if (Ridden != nullptr)
        {
            _runtime->Mixer->StopVoice(_walkerSound);
            _walkerSound = 0;
            kurt.JumpOff();
        }

        Ridden = nullptr;
    }
}
