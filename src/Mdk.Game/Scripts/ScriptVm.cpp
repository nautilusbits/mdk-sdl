#include "Mdk.Game/Scripts/ScriptVm.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/AnimatedTextures.h"
#include "Mdk.Game/Scripts/Debris.h"
#include "Mdk.Game/Scripts/Effects.h"
#include "Mdk.Game/Scripts/Fans.h"
#include "Mdk.Game/Scripts/GameStats.h"
#include "Mdk.Game/Scripts/Rides.h"
#include "Mdk.Game/Scripts/SniperRounds.h"
#include "Mdk.Game/Hud/Messages.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Scripts/Items.h"
#include "Mdk.Game/Scripts/ObjectBehaviors.h"
#include "Mdk.Game/Scripts/ObjectMotion.h"
#include "Mdk.Game/Scripts/ScriptMath.h"

namespace Mdk::Game::Scripts
{
    using namespace ScriptMath;
    using Formats::Scripts::BranchAction;
    using Formats::Scripts::ScriptDecoder;
    using Formats::Scripts::Variable;
    using Objects::MdkObject;

    namespace
    {
        // Vector3's indexer (C#'s v[axis]).
        float Axis(Vector3 v, int axis)
        {
            return axis == 0 ? v.X : axis == 1 ? v.Y : v.Z;
        }

        // C#'s (string)operand: a string operand, or "" for null.
        const std::string& Text(const Operand& operand)
        {
            static const std::string Empty;
            if (auto text = std::get_if<std::string>(&operand))
            {
                return *text;
            }

            return Empty;
        }

        // C#'s operand as object?[] (null when it isn't a list).
        const OperandList* AsList(const Operand& operand)
        {
            return std::get_if<OperandList>(&operand);
        }
    }

    void ScriptVm::Run(MdkObject& obj)
    {
        if (obj.Restart == 0 || obj.Dead)
        {
            return;
        }

        _runtime->SelectTarget(obj);
        auto pc = obj.Restart;
        if (obj.WaitTime > 0.0f)
        {
            obj.WaitTime -= Dt;
            if (obj.WaitTime > 0.0f)
            {
                return;
            }

            obj.WaitTime = 0.0f;
            pc = obj.WaitResume;
        }

        auto count = 0;
        while (true)
        {
            auto ins = _decoder->Decode(pc);
            if (ins == nullptr)
            {
                // Unknown opcode: the script stops (the original only writes to its debug log).
                obj.Restart = 0;
                return;
            }

            if (ins->Opcode == ScriptDecoder::End)
            {
                obj.HitEvent = 0;
                return;
            }

            count++;
            _opcodeCount = count;
            if (count > MaxOpcodesPerFrame)
            {
                obj.Restart = 0;
                return;
            }

            pc = Execute(obj, *ins);
            if (pc == Yield || obj.Dead)
            {
                return;
            }
        }
    }

    int ScriptVm::Branch(MdkObject& obj, const Instruction& ins, bool condition)
    {
        const auto& action = ins.Action;
        if (!action)
        {
            return ins.Next;
        }

        switch (action->Type)
        {
            case BranchAction::Kind::Goto:
                return condition ? Goto(obj, action->Target) : ins.Next;
            case BranchAction::Kind::Gosub:
                return condition ? Gosub(obj, ins.Next, action->Target) : ins.Next;
            case BranchAction::Kind::GosubElse:
                return Gosub(obj, ins.Next, condition ? action->Target : action->Else);
            case BranchAction::Kind::Return:
                return condition ? Return(obj) : ins.Next;
            default:
                return ins.Next;
        }
    }

    int ScriptVm::Goto(MdkObject& obj, int target)
    {
        if (target == 0)
        {
            obj.Restart = 0;
            return Yield;
        }

        obj.Restart = target;
        obj.LevelTimers[obj.GosubReturns.size()] = 0.0f;
        return target;
    }

    int ScriptVm::Gosub(MdkObject& obj, int returnPc, int target)
    {
        if (static_cast<int>(obj.GosubReturns.size()) >= GosubDepth || target == 0)
        {
            obj.Restart = 0;
            return Yield;
        }

        obj.GosubReturns.push_back(returnPc);
        obj.GosubRestarts.push_back(obj.Restart);
        obj.Restart = target;
        obj.LevelTimers[obj.GosubReturns.size()] = 0.0f;
        return target;
    }

    int ScriptVm::Return(MdkObject& obj)
    {
        if (obj.GosubReturns.empty())
        {
            obj.Restart = 0;
            return Yield;
        }

        obj.Restart = Pop(obj.GosubRestarts);
        return Pop(obj.GosubReturns);
    }

    int ScriptVm::Pop(std::vector<int>& stack)
    {
        auto value = stack.back();
        stack.pop_back();
        return value;
    }

    void ScriptVm::ClearStack(MdkObject& obj)
    {
        obj.GosubReturns.clear();
        obj.GosubRestarts.clear();
    }

    float ScriptVm::Value(MdkObject& obj, const Operand& operand)
    {
        auto variable = std::get_if<Variable>(&operand);
        if (variable == nullptr)
        {
            return F(operand);
        }

        return Variables(obj, variable->Kind)[variable->Index >= 0 && variable->Index < VariableCount ? variable->Index : 0];
    }

    std::span<float> ScriptVm::Variables(MdkObject& obj, int source)
    {
        switch (source)
        {
            case 0:
                return _runtime->GlobalVariables;
            case 1:
                return _runtime->GetArenaState(obj.Arena)->Variables;
            case 2:
                return obj.Variables;
            default:
                return obj.Linked != nullptr ? std::span<float>(obj.Linked->Variables) : NoVariables();
        }
    }

    std::span<float> ScriptVm::NoVariables()
    {
        _noVariables.fill(0.0f);
        return _noVariables;
    }

    bool ScriptVm::IsDoor(MdkObject& obj)
    {
        return (obj.Flags & MdkObject::FlagDoor) != 0;
    }

    int ScriptVm::GetFlags(MdkObject& obj, int source)
    {
        switch (source)
        {
            case 0:
                return _runtime->GlobalFlags;
            case 1:
                return _runtime->GetArenaState(obj.Arena)->Flags;
            case 2:
                return obj.ScriptFlags;
            case DoorFlags:
                return IsDoor(obj) ? obj.DoorState : 0;
            default:
                return obj.Linked != nullptr ? obj.Linked->ScriptFlags : 0;
        }
    }

    void ScriptVm::SetFlags(MdkObject& obj, int source, int value)
    {
        switch (source)
        {
            case 0:
                _runtime->GlobalFlags = value;
                break;
            case 1:
                _runtime->GetArenaState(obj.Arena)->Flags = value;
                break;
            case 2:
                obj.ScriptFlags = value;
                break;
            case DoorFlags:
                if (IsDoor(obj))
                {
                    obj.DoorState = value;
                }

                break;
            default:
                if (obj.Linked != nullptr)
                {
                    obj.Linked->ScriptFlags = value;
                }

                break;
        }
    }

    bool ScriptVm::Compare(float value, const OperandList& cond)
    {
        static const Operand Null{};
        return Compare(value, cond[0], cond[1], cond.size() > 2 ? cond[2] : Null);
    }

    bool ScriptVm::Compare(float value, const Operand& op, const Operand& first, const Operand& second)
    {
        auto a = F(first);
        switch (I(op))
        {
            case 1:
                return value < a;
            case 2:
                return value > a;
            case 3:
                return value < a + Epsilon;
            case 4:
                return value > a - Epsilon;
            case 5:
                return MathF::Abs(value - a) < Epsilon;
            case 6:
                return MathF::Abs(value - a) >= Epsilon;
            case 7:
                return value >= a && value <= F(second);
            case 8:
                return value <= a || value >= F(second);
            default:
                return false;
        }
    }

    int ScriptVm::LastFollowerId(MdkObject& leader)
    {
        std::optional<int> maxId;
        for (const auto& other : _runtime->Objects)
        {
            if (other->Leader.get() == &leader && !other->Dead)
            {
                maxId = Math::Max(maxId.value_or(std::numeric_limits<int>::min()), other->InstanceId);
            }
        }

        return maxId.value_or(-1);
    }

    std::shared_ptr<MdkObject> ScriptVm::LastChainLink(MdkObject& leader)
    {
        constexpr int ChainCommand = 30;
        std::shared_ptr<MdkObject> best;
        for (const auto& other : _runtime->Objects)
        {
            if (IsArenaOther(leader, *other) && other->Leader.get() == &leader && other->MoveCommand == ChainCommand
                && (best == nullptr || other->InstanceId > best->InstanceId))
            {
                best = other;
            }
        }

        return best;
    }

    bool ScriptVm::IsArenaOther(MdkObject& obj, MdkObject& other)
    {
        return &other != &obj && !other.Dead && other.Arena == obj.Arena;
    }

    int ScriptVm::CountOthers(MdkObject& obj, const Operand& type, Counting counting)
    {
        auto count = 0;
        for (const auto& other : _runtime->Objects)
        {
            if (IsArenaOther(obj, *other) && Is(other->TypeName, type)
                && (counting == Counting::Alive ? other->Health > 0 : ScriptRuntime::MayCommand(obj, *other)))
            {
                count++;
            }
        }

        return count;
    }

    std::shared_ptr<MdkObject> ScriptVm::FirstOther(MdkObject& obj, const Operand& type, int instance)
    {
        constexpr int AnyInstance = -1;
        for (const auto& other : _runtime->Objects)
        {
            if (IsArenaOther(obj, *other) && Is(other->TypeName, type) && (instance == AnyInstance || other->InstanceId == instance))
            {
                return other;
            }
        }

        return nullptr;
    }

    bool ScriptVm::IsNamed(MdkObject& other, const Operand& name)
    {
        std::string_view text = Text(name);
        std::string_view type = other.TypeName;
        auto id = std::to_string(other.InstanceId);
        return text.size() == type.size() + 1 + id.size() && text[type.size()] == '_'
            && Str::EqualsIgnoreCase(text.substr(0, type.size()), type)
            && Str::EqualsIgnoreCase(text.substr(type.size() + 1), id);
    }

    std::shared_ptr<MdkObject> ScriptVm::Bomb()
    {
        auto bomb = _runtime->Items->Bomb;
        return bomb != nullptr && !bomb->Dead ? bomb : nullptr;
    }

    int ScriptVm::S16(int value)
    {
        constexpr int SignBit = 0x8000;
        constexpr int Range = 0x10000;
        return value >= SignBit ? value - Range : value;
    }

    int ScriptVm::WeightedTarget(const OperandList& entries)
    {
        auto total = 0;
        for (const auto& entry : entries)
        {
            total += I(L(entry).at(0));
        }

        auto r = _runtime->Rng->Next(Math::Max(total, 1));
        for (const auto& entry : entries)
        {
            r -= I(L(entry).at(0));
            if (r < 0)
            {
                return I(L(entry).at(1));
            }
        }

        return 0;
    }

    int ScriptVm::RandomTarget(const OperandList& entries)
    {
        return entries.empty() ? 0 : I(L(entries[_runtime->Rng->Next(static_cast<int>(entries.size()))]).at(0));
    }

    bool ScriptVm::Is(const std::string& a, const Operand& b)
    {
        return Str::EqualsIgnoreCase(a, Text(b));
    }

    int ScriptVm::Execute(MdkObject& obj, const Instruction& ins)
    {
        const auto& o = ins.Operands;
        auto& rng = *_runtime->Rng;
        switch (ins.Opcode)
        {
            case 1: // set_restart
                obj.Restart = ins.Next;
                break;
            case 9: // stop_script
                obj.Restart = 0;
                ClearStack(obj);
                return Yield;
            case 12: // goto (random target)
                return Goto(obj, RandomTarget(L(o[0])));
            case 94: // goto_weighted
                return Goto(obj, WeightedTarget(L(o[0])));
            case 252: // gosub (random target)
                return Gosub(obj, ins.Next, RandomTarget(L(o[0])));
            case 95: // gosub_weighted
                return Gosub(obj, ins.Next, WeightedTarget(L(o[0])));
            case 253: // return
                return Return(obj);
            case 125: // clear_gosub_stack
                ClearStack(obj);
                break;
            case 64: // wait
                obj.WaitTime = MathF::Max(Value(obj, o[0]), MinWait);
                obj.WaitResume = ins.Next;
                return Yield;
            case 18: // if_timer: the counter of the current gosub level counts ticks
            {
                auto level = obj.GosubReturns.size();
                if (F(o[0]) * ScriptRuntime::TicksPerSecond > obj.LevelTimers[level])
                {
                    obj.LevelTimers[level] += Ticks;
                    return Branch(obj, ins, false);
                }

                obj.LevelTimers[level] = 0.0f;
                return Branch(obj, ins, true);
            }

            // Spawning.
            case 86: // spawn
                _runtime->Spawn(obj, Text(o[3]), V(o), 0.0f, -1, I(o[4]), ScriptRuntime::Spawning::Plain);
                break;
            case 161: // spawn_flagged
                _runtime->SpawnFlagged(obj, Text(o[3]), V(o), I(o[4]));
                break;
            case 206: // spawn_along_path: [path, step, type, script]: one object every step along the path
            {
                // (from time 0 to before its last key), flagged like spawn_flagged (0x2008a6)
                auto path = I(o[0]);
                auto end = _runtime->Motion->PathKeyFrame(path, _runtime->Motion->PathKeyCount(path) - 1);
                for (auto time = 0.0f; time < end; time += F(o[1]))
                {
                    _runtime->Spawn(obj, Text(o[2]), _runtime->Motion->PathPosition(path, time), 0.0f, -1, I(o[3]), ScriptRuntime::Spawning::Flagged);
                }

                break;
            }
            case 230: // spawn_ex
                _runtime->Spawn(obj, Text(o[5]), V(o), F(o[3]), I(o[4]), I(o[6]), ScriptRuntime::Spawning::Plain);
                break;
            case 113: // spawn_relative
            {
                auto offset = Rotated(Vector2(F(o[0]), F(o[1])), obj.Yaw);
                _runtime->Spawn(obj, Text(o[3]), obj.Position + Vector3(offset.X, offset.Y, F(o[2])), 0.0f, -1, I(o[4]), ScriptRuntime::Spawning::Plain);
                break;
            }
            case 150: // door_set_anims: opening and closing animations
                obj.DoorAnimations = {I(o[0]), I(o[1])};
                break;
            case 151: // door_set_names: sounds when opening, closing, open, closed
                obj.DoorSounds = {Text(o[0]), Text(o[1]), Text(o[2]), Text(o[3])};
                break;
            case 152: // door_set_flags (the low 4 bits are the engine's state)
                obj.DoorState = (obj.DoorState & ObjectBehaviors::DoorEngineBits) | (I(o[0]) & ~ObjectBehaviors::DoorEngineBits);
                break;
            case 153: // door_set_param: distance at which the door opens
                obj.DoorDistance = F(o[0]);
                break;
            case 100: // arena_show
                _runtime->ShowArena(Text(o[0]));
                break;
            case 149: // spawn_connector
                _runtime->SpawnConnector(obj, Text(o[5]), V(o), F(o[3]), I(o[4]), Text(o[6]), I(o[7]));
                break;
            case 111: // set_instance
                obj.InstanceId = I(o[0]) & 0xFFFF;
                break;
            case 110: // delete_self
                _runtime->Remove(obj);
                return Yield;
            case 76: // set_death_script
                obj.DeathScript = I(o[0]);
                break;
            case 16: // set_health
                obj.Health = I(o[0]);
                obj.MaxHealth = I(o[0]);
                obj.Indestructible = I(o[0]) >= ScriptRuntime::Indestructible;
                if (I(o[0]) == 0)
                {
                    _runtime->Kill(obj);
                    return Yield;
                }

                break;
            case 56: // add_health
                if (obj.Health < ScriptRuntime::Indestructible)
                {
                    obj.Health += I(o[0]);
                    if (obj.Health <= 0)
                    {
                        _runtime->Kill(obj);
                        return Yield;
                    }
                }

                break;

            // Animation.
            case 3: // anim_once
            case 59: // anim_loop
                obj.PlayAnimation(_runtime->GetAnimation(obj, I(o[0])), ins.Opcode == 59 ? MdkObject::Looping::Loop : MdkObject::Looping::Once);
                break;
            case 17: // if_anim_done
                return Branch(obj, ins, obj.IsAnimationDone());
            case 92: // if_anim_frame
                return Branch(obj, ins, obj.IsAnimationDone() || obj.AnimationFrame >= I(o[0]) - 1);
            case 118: // anim_end_frame (s16)
                obj.AnimationEndFrame = S16((I(o[0]) & 0xFFFF) - 1);
                break;
            case 24: // set_id_and_name: a sound played when the animation reaches frame n - 1
                obj.FrameSoundFrame = I(o[0]) - 1;
                obj.FrameSound = Text(o[1]);
                break;
            case 25: // set_string154
            case 26: // set_string150
                obj.Labels[ins.Opcode - 25] = Text(o[0]);
                break;
            case 19: // anim_mark_ended
                obj.AnimationEndFrame = MdkObject::AnimationEnded;
                break;
            case 31: // set_parts_mask
                obj.HiddenParts = PartsMask(obj, L(o[0]), obj.HiddenParts, Masking::Hide);
                break;
            case 83: // set_scale: a value, or [target, rate] to grow or shrink towards the target
            {
                auto scaling = AsList(o[0]);
                auto target = scaling != nullptr && scaling->size() == 2 ? std::get_if<float>(&(*scaling)[0]) : nullptr;
                if (target != nullptr)
                {
                    auto rate = F((*scaling)[1]);
                    if (obj.Scale < *target)
                    {
                        obj.Scale = MathF::Min(obj.Scale * (1.0f + rate * Dt), *target);
                    }
                    else if (obj.Scale > *target)
                    {
                        obj.Scale = MathF::Max(obj.Scale * (1.0f - rate * Dt * 0.5f), *target);
                    }
                }
                else
                {
                    obj.Scale = Value(obj, o[0]);
                }

                break;
            }

            // Hits.
            case 22: // if_hit: any hit but blasts; the event is cleared when true
            {
                auto hit = obj.HitEvent != 0 && obj.HitEvent != -3;
                if (hit)
                {
                    obj.HitEvent = 0;
                }

                return Branch(obj, ins, hit);
            }
            case 212: // if_hit_fd: a blast
            {
                auto hit = obj.HitEvent == -3;
                if (hit)
                {
                    obj.HitEvent = 0;
                }

                return Branch(obj, ins, hit);
            }
            case 42: // if_hit_part: name (cleared when true) or ["", name] (not cleared); "ANY" = any part
            {
                auto isName = std::holds_alternative<std::string>(o[0]);
                const auto& name = isName ? Text(o[0]) : Text(L(o[0]).at(1));
                auto hit = obj.Model != nullptr && obj.HitEvent > 0 && obj.HitEvent <= static_cast<int>(obj.Model->PartList.size())
                    && (Str::EqualsIgnoreCase("ANY", name) || Str::EqualsIgnoreCase(obj.Model->PartList[obj.HitEvent - 1]->Name, name));
                if (hit && isName)
                {
                    obj.HitEvent = 0;
                }

                return Branch(obj, ins, hit);
            }
            case 198: // set_weak_parts
            {
                constexpr int WeakPartCount = 8;
                obj.Flags |= MdkObject::FlagWeakParts;
                obj.WeakPrefix = Text(o[0]);
                obj.WeakPrefixLength = I(o[1]);
                obj.PartHealth = std::vector<int>(WeakPartCount, I(o[2]) & 0xFFFF);
                obj.PartMaxHealth = obj.PartHealth;
                break;
            }
            case 81: // push_hit_dir: velocity along the direction of the last hit
            {
                auto speed = Value(obj, o[1]) * (I(o[0]) == 1 ? Dt : 1.0f);
                auto direction = FromAngle(obj.HitDirection) * speed;
                obj.Velocity.X += direction.X;
                obj.Velocity.Y += direction.Y;
                break;
            }
            case 108: // if_touching_kurt
                if (obj.MoveCommand == 61)
                {
                    return Branch(obj, ins, (obj.ContactFlags & MdkObject::ContactTouchedKurt) != 0);
                }

                return Branch(obj, ins, Intersects(_runtime->GetWorldBounds(obj), _runtime->GetKurtBox()));
            case 109: // hurt_kurt
                _runtime->HurtKurt(I(o[0]));
                break;
            case 61: // fire
                _runtime->Fire(obj, L(o[0]), Text(o[1]), I(o[2]));
                break;

            // Orientation.
            case 8: // set_yaw
                obj.Yaw = Wrap360(F(o[0]));
                break;
            case 60: // face_target
                obj.Yaw = obj.YawTo(_runtime->TargetPosition);
                break;
            case 40: // turn_yaw
                obj.Yaw = Wrap360(obj.Yaw + Value(obj, o[0]) * Dt);
                break;
            case 134: // turn_roll
                obj.Roll = Wrap360(obj.Roll + Value(obj, o[0]) * Dt);
                break;
            case 120: // set_pitch
                obj.Pitch = F(o[0]);
                break;
            case 122: // turn_pitch
                obj.Pitch += Math::Clamp(WrapAngle(F(o[1]) - obj.Pitch), -F(o[0]) * Dt, F(o[0]) * Dt);
                break;
            case 97: // set_banking (automatic banking, ObjectMotion.Bank)
                if (I(o[0]) != 0)
                {
                    obj.Flags &= ~MdkObject::FlagNoBanking;
                }
                else
                {
                    obj.Flags |= MdkObject::FlagNoBanking;
                    obj.Roll = 0.0f;
                }

                break;
            case 23: // set_flag148_1
                if (I(o[0]) != 0)
                {
                    obj.Flags |= 1;
                }
                else
                {
                    obj.Flags &= ~1;
                    obj.Roll = 0.0f;
                }

                break;
            case 207: // turn_to_yaw
            {
                auto goal = F(o[1]);
                auto diff = WrapAngle(goal - obj.Yaw);
                auto step = F(o[0]) * Dt;
                obj.Yaw = Wrap360(MathF::Abs(diff) <= step ? goal : obj.Yaw + MathF::Sign(diff) * step);
                if (ins.Action && ins.Action->Type == BranchAction::Kind::None)
                {
                    return ins.Next;
                }

                return Branch(obj, ins, MathF::Abs(obj.Yaw - Wrap360(goal)) < 1e-4f);
            }
            case 101: // aim_target
                Aim(obj, 100.0f, Aiming::CheckDistance);
                break;
            case 104: // aim_target_inaccurate
                Aim(obj, F(o[0]), Aiming::Always);
                break;
            case 235: // turn_to_target_offset
                TurnToTarget(obj, F(o[0]), F(o[1]), F(o[2]), F(o[3]), std::nullopt);
                break;
            case 105: // turn_to_target_offset3d
                TurnToTarget(obj, F(o[0]), F(o[1]), F(o[2]), F(o[3]), F(o[4]));
                break;
            case 62: // if_target_angle
            {
                auto angle = MathF::Abs(WrapAngle(obj.YawTo(_runtime->TargetPosition) - obj.Yaw));
                return Branch(obj, ins, Compare(angle, L(o[0])));
            }
            case 193: // face_mode
            {
                const auto& face = L(o[0]);
                auto mode = I(face.at(0));
                if ((mode == 1 || mode == 2) && obj.Velocity.X != 0.0f && obj.Velocity.Y != 0.0f)
                {
                    obj.Yaw = Wrap360(ToDegrees(MathF::Atan2(obj.Velocity.Y, obj.Velocity.X)));
                    if (mode == 2 && obj.Velocity.Z != 0.0f)
                    {
                        obj.Pitch = ToDegrees(MathF::Atan2(obj.Velocity.Z, Vector2(obj.Velocity.X, obj.Velocity.Y).Length()));
                    }
                }
                else if (mode == 3)
                {
                    auto other = FirstOther(obj, face.at(1), -1);
                    if (other != nullptr)
                    {
                        obj.Yaw = other->Yaw;
                    }
                }

                break;
            }

            // Movement.
            case 78: // move_to
                StartMove(obj, 78, V(o));
                break;
            case 6: // move_to_target
                obj.MoveCommand = 6;
                break;
            case 43: // move_near_target (only in Kurt's arena)
                if (obj.Arena == _runtime->CurrentArena)
                {
                    StartMove(obj, 43, _runtime->NearTargetDestination(obj, F(o[0]), F(o[1])));
                }

                break;
            case 75: // stop_motion
                obj.MoveCommand = 0;
                break;
            case 15: // alarm
                obj.MoveCommand = 15;
                break;
            case 88: // set_move_x
            {
                constexpr int Stop = 2;
                obj.Path = 0;
                if (I(o[0]) != Stop)
                {
                    obj.MoveCommand = 88;
                    obj.MoveParameter = I(o[0]);
                }
                else
                {
                    obj.MoveCommand = 0;
                    obj.Speed = 0.0f;
                }

                break;
            }
            case 44: // if_move_done
                return Branch(obj, ins, obj.MoveCommand == 0);
            case 54: // if_dest_dist (2D when on the floor)
            {
                auto toDestination = obj.MoveDestination - obj.Position;
                if ((obj.ContactFlags & MdkObject::ContactFloor) != 0)
                {
                    toDestination.Z = 0.0f;
                }

                return Branch(obj, ins, Compare(toDestination.Length(), L(o[0])));
            }
            case 50: // set_max_speed
                obj.MaxSpeed = Value(obj, o[0]);
                break;
            case 51: // set_accel
                obj.Acceleration = Value(obj, o[0]);
                break;
            case 52: // set_decel
                obj.Deceleration = Value(obj, o[0]);
                break;
            case 53: // set_speed
                obj.Speed = Value(obj, o[0]);
                break;
            case 82: // set_friction (set_param44)
                obj.Friction = Value(obj, o[0]);
                break;
            case 55: // set_gravity (set_turn_rate)
                obj.Gravity = Value(obj, o[0]);
                break;
            case 84: // set_height_offset
                obj.HeightOffset = Value(obj, o[0]);
                break;
            case 106: // set_302: lifetime of projectiles, jump time...
                obj.Parameter = F(o[0]);
                break;
            case 39: // push_forward
            {
                auto direction = FromAngle(obj.Yaw) * Value(obj, o[0]);
                obj.Push += Vector3(direction.X, direction.Y, 0.0f);
                break;
            }
            case 201: // push_dir
            {
                auto direction = FromAngle(obj.Yaw + F(o[1])) * F(o[0]);
                obj.Push += Vector3(direction.X, direction.Y, 0.0f);
                break;
            }
            case 80: // add_vel_local
            {
                auto [s, c] = MathF::SinCos(ToRadians(obj.Yaw));
                obj.Velocity += Vector3(-F(o[0]) * c - F(o[1]) * s, -F(o[1]) * c - F(o[0]) * s, F(o[2]));
                break;
            }
            case 93: // accel_toward_point: velocity towards a point (Manhattan-normalized)
            {
                auto toPoint = V(o, 1) - obj.Position;
                auto length = MathF::Abs(toPoint.X) + MathF::Abs(toPoint.Y) + MathF::Abs(toPoint.Z);
                if (length > 0.0f)
                {
                    obj.Velocity += toPoint / length * F(o[0]);
                }

                break;
            }
            case 211: // stop_velocity
                obj.Velocity = Vector3::Zero();
                break;
            case 38: // if_vel_z
                return Branch(obj, ins, Compare(obj.Velocity.Z, L(o[0])));
            case 37: // if_flag14c_2 (on the floor)
                return Branch(obj, ins, (obj.ContactFlags & MdkObject::ContactFloor) != 0);
            case 200: // move_to_point
            {
                // 0x459555: a Manhattan step, clamped per axis; gravity leaves z alone. (Euclid made
                // diagonals faster: XGEN's targets hit their pedestals' glass sides too low.)
                constexpr float Arrived = 0.5f;
                constexpr float MinDistance = 0.1f;
                auto toPoint = V(o, 1) - obj.Position;
                if ((obj.Flags & MdkObject::FlagGravity) != 0)
                {
                    toPoint.Z = 0.0f;
                }

                auto distance = MathF::Abs(toPoint.X) + MathF::Abs(toPoint.Y) + MathF::Abs(toPoint.Z);
                if (distance < Arrived)
                {
                    return Branch(obj, ins, true);
                }

                auto step = F(o[0]) * Dt / MathF::Max(distance, MinDistance) * toPoint;
                obj.Push += Vector3::Clamp(step, -Vector3::Abs(toPoint), Vector3::Abs(toPoint)) / Dt;
                break;
            }

            // Paths.
            case 2: // follow_path
                FollowPath(obj, o);
                break;
            case 20: // stop_path
                obj.Path = 0;
                break;
            case 21: // path_stop_at (-2 = the current frame)
            {
                constexpr int CurrentFrame = -2;
                obj.PathStop = S16(I(o[0]) & 0xFFFF);
                if (obj.PathStop == CurrentFrame)
                {
                    obj.PathStop = Round(obj.PathTime);
                }

                break;
            }
            case 124: // path_yaw_offset
                obj.PathYawOffset = F(o[0]);
                break;
            case 102: // if_path_done
                return Branch(obj, ins, obj.Path == 0);

            // The World's Most Interesting Bomb: aliens gather around it.
            case 165: // if_no_bomb
                return Branch(obj, ins, Bomb() == nullptr);
            case 166: // if_bomb_visible
            {
                auto bomb = Bomb();
                auto up = Vector3(0.0f, 0.0f, 5.0f);
                return Branch(obj, ins, bomb != nullptr && !_runtime->Raycast(obj.Position + up, bomb->Position + up).has_value());
            }
            case 167: // move_to_bomb: next to the bomb, on this side, a little aside
            {
                constexpr float MaxAside = 6.0f;
                constexpr float AboveBomb = 2.5f;
                auto bomb = Bomb();
                if (bomb == nullptr)
                {
                    break;
                }

                auto away = Vector2(obj.Position.X - bomb->Position.X, obj.Position.Y - bomb->Position.Y);
                away = away == Vector2::Zero() ? away : Vector2::Normalize(away);
                auto aside = Vector2(away.Y, -away.X) * (rng.NextSingle() * 2.0f - 1.0f) * MathF::Min(F(o[0]), MaxAside);
                auto spot = Vector2(bomb->Position.X, bomb->Position.Y) + away * F(o[0]) + aside;
                StartMove(obj, 78, Vector3(spot.X, spot.Y, bomb->Position.Z + AboveBomb + obj.HeightOffset));
                break;
            }
            case 177: // set_blast_range: blasts farther than this don't hurt the object
                obj.BlastRange = F(o[0]);
                break;

            // Variables and flags.
            case 65: // set_var
                Variables(obj, I(o[0]))[Math::Clamp(I(o[1]), 0, VariableCount - 1)] = F(o[2]);
                break;
            case 66: // add_var
                Variables(obj, I(o[0]))[Math::Clamp(I(o[1]), 0, VariableCount - 1)] += F(o[2]);
                break;
            case 216: // add_var_dt
                Variables(obj, I(o[0]))[Math::Clamp(I(o[1]), 0, VariableCount - 1)] += F(o[2]) * Dt;
                break;
            case 67: // if_var
                return Branch(obj, ins, Compare(Variables(obj, I(o[0]))[Math::Clamp(I(o[1]), 0, VariableCount - 1)], L(o[2])));
            case 68: // set_flag
                SetFlags(obj, I(o[0]), GetFlags(obj, I(o[0])) | (1 << (I(o[1]) & FlagBits)));
                break;
            case 69: // clear_flag
                SetFlags(obj, I(o[0]), GetFlags(obj, I(o[0])) & ~(1 << (I(o[1]) & FlagBits)));
                break;
            case 71: // if_flag_set
                return Branch(obj, ins, (GetFlags(obj, I(o[0])) & (1 << (I(o[1]) & FlagBits))) != 0);
            case 70: // toggle_flag
                SetFlags(obj, I(o[0]), GetFlags(obj, I(o[0])) ^ (1 << (I(o[1]) & FlagBits)));
                break;
            case 72: // if_flag_clear
                return Branch(obj, ins, (GetFlags(obj, I(o[0])) & (1 << (I(o[1]) & FlagBits))) == 0);
            case 176: // if_flag_40000
                return Branch(obj, ins, (obj.Flags & MdkObject::FlagCollected) != 0);
            case 116: // flags_set
                obj.Flags |= I(o[0]);
                break;
            case 117: // flags_clear
                obj.Flags &= ~I(o[0]);
                break;
            case 232: // if_option (a cheat toggled option, 1 by default)
                return Branch(obj, ins, I(o[0]) == _runtime->Option);
            case 35: // set_flag148_4
                obj.Flags = I(o[0]) != 0 ? obj.Flags | MdkObject::FlagCollides : obj.Flags & ~MdkObject::FlagCollides;
                break;
            case 36: // set_flag148_2
                obj.Flags = I(o[0]) != 0 ? obj.Flags | MdkObject::FlagGravity : obj.Flags & ~MdkObject::FlagGravity;
                break;

            // Other objects.
            case 4: // command_objects
                CommandObjects(obj, o);
                break;
            case 11: // set_priority
                obj.Priority = I(o[0]);
                break;
            case 73: // set_obey_level
                obj.ObeyLevel = I(o[0]);
                break;
            case 10: // if_count_objects: objects of a type that this object may command
            {
                auto count = CountOthers(obj, o[0], Counting::Commandable);
                return Branch(obj, ins, count == I(o[1]));
            }
            case 119: // if_count_alive
            {
                auto count = CountOthers(obj, o[0], Counting::Alive);
                return Branch(obj, ins, Compare(static_cast<float>(count), L(o[1])));
            }
            case 74: // attach_to: the object named "<type>_<id>"
            {
                std::shared_ptr<MdkObject> found;
                for (const auto& other : _runtime->Objects)
                {
                    if (IsArenaOther(obj, *other) && IsNamed(*other, o[2]))
                    {
                        found = other;
                    }
                }

                if (found != nullptr)
                {
                    obj.Leader = found;
                    obj.AttachPoints = {I(o[0]), I(o[1])};
                    obj.MoveCommand = 74;
                }

                break;
            }
            case 175: // if_inventory: how many items of a type Kurt has
            {
                auto count = 0;
                const auto& slots = _runtime->Kurt->Inventory->Slots();
                for (size_t i = 0; i < slots.size(); i++)
                {
                    count += static_cast<int>(slots[i].Item) == I(o[0]) ? slots[i].Count : 0;
                }

                return Branch(obj, ins, Compare(static_cast<float>(count), o[1], o[2], std::holds_alternative<std::monostate>(o[3]) ? BoxedZero : o[3]));
            }
            case 174: // if_ammo: 0 the super chain gun's ticks, 1-5 sniper ammo
            {
                const auto& inventory = *_runtime->Kurt->Inventory;
                auto kind = I(o[0]);
                auto amount = kind == 0 ? inventory.SuperChainGun : kind <= static_cast<int>(inventory.Ammo().size()) ? inventory.Ammo()[kind - 1] : 0;
                return Branch(obj, ins, Compare(static_cast<float>(amount), o[1], o[2], std::holds_alternative<std::monostate>(o[3]) ? BoxedZero : o[3]));
            }
            case 29: // spawn_chain: links that hang off this object, nearest one first
            {
                constexpr int ChainCommand = 30;
                auto maxId = LastFollowerId(obj);
                for (auto n = I(o[0]) - 1; n >= 0; n--)
                {
                    auto link = _runtime->Spawn(obj, Text(o[1]), obj.Position, 0.0f, n + maxId + 1, I(o[2]), ScriptRuntime::Spawning::Plain);
                    if (link == nullptr)
                    {
                        break;
                    }

                    link->MoveCommand = ChainCommand;
                    link->Leader = obj.shared_from_this();
                }

                break;
            }
            case 159: // spawn_box: [[position mode, x, y, z], size x, y, z, texture, script]
            {
                const auto& where = L(o[0]);
                auto point = obj.Position;
                if (I(where.at(0)) == 1)
                {
                    auto offset = Rotated(Vector2(F(where.at(1)), F(where.at(2))), obj.Yaw);
                    point += Vector3(offset.X, offset.Y, F(where.at(3)));
                }
                else if (I(where.at(0)) == 2)
                {
                    point = V(where, 1);
                }

                _runtime->SpawnBox(obj, point, V(o, 1), Text(o[4]), I(o[5]));
                break;
            }
            case 133: // arena_texture_frame: [texture, mode, value]; the levels only use mode 1 (frames per second)
                _runtime->AnimatedTextures->Set(obj.Arena, Text(o[0]), AnimatedTextures::ModeOf(I(o[1])), F(o[2]), Dt);
                break;
            case 131: // special_event: cutscenes and the end of the level
                _runtime->SpecialEvent(obj, I(o[0]));
                break;
            case 85: // set_rolling
                obj.SetRolling(I(o[0]) != 0 ? MdkObject::Rolling::On : MdkObject::Rolling::Off);
                break;
            case 169: // set_roll_radius
                obj.RollRadius = Value(obj, o[0]);
                break;
            case 137: // shatter_group: [group, life, size, speed(, point(, direction))]
            case 138:
            case 139:
                // 138 sets the point and direction, 139 the point (pieces fly away from it), 137 reuses them.
                if (ins.Opcode == 138)
                {
                    _runtime->ShatterPoint = V(o, 4);
                    _runtime->ShatterDirection = V(o, 7);
                }
                else if (ins.Opcode == 139)
                {
                    _runtime->ShatterPoint = V(o, 4);
                    _runtime->ShatterDirection = Vector3::Zero();
                }

                if (I(o[0]) >= 1 && I(o[0]) <= 255)
                {
                    _runtime->Debris->Shatter(obj.Arena, I(o[0]), F(o[1]), F(o[2]), F(o[3]), _runtime->ShatterPoint, _runtime->ShatterDirection);
                }

                if (_runtime->ShatterDirection == Vector3::Zero())
                {
                    // A radial burst leaves the direction straight up for the next 137 (0x40c828).
                    _runtime->ShatterDirection = Vector3::UnitZ();
                }

                break;
            case 128: // attach_effect: [name, point, second point]: a wound bleeding slime at a reference point
                // (only with the effects detail on); "OFF" with the same point twice stops it
                if (Text(o[0]) == "OFF" && I(o[1]) == I(o[2]))
                {
                    _runtime->Effects->Detach(obj, I(o[1]));
                }
                else if (_runtime->Option != 0)
                {
                    _runtime->Effects->Attach(obj, I(o[1]), I(o[2]));
                }

                break;
            case 132: // spawn_effect: [chance, reference point or 255, point]: a bubble now and then
            {
                // (chance >= 150 would make sparks, which no level uses)
                constexpr int SparkChance = 150;
                constexpr int NoPoint = 255;
                if (I(o[0]) < SparkChance && rng.Next(100) < I(o[0]))
                {
                    auto point = I(o[1]) != NoPoint ? obj.ReferencePoint(I(o[1])) : V(L(o[2]));
                    _runtime->Effects->SpawnBubble(obj.Arena, point);
                }

                break;
            }
            case 136: // spawn_debris: [count, velocity, spread, absolute, point, size]: slime drops
            {
                constexpr float Spread = 1.0f / 32768.0f;
                auto count = _runtime->Option != 0 ? I(o[0]) : I(o[0]) > 1 ? Math::Min(I(o[0]), 1) : 0;
                auto origin = V(o, 6) + (I(o[5]) == 0 ? obj.Position : Vector3::Zero());
                for (auto i = 0; i < count; i++)
                {
                    // Separate statements: C# evaluates the arguments left to right, C++ doesn't.
                    auto x = F(o[1]) + (rng.Next(32768) - 0x4000) * F(o[4]) * Spread;
                    auto y = F(o[2]) + (rng.Next(32768) - 0x4000) * F(o[4]) * Spread;
                    auto z = F(o[3]) + (rng.Next(32768) - 0x800) * F(o[4]) * Spread;
                    auto velocity = Vector3(x, y, z);
                    _runtime->Effects->SpawnDrop(obj.Arena, origin, velocity, F(o[9]) + rng.Next(32768) * 0.0005f);
                }

                break;
            }
            case 219: // if_gun_aim: [target row y, z, action]
                return Branch(obj, ins, _runtime->GunAim(obj, F(o[0]), F(o[1])));
            case 220: // place_x_near_player: [x min, x max, y limit]
                _runtime->PlaceNearKurt(obj, F(o[0]), F(o[1]), F(o[2]));
                break;
            case 203: // camera_track: [[mode(, height)]] (nothing in sniper mode)
            {
                const auto& track = L(o[0]);
                _runtime->CameraTrack(obj, I(track.at(0)), track.size() > 1 ? F(track[1]) : 0.0f);
                break;
            }
            case 28: // bomb_follow_path: the sniper mortar round that just hit a triangle group follows a path
                _runtime->SniperRounds->GuideLastMortar(I(o[0]));
                break;
            case 130: // special_130: a bullet hole on the texture where a sniper round hit
                _runtime->StampBulletHole(obj);
                break;
            case 229: // turn_and_jump_to_dest: [turn rate, action while jumping]
                _runtime->Motion->TurnAndJump(obj, F(o[0]));
                return Branch(obj, ins, obj.MoveCommand == 229);
            case 226: // jump_to: [pivot x, y, z, angular speed, gain], starts swinging below the pivot
                obj.SwingPivot = V(o);
                obj.SwingSpeed = F(o[3]);
                obj.SwingGain = F(o[4]);
                obj.SwingYaw = obj.Yaw;
                obj.SwingLength = obj.SwingPivot.Z - obj.Position.Z;
                obj.Flags |= MdkObject::FlagSwinging;
                break;
            case 242: // set_2d0_block: [[count, 4 points]]: lines from reference points 1-4 to the points
            {
                constexpr int AllLines = 0xFF;
                const auto& block = L(o[0]);
                if (I(block.at(0)) == 0)
                {
                    obj.RopeMask = 0;
                    break;
                }

                obj.RopeColor = 1;
                obj.RopeMask = AllLines;
                for (size_t i = 0; i < obj.RopePoints.size(); i++)
                {
                    obj.RopePoints[i] = V(block, 1 + static_cast<int>(i) * 3);
                }

                break;
            }
            case 224: // wind_zone: [enable] or [enable, yaw, speed]
            {
                const auto& wind = L(o[0]);
                _runtime->WindZone(obj.Arena, I(wind.at(0)) != 0 ? ScriptRuntime::Wind::On : ScriptRuntime::Wind::Off, wind.size() > 2 ? F(wind[1]) : 0.0f, wind.size() > 2 ? F(wind[2]) : 0.0f);
                break;
            }

            // Fans (updrafts).
            case 142: // fan_create: [hotspot, name, param, type, strength]
                _runtime->Fans->Create(obj.Arena, I(o[0]), Text(o[1]), I(o[2]), I(o[3]), F(o[4]));
                break;
            case 143: // fan_remove
                _runtime->Fans->Remove(obj.Arena, Text(o[0]));
                break;
            case 190: // fan_enable: [enable, name]
                _runtime->Fans->Enable(obj.Arena, Text(o[1]), I(o[0]) != 0 ? Fans::Power::On : Fans::Power::Off);
                break;

            // The HUD.
            case 247: // hud_message: [flags, text name, seconds]
                if (_runtime->Messages)
                {
                    _runtime->Messages->Push(Text(o[1]), I(o[0]), F(o[2]));
                }

                break;
            case 181: // boss_bar: the health bar shows a counter (mode 0) or an arena variable (mode 1)
                BossBar(obj, L(o[0]));
                break;

            // Triangle groups of the arena.
            case 98: // group_set_state
                _runtime->SetGroupState(obj.Arena, I(o[0]), I(o[1]));
                break;
            case 140: // group_set_texture
                _runtime->SetGroupTexture(obj.Arena, I(o[0]), I(o[1]));
                break;
            case 99: // group_on_hit
            {
                auto state = _runtime->GetArenaState(obj.Arena);
                state->GroupHitMasks[GroupIndex(o[1])] = I(o[0]);
                state->GroupHitScripts[GroupIndex(o[1])] = I(o[2]);
                break;
            }
            case 168: // group_set_hit_flags: 0x80 = destructible (hidden until hit)
                _runtime->GetArenaState(obj.Arena)->GroupHitFlags[GroupIndex(o[0])] = I(o[1]);
                if ((I(o[1]) & ScriptRuntime::GroupDestructible) != 0)
                {
                    _runtime->SetGroupState(obj.Arena, I(o[0]), ScriptRuntime::GroupHide);
                }

                break;
            case 162: // arena_counter_set
                _runtime->GetArenaState(obj.Arena)->GroupCounters[GroupIndex(o[0])] = I(o[1]);
                break;
            case 163: // if_arena_counter
                return Branch(obj, ins, Compare(static_cast<float>(_runtime->GetArenaState(obj.Arena)->GroupCounters[GroupIndex(o[0])]), L(o[1])));
            case 194: // group_state_near_player: groups around the one under Kurt get the opposite op, the others the op (0x453a1e)
            {
                auto floorGroup = _runtime->GetKurtFloorGroup();
                auto first = I(o[1]);
                auto last = I(o[2]);
                if (first <= 0 || floorGroup < first || floorGroup > last)
                {
                    break;
                }

                for (auto group = first; group <= last; group++)
                {
                    auto near = group >= floorGroup - I(o[3]) && group <= floorGroup + I(o[4]);
                    _runtime->SetGroupState(_runtime->CurrentArena, group, near ? I(o[0]) ^ 1 : I(o[0]));
                }

                break;
            }

            // Conditions about Kurt, the arena and chance.
            case 123: // if_not_in_player_arena
                return Branch(obj, ins, obj.Arena != _runtime->CurrentArena);
            case 222: // if_opcode_count
                return Branch(obj, ins, Compare(static_cast<float>(_opcodeCount), L(o[0])));
            case 231: // if_move_idle_flag: the movement gave up (stuck, ObjectMotion.HandleStuck)
                return Branch(obj, ins, obj.MoveCommand == 0 && obj.StuckCount != 0);
            case 236: // if_no_floor_at: no up-facing face of its arena below a point (0x46046c)
            {
                // From 3 above the point down to 3 below it (or to z + depth). The offset turns like
                // add_vel_local's: −dx is ahead (the XG's −20 checks 20 units in front).
                constexpr float ProbeTop = 3.0f;
                constexpr float DefaultDrop = 6.0f;
                auto dx = F(o[0]);
                auto dy = F(o[1]);
                auto depth = F(o[2]);
                auto [s, c] = MathF::SinCos(ToRadians(obj.Yaw));
                auto top = obj.Position + Vector3(-dx * c - dy * s, -dy * c - dx * s, ProbeTop);
                auto bottom = depth != 0.0f ? obj.Position.Z + depth : top.Z - DefaultDrop;
                return Branch(obj, ins, !_runtime->CrossesFloor(obj.Arena, top, Vector3(top.X, top.Y, bottom)));
            }
            case 189: // lob_to_kurt: throw itself so that it lands on Kurt (or at a height)
                LobToKurt(obj, L(o[0]));
                break;
            case 46: // find_cover_spot
                _runtime->FindCoverSpot(obj);
                return Branch(obj, ins, obj.MoveCommand == 43);
            case 197: // find_advance_spot
                _runtime->FindAdvanceSpot(obj);
                return Branch(obj, ins, obj.MoveCommand == 197);
            case 221: // pick_waypoint8
                _runtime->PickWaypoint8(obj, I(o[0]));
                return Branch(obj, ins, obj.MoveCommand == 221);
            case 209: // if_player_on_me_low: Kurt stands on it and fires
                // 0x458a59: his feet not above his floor on it (0x573a1c), which standing on it
                // already means; its model's top can be below them (LEVEL6's XBGUN: by 0.02).
                return Branch(obj, ins, _runtime->GetKurtPlatform().get() == &obj && _runtime->Kurt->Firing);
            case 164: // path_speed_by_kurt: [enable, [distance, far, middle, near speeds]]
                if (I(o[0]) != 0)
                {
                    obj.PathSpeeds.clear();
                    for (const auto& speed : L(o[1]))
                    {
                        obj.PathSpeeds.push_back(F(speed));
                    }

                    obj.PathSpeed = obj.PathSpeeds.at(2);
                    obj.Flags |= MdkObject::FlagPathSpeedByKurt;
                }
                else
                {
                    obj.Flags &= ~MdkObject::FlagPathSpeedByKurt;
                }

                break;
            case 173: // teleport_player_keep: as teleport_player, from Kurt's place and yaw (0x454556)
            {
                // [arena] keeps them; ["", arena, dx, dy, dz, dyaw] adds an offset (LEVEL4's
                // CMEAT_6 lifts Kurt 1971 up into MEAT_7).
                const auto& keep = L(o[0]);
                auto name = Text(keep.at(0));
                auto shifted = name.empty();
                auto offset = shifted ? V(keep, 2) : Vector3::Zero();
                auto turn = shifted ? F(keep.at(5)) : 0.0f;
                _runtime->TeleportKurt(shifted ? Text(keep.at(1)) : name, _runtime->KurtPosition + offset, _runtime->KurtYaw + turn);
                obj.Restart = 0;
                ClearStack(obj);
                return Yield;
            }
            case 103: // if_kurt_in_box: each axis between its min and max (corners given backwards never hold)
            {
                auto kurt = _runtime->KurtPosition;
                auto inside = true;
                for (auto axis = 0; axis < 3; axis++)
                {
                    inside = inside && Axis(kurt, axis) >= F(o[axis]) && Axis(kurt, axis) <= F(o[axis + 3]);
                }

                return Branch(obj, ins, inside);
            }
            case 115: // if_wall
            {
                constexpr float WalkerHeight = 2.0f;
                auto from = obj.Position + Vector3(0.0f, 0.0f, (obj.Flags & MdkObject::FlagCollides) != 0 ? WalkerHeight : 0.0f);
                auto direction = FromAngle(obj.Yaw + F(o[0])) * F(o[1]);
                return Branch(obj, ins, _runtime->Raycast(from, from + Vector3(direction.X, direction.Y, 0.0f)).has_value());
            }
            case 121: // if_floor_below
                return Branch(obj, ins, _runtime->Raycast(obj.Position, obj.Position - Vector3(0.0f, 0.0f, F(o[0]))).has_value());
            case 96: // if_kurt_in_rect
            {
                auto kurt = _runtime->KurtPosition;
                return Branch(obj, ins, kurt.X >= F(o[0]) && kurt.X <= F(o[2]) && kurt.Y >= F(o[1]) && kurt.Y <= F(o[3]));
            }
            case 14: // if_sees_kurt
            case 57: // if_not_sees_kurt
            {
                auto sees = _runtime->CanSeeKurt(obj, F(o[0]), F(o[1]));
                return Branch(obj, ins, ins.Opcode == 14 ? sees : !sees);
            }
            case 45: // if_target_dist
                return Branch(obj, ins, Compare(obj.DistanceTo(_runtime->TargetPosition), L(o[0])));
            case 47: // if_chance
                return Branch(obj, ins, rng.Next(10000) < F(o[0]) * 100.0f);
            case 48: // if_chance_per_second
            {
                constexpr float MinPeriod = 0.001f;
                return Branch(obj, ins, rng.Next(10000) < Dt / MathF::Max(F(o[0]), MinPeriod) * 10000.0f);
            }

            // Sounds.
            case 89: // play_sound
                _runtime->PlaySound(obj, Text(o[2]), I(o[0]), o[1]);
                break;
            case 107: // set_loop_sound
                _runtime->SetLoopSound(obj, Text(o[0]));
                break;
            case 249: // if_own_sound: mode 0 playing, 1 playing this sound, other: not playing
            {
                auto playing = _runtime->Mixer->IsVoicePlaying(obj.TrackedVoice);
                auto mode = I(o[0]);
                auto condition = mode == 0 ? playing : mode == 1 ? playing && Is(obj.TrackedSound, o[2]) : !playing;
                return Branch(obj, ins, condition);
            }

            // Animation speed.
            case 58: // anim_fps
                obj.AnimationRate = Value(obj, o[0]);
                break;
            case 186: // anim_fps_from_speed: the animation keeps up with the walking speed
                obj.AnimationRate = F(o[1]) != 0.0f ? obj.Speed * ScriptRuntime::TicksPerSecond / F(o[1]) : MdkObject::AnimationFps;
                break;
            case 154: // wait_anim_frame: runs again every frame until the animation reaches the frame
            {
                constexpr int HoldFrame = -1;
                obj.Restart = ins.Pc;
                auto frame = I(o[0]);
                auto reached = obj.IsAnimationDone() || (frame >= 0 && obj.AnimationFrame >= frame)
                    || (frame == HoldFrame && (obj.AnimationEndFrame < 0 || obj.AnimationFrame == obj.AnimationEndFrame));
                if (!reached)
                {
                    return Yield;
                }

                break;
            }

            // More conditions.
            case 127: // if_health
                return Branch(obj, ins, Compare(static_cast<float>(obj.Health), L(o[0])));
            case 185: // if_speed
                return Branch(obj, ins, Compare(obj.Speed, L(o[0])));
            case 160: // if_yaw
                return Branch(obj, ins, Compare(Wrap360(obj.Yaw), L(o[0])));
            case 241: // if_player_health
                return Branch(obj, ins, Compare(static_cast<float>(_runtime->Kurt->Health), L(o[0])));
            case 238: // if_self_pos: 0 x, 1 y, other z
                return Branch(obj, ins, Compare(Axis(obj.Position, Math::Min(I(o[0]), 2)), L(o[1])));
            case 237: // if_self_in_box
            {
                auto p = obj.Position;
                return Branch(obj, ins, p.X >= F(o[0]) && p.Y >= F(o[1]) && p.Z >= F(o[2]) && p.X <= F(o[3]) && p.Y <= F(o[4]) && p.Z <= F(o[5]));
            }
            case 191: // if_kurt_delta: Kurt's offset along an axis, absolute unless +0x80
            {
                constexpr int Signed = 0x80;
                auto delta = Axis(_runtime->KurtPosition - obj.Position, Math::Min(I(o[0]) & 0x7F, 2));
                if ((I(o[0]) & Signed) == 0)
                {
                    delta = MathF::Abs(delta);
                }

                return Branch(obj, ins, Compare(delta, L(o[1])));
            }
            case 188: // if_target_dist2d
                return Branch(obj, ins, Compare(Distance2D(_runtime->TargetPosition, obj.Position), L(o[0])));
            case 234: // if_target_angle_signed
                return Branch(obj, ins, Compare(WrapAngle(obj.Yaw - obj.YawTo(_runtime->TargetPosition)), L(o[0])));
            case 87: // if_kurt_looks_at_me
                return Branch(obj, ins, _runtime->KurtLooksAt(obj, F(o[0]), F(o[1]), F(o[2])));
            case 114: // if_player_on_me
                return Branch(obj, ins, _runtime->GetKurtPlatform().get() == &obj);
            case 250: // if_in_box: an object of a type inside a box (Kurt's projectiles aren't done yet)
                return Branch(obj, ins, InBox(obj, o));
            case 33: // if_path_frame
                return Branch(obj, ins, obj.Path == 0 || Round(obj.PathTime) >= I(o[0]) - 1);
            case 34: // if_no_leader (the movement command is cleared when true)
            {
                auto alone = obj.Leader == nullptr || obj.Leader->Dead;
                auto next = Branch(obj, ins, alone);
                if (alone)
                {
                    obj.MoveCommand = 0;
                }

                return next;
            }

            // Partners (obj+0x2b8).
            case 245: // find_object
            {
                const auto& args = L(o[0]);
                obj.Linked = FindObject(obj, Text(args.at(args.size() - 1)), args.size() == 2 ? I(args[0]) : 0);
                break;
            }
            case 213: // if_partner_distance (0 without a partner, at least 1)
            {
                auto partner = obj.Linked;
                auto distance = partner != nullptr && !partner->Dead ? MathF::Max(obj.DistanceTo(partner->Position), 1.0f) : 0.0f;
                return Branch(obj, ins, Compare(distance, L(o[0])));
            }
            case 214: // if_partner_angle
            {
                auto partner = obj.Linked;
                auto angle = partner != nullptr && !partner->Dead ? WrapAngle(obj.Yaw - obj.YawTo(partner->Position)) : 0.0f;
                return Branch(obj, ins, Compare(angle, L(o[0])));
            }
            case 179: // spawn_partner
            {
                auto partner = _runtime->Spawn(obj, Text(o[0]), obj.Position, 0.0f, -1, I(o[1]), ScriptRuntime::Spawning::Plain);
                if (partner != nullptr)
                {
                    partner->Linked = obj.shared_from_this();
                    obj.Linked = partner;
                }

                break;
            }
            case 180: // snap_to_partner
            {
                auto partner = obj.Linked;
                if (partner == nullptr || partner->Dead)
                {
                    break;
                }

                obj.Position = partner->Position;
                const auto& offset = L(o[0]);
                if (I(offset.at(0)) != 0)
                {
                    obj.Position += V(offset, 1);
                }

                break;
            }
            case 228: // link_partners: pairs up unlinked objects of a type closer than 100
                if (auto type = std::get_if<std::string>(&o[0]))
                {
                    LinkPartners(obj, *type);
                }
                else if (I(L(o[0]).at(1)) == 0)
                {
                    obj.Linked = nullptr;
                }

                break;
            case 246: // turn_to_linked (bit 7: the pitch too)
            {
                constexpr int Turns = 1;
                constexpr int AlsoPitch = 0x80;
                auto partner = obj.Linked;
                if ((I(o[0]) & Turns) == 0 || partner == nullptr || partner->Dead)
                {
                    break;
                }

                auto step = F(o[1]) * Dt;
                auto diff = WrapAngle(obj.YawTo(partner->Position) - obj.Yaw);
                obj.Yaw = Wrap360(obj.Yaw + Math::Clamp(diff, -step, step));
                if ((I(o[0]) & AlsoPitch) != 0)
                {
                    auto d = partner->Position - obj.Position;
                    auto targetPitch = ToDegrees(MathF::Atan2(d.Z + obj.HeightOffset, Vector2(d.X, d.Y).Length()));
                    obj.Pitch += Math::Clamp(WrapAngle(targetPitch - obj.Pitch), -step, step);
                }

                break;
            }

            // Parts, hits and damage.
            case 208: // if_part_hit: the part is hidden (blown off or destroyed)
            {
                auto part = obj.FindPart(Text(o[0]));
                auto hit = part >= 0 && (obj.HiddenParts & (1 << (part & 31))) != 0;
                if (hit)
                {
                    obj.HitEvent = 0;
                }

                return Branch(obj, ins, hit);
            }
            case 32: // clear_parts_mask: shows parts again, except blown off ones
                obj.HiddenParts = PartsMask(obj, L(o[0]), obj.HiddenParts, Masking::Show);
                break;
            case 195: // if_hit_weapon
                return Branch(obj, ins, obj.HitType == I(o[0]));
            case 158: // touch_damage: stops the script when the object died of it
            {
                constexpr int GoesOnHit = 2;
                auto hit = _runtime->TouchDamage(obj, I(o[0]), I(o[1]), I(o[2]));
                if (obj.Dead || obj.Health == 0)
                {
                    return Yield;
                }

                if (hit && (I(o[2]) & GoesOnHit) != 0)
                {
                    return Goto(obj, I(o[3]));
                }

                break;
            }
            case 184: // explode: a blast on everything around, then the object dies
            {
                constexpr int AllTargets = 7;
                constexpr int BlastHitType = -5;
                obj.Flags |= MdkObject::FlagNotTarget;
                if (F(o[0]) != 0.0f && F(o[1]) != 0.0f)
                {
                    _runtime->Items->Blast(obj.Position, Round(F(o[0])), F(o[1]), AllTargets, BlastHitType, nullptr, Items::Kills::Ignored);
                }

                _runtime->Kill(obj);
                return Yield;
            }
            case 172: // explosion
            {
                auto point = Point(obj, L(o[0]));
                _runtime->PlaySoundAt(std::string(ScriptRuntime::ExplodeSound), point);
                _runtime->SpawnExplosion(obj.Arena, point, F(o[1]));
                break;
            }
            case 178: // explosion_damage: flags are the targets (1 Kurt, 2 objects, 4 triangle groups)
            {
                constexpr int BlastHitType = -5;
                auto point = Point(obj, L(o[0]));
                _runtime->PlaySoundAt(std::string(ScriptRuntime::ExplodeSound), point);
                _runtime->SpawnExplosion(obj.Arena, point, F(o[1]));
                _runtime->Items->Blast(point, Round(F(o[2])), F(o[3]), I(o[4]), BlastHitType, nullptr);
                break;
            }
            case 156: // spawn_at_point
                _runtime->Spawn(obj, Text(o[1]), obj.ReferencePoint(I(o[0])), 0.0f, -1, I(o[2]), ScriptRuntime::Spawning::Plain);
                break;
            case 205: // set_21f: indestructible
                obj.Indestructible = I(o[0]) != 0;
                break;

            // Kurt and the screen.
            case 135: // raise_573aa8: shakes the screen
                _runtime->RaiseShake(F(o[0]));
                break;
            case 215: // screen_flash
                _runtime->Kurt->WhiteFlash = MathF::Max(_runtime->Kurt->WhiteFlash, MathF::Round(Value(obj, o[0])));
                break;
            case 240: // set_hurt_flash: Kurt's knock-down counter (1 means 5), unless he's invulnerable
            {
                constexpr float DefaultKnock = 5.0f;
                if (_runtime->Kurt->Invulnerable <= 0.0f)
                {
                    _runtime->Kurt->KnockDamage = I(o[0]) == 1 ? DefaultKnock : F(o[0]);
                }

                break;
            }
            case 244: // set_global_573bd4: Kurt's invulnerability time
                _runtime->Kurt->Invulnerable = I(o[0]) == 0 ? F(o[1]) : _runtime->Kurt->Invulnerable + F(o[1]);
                break;
            case 248: // push_kurt: knocks Kurt down, pushed away
                _runtime->PushKurt(obj, L(o[0]));
                break;
            case 170: // inherit_kurt_vel
                obj.Velocity += _runtime->Kurt->Velocity() * F(o[0]);
                break;
            case 217: // global_573c4c_add (a count shown after the level)
                if (I(o[0]) == 1)
                {
                    _runtime->Stats->HeadShots += I(o[1]);
                }

                break;
            case 202: // set_574304: how the sky is drawn (0 normally)
                _runtime->SkyMode = I(o[0]);
                break;

            // More movement.
            case 63: // set_flag148_10_inv: Kurt goes through the object (0x10)
                obj.Flags = I(o[0]) != 0 ? obj.Flags & ~MdkObject::FlagNotSolid : obj.Flags | MdkObject::FlagNotSolid;
                break;
            case 41: // set_targetable: flag 0x100 (a platform), and 0x800000 with mode 2
            {
                constexpr int Platform = 0x100;
                constexpr int StandableMode = 2;
                obj.Flags = I(o[0]) != 0 ? obj.Flags | Platform : obj.Flags & ~Platform;
                obj.Flags = I(o[0]) == StandableMode ? obj.Flags | MdkObject::FlagStandable : obj.Flags & ~MdkObject::FlagStandable;
                break;
            }
            case 79: // set_pos
                obj.Position = V(o);
                break;
            case 91: // set_path_speed
                obj.PathSpeed = Value(obj, o[0]);
                break;
            case 187: // random_kick
            {
                // Separate statements: C# evaluates left to right, C++ leaves the order open.
                auto heading = rng.NextSingle() * FullTurn;
                auto spread = 0.5f + rng.NextSingle();
                auto kick = FromAngle(heading) * F(o[0]) * spread;
                obj.Velocity += Vector3(kick.X, kick.Y, F(o[1]) * (0.5f + rng.NextSingle()));
                break;
            }
            case 126: // aim_pitch_target
            {
                constexpr float AimHeight = 3.0f;
                auto d = _runtime->TargetPosition - obj.Position;
                obj.Pitch = ToDegrees(MathF::Atan2(d.Z + AimHeight, Vector2(d.X, d.Y).Length()));
                break;
            }
            case 218: // set_f13c: the pitch
                obj.Pitch = F(o[0]);
                break;
            case 157: // vel_away_from (a negative speed moves towards it)
            {
                auto other = FirstOther(obj, o[0], I(o[1]));
                auto away = other == nullptr ? Vector3::Zero() : obj.Position - other->Position;
                if (away != Vector3::Zero())
                {
                    obj.Velocity = Vector3::Normalize(away) * F(o[2]);
                    obj.Path = 0;
                }

                break;
            }
            case 210: // set_2c0 (not identified)
                obj.Value2c0 = Value(obj, o[0]);
                break;
            case 199: // set_104 (not identified)
                obj.Value104 = Value(obj, o[0]);
                break;
            case 183: // switch_gosub: a gosub picked by a variable, returning after the table
            {
                auto index = ToInt(Variables(obj, I(o[0]))[Math::Clamp(I(o[1]), 0, VariableCount - 1)]);
                const auto& table = L(o[2]);
                if (index >= 0 && index < static_cast<int>(table.size()))
                {
                    return Gosub(obj, ins.Next, I(L(table[index]).at(0)));
                }

                break;
            }
            case 251: // set_target_mode: 1 the aliens' target, 2 always targets Kurt
                obj.TargetMode = I(o[0]);
                if (I(o[0]) == 1)
                {
                    _runtime->AlienTarget = obj.shared_from_this();
                }
                else if (I(o[0]) == 0 && _runtime->AlienTarget.get() == &obj)
                {
                    _runtime->AlienTarget = nullptr;
                }

                break;
            case 27: // if_alarm: an object sounds the alarm (movement command 15)
                return Branch(obj, ins, _runtime->AlarmTicks > 0);
            case 233: // if_sound_playing
                return Branch(obj, ins, _runtime->IsSoundPlaying(Text(o[0])));
            case 192: // if_no_flat_floor_at: no floor below a point in front, or a steep one
            {
                auto offset = Rotated(Vector2(F(o[0]), F(o[1])), obj.Yaw);
                auto point = obj.Position + Vector3(offset.X, offset.Y, 1.0f);
                auto hit = _runtime->Raycast(point, point - Vector3(0.0f, 0.0f, F(o[2]) + 1.0f));
                return Branch(obj, ins, !hit.has_value() || MathF::Abs(hit->Normal.Z) < F(o[3]));
            }
            case 243: // if_point_sees_player
            {
                auto point = obj.ReferencePoint(I(o[0]));
                auto chest = _runtime->KurtPosition + Vector3(0.0f, 0.0f, 4.0f);
                return Branch(obj, ins, Vector3::Distance(point, _runtime->KurtPosition) <= F(o[1]) && !_runtime->Raycast(point, chest).has_value());
            }
            case 13: // if_cheat_key (cheat keys aren't done)
                return Branch(obj, ins, false);
            case 171: // if_is_573c30: the object Kurt rides
                return Branch(obj, ins, _runtime->Rides->Ridden.get() == &obj);
            case 77: // debug_msg
                break;
            case 204: // avoid_objects: turns away from an object it overlaps
            {
                auto box = _runtime->GetWorldBounds(obj);
                for (const auto& other : _runtime->GetArenaObjects(obj))
                {
                    if ((other->Flags & (MdkObject::FlagNotSolid | MdkObject::FlagNotSolid2)) != 0 || !Intersects(box, _runtime->GetWorldBounds(*other)))
                    {
                        continue;
                    }

                    auto away = WrapAngle(obj.YawTo(other->Position) + HalfTurn - obj.Yaw);
                    obj.Yaw = Wrap360(obj.Yaw + MathF::Sign(away) * MathF::Min(MathF::Abs(away), HalfTurn * Dt));
                    break;
                }

                break;
            }
            case 49: // release_children: lets go of the last followers of a chain
                for (auto i = 0; i < I(o[0]); i++)
                {
                    auto last = LastChainLink(obj);
                    if (last == nullptr)
                    {
                        return Branch(obj, ins, true);
                    }

                    last->Leader = nullptr;
                    last->MoveCommand = 0;
                }

                break;
            case 129: // blow_off_parts: the parts go (the debris isn't drawn yet) and stay hidden; mode 2 only
            {
                // with gore (0x45c498)
                constexpr int GoreMode = 2;
                auto mode = I(o[0]);
                if (mode < 0 || mode > GoreMode || obj.Model == nullptr || (mode == GoreMode && _runtime->Option == 0))
                {
                    break;
                }

                auto mask = 0;
                for (const auto& name : L(o[1]))
                {
                    auto part = obj.FindPart(Text(name));
                    if (part >= 0)
                    {
                        mask |= 1 << (part & 31);
                    }
                }

                obj.LockedParts |= mask;
                obj.HiddenParts |= mask;
                break;
            }
            case 112: // teleport_player
                _runtime->TeleportKurt(Text(o[0]), V(o, 1), F(o[4]));
                if (!Text(o[0]).empty())
                {
                    obj.Restart = 0;
                    ClearStack(obj);
                    return Yield;
                }

                break;
            case 223: // arena_set_neighbour
                _runtime->PreloadArena(Text(o[0]));
                break;
            case 239: // set_27c (not identified)
                break;

            // The 1996 demo's own opcodes (ScriptDecoder.Beta.cs).
            case Formats::Scripts::BetaOpcodes::FollowPath:
                FollowBetaPath(obj, I(o[0]));
                break;
            case Formats::Scripts::BetaOpcodes::Fire:
                FireBolt(obj, o);
                break;
            case Formats::Scripts::BetaOpcodes::IfAlarmEnded:
                return Branch(obj, ins, _runtime->AlarmEndedTicks > 0);
            case Formats::Scripts::BetaOpcodes::IfField108:
                return Branch(obj, ins, false);
            case Formats::Scripts::BetaOpcodes::Nothing:
                break;
            default:
                Unimplemented[ins.Opcode] = Unimplemented.GetValueOrDefault(ins.Opcode) + 1;
                if (ins.Action)
                {
                    return Branch(obj, ins, false);
                }

                break;
        }

        return ins.Next;
    }
}

namespace Mdk::Game::Scripts
{
    using namespace ScriptMath;
    using Formats::Scripts::BranchAction;
    using Objects::MdkObject;

    int ScriptVm::PartsMask(MdkObject& obj, const OperandList& entries, int mask, Masking masking)
    {
        constexpr std::string_view All = "ALL";
        if (obj.Model == nullptr)
        {
            return mask;
        }

        for (const auto& entry : entries)
        {
            auto name = std::get_if<std::string>(&L(entry).at(0));
            std::string_view text = name != nullptr ? std::string_view(*name) : std::string_view();
            for (size_t i = 0; i < obj.Model->PartList.size(); i++)
            {
                if (!Str::EqualsIgnoreCase(All, text) && !Str::EqualsIgnoreCase(obj.Model->PartList[i]->Name, text))
                {
                    continue;
                }

                auto bit = 1 << (static_cast<int>(i) & 31);
                if (masking == Masking::Hide)
                {
                    mask |= bit;
                }
                else if ((obj.LockedParts & bit) == 0)
                {
                    mask &= ~bit;
                }
            }
        }

        return mask;
    }

    int ScriptVm::GroupIndex(const Operand& group)
    {
        return (I(group) - 1) & (ScriptRuntime::GroupCount - 1);
    }

    void ScriptVm::BossBar(MdkObject& obj, const OperandList& source)
    {
        auto state = _runtime->GetArenaState(obj.Arena);
        auto maxHealth = 0;
        auto value = 0;
        switch (I(source.at(0)))
        {
            case 0:
                maxHealth = I(source.at(2));
                value = maxHealth - state->GroupCounters[GroupIndex(source.at(1))];
                break;
            case 1:
            {
                maxHealth = I(source.at(4));
                auto index = I(source[1]) <= VariableCount - 1 ? I(source[1]) : 0;
                auto high = F(source[3]);
                value = Round((high - state->Variables.at(index)) / (high - F(source[2])) * maxHealth);
                break;
            }
        }

        if (maxHealth > 0 && _runtime->Kurt->Firing)
        {
            _runtime->ShowBarValues(Math::Min(value, maxHealth), maxHealth);
        }
    }

    void ScriptVm::LobToKurt(MdkObject& obj, const OperandList& args)
    {
        if (I(args.at(0)) > 1)
        {
            return;
        }

        auto targetZ = I(args[0]) == 0 ? F(args.at(2)) : _runtime->KurtPosition.Z;
        auto fall = targetZ - obj.Position.Z;

        // The fall time of 0.5 × gravity × t² = fall with the current vertical speed.
        auto a = obj.Gravity * -0.5f;
        auto discriminant = obj.Velocity.Z * obj.Velocity.Z + 4.0f * a * fall;
        if (fall > 0.0f || discriminant < 0.0f)
        {
            return;
        }

        auto root = MathF::Sqrt(discriminant);
        auto time = MathF::Max((root - obj.Velocity.Z) / (a * 2.0f), (-obj.Velocity.Z - root) / (a * 2.0f));
        auto toKurt = Vector2(_runtime->KurtPosition.X - obj.Position.X, _runtime->KurtPosition.Y - obj.Position.Y);
        auto distance = toKurt.Length();
        if (time <= 0.0f || distance <= 0.0f)
        {
            return;
        }

        // The friction over the flight has to be made up for.
        auto speed = MathF::Min(obj.Friction * 0.5f * time + distance / time, F(args.at(1)));
        auto velocity = toKurt * (speed / distance);
        obj.Velocity.X = velocity.X;
        obj.Velocity.Y = velocity.Y;
    }

    bool ScriptVm::InBox(MdkObject& obj, const OperandList& o)
    {
        constexpr int ByType = 0xFF;
        constexpr int Flat = 2;
        constexpr int Solid = 3;
        if (I(o[0]) != ByType)
        {
            return false;
        }

        std::array<float, BoxValues> box{};
        const auto& values = L(o[3]);
        for (auto i = 0; i < Math::Min(static_cast<int>(values.size()), BoxValues); i++)
        {
            box[i] = F(values[i]);
        }

        auto mode = I(o[2]);
        for (const auto& other : _runtime->Objects)
        {
            if (other->Dead || other->Arena != obj.Arena || !Is(other->TypeName, o[1]))
            {
                continue;
            }

            auto p = other->Position;
            auto inside = mode == Flat
                ? p.X >= box[0] && p.Y >= box[1] && p.X <= box[2] && p.Y <= box[3]
                : p.X >= box[0] && p.Y >= box[1] && p.X <= box[3] && p.Y <= box[4] && (mode != Solid || (p.Z >= box[2] && p.Z <= box[5]));
            if (inside)
            {
                return true;
            }
        }

        return false;
    }

    std::shared_ptr<MdkObject> ScriptVm::FindObject(MdkObject& obj, const std::string& type, int mode)
    {
        constexpr int ItemsMode = 2;
        constexpr float ItemRange = 200.0f;
        constexpr float ItemAbove = 9.0f;
        auto& found = _found;
        found.clear();
        std::shared_ptr<MdkObject> best;
        auto bestDistance = std::numeric_limits<float>::infinity();
        for (const auto& other : _runtime->GetArenaObjects(obj))
        {
            if (!Str::EqualsIgnoreCase(other->TypeName, type))
            {
                continue;
            }

            auto distance = obj.DistanceTo(other->Position);
            if (mode == ItemsMode && (distance > ItemRange || (other->Flags & Items::FlagThrown) == 0 || other->Position.Z > obj.Position.Z + ItemAbove
                || _runtime->Raycast(obj.Position + Vector3(0.0f, 0.0f, 5.0f), other->Position + Vector3(0.0f, 0.0f, 2.0f)).has_value()))
            {
                continue;
            }

            found.push_back(other);
            if (distance < bestDistance)
            {
                best = other;
                bestDistance = distance;
            }
        }

        if (found.empty())
        {
            return nullptr;
        }

        // Released after the pick, as C#'s list only holds references.
        auto picked = mode == 0 ? found[_runtime->Rng->Next(static_cast<int>(found.size()))] : best;
        found.clear();
        return picked;
    }

    void ScriptVm::LinkPartners(MdkObject& obj, const std::string& type)
    {
        constexpr float RangeSquared = 10000.0f;
        std::vector<std::shared_ptr<MdkObject>> loose;
        for (const auto& other : _runtime->Objects)
        {
            if (!other->Dead && other->Arena == obj.Arena && Str::EqualsIgnoreCase(other->TypeName, type)
                && (other->Linked == nullptr || other->Linked->Dead))
            {
                loose.push_back(other);
            }
        }

        for (size_t i = 0; i < loose.size(); i++)
        {
            if (loose[i]->Linked != nullptr)
            {
                continue;
            }

            for (auto j = i + 1; j < loose.size(); j++)
            {
                if (loose[j]->Linked != nullptr || Vector3::DistanceSquared(loose[i]->Position, loose[j]->Position) >= RangeSquared)
                {
                    continue;
                }

                loose[i]->Linked = loose[j];
                loose[j]->Linked = loose[i];
                break;
            }
        }
    }

    Vector3 ScriptVm::Point(MdkObject& obj, const OperandList& operand)
    {
        switch (I(operand.at(0)))
        {
            case 3:
                return obj.ReferencePoint(I(operand.at(1)));
            case 1:
                return obj.Position + V(operand, 1);
            case 2:
                return obj.Position + RotatedZ(V(operand, 1), obj.Yaw);
            default:
                return V(operand, 1);
        }
    }

    void ScriptVm::StartMove(MdkObject& obj, int command, Vector3 destination)
    {
        obj.MoveCommand = command;
        obj.MoveDestination = destination;
        obj.Path = 0;
        _runtime->Motion->PlanMove(obj);
    }

    void ScriptVm::FollowPath(MdkObject& obj, const OperandList& o)
    {
        constexpr int Flag200 = 0x200;
        auto path = I(o[0]);
        if (path == 0)
        {
            obj.Path = 0;
            return;
        }

        auto& motion = *_runtime->Motion;
        obj.Flags = (I(o[1]) & 1) != 0 ? obj.Flags | Flag200 : obj.Flags & ~Flag200;
        obj.Flags = (I(o[1]) & 2) != 0 ? obj.Flags | MdkObject::FlagPathPushes : obj.Flags & ~MdkObject::FlagPathPushes;
        obj.Flags = (I(o[2]) & 1) != 0 ? obj.Flags | MdkObject::FlagPathOnce : obj.Flags & ~MdkObject::FlagPathOnce;
        auto time = static_cast<float>(I(o[3]));
        if ((I(o[2]) & 2) != 0)
        {
            // Backwards: start frame 0 means the end of the path.
            obj.PathSpeed = -1.0f;
            if (I(o[3]) == 0)
            {
                time = static_cast<float>(motion.PathKeyFrame(path, motion.PathKeyCount(path) - 1) - 1);
            }
        }

        auto origin = I(o[4]) != 0 ? obj.Position - motion.PathPosition(path, time) : V(L(o[5]));
        motion.StartPath(obj, path, time, origin);
    }

    void ScriptVm::FollowBetaPath(MdkObject& obj, int path)
    {
        if (path == 0)
        {
            obj.Path = 0;
            return;
        }

        if (obj.Path == path)
        {
            obj.PathStop = -1;
            return;
        }

        obj.Flags &= ~(MdkObject::FlagPathPushes | MdkObject::FlagPathOnce | MdkObject::FlagNoTurning);
        _runtime->Motion->StartPath(obj, path, 0.0f, Vector3::Zero());
    }

    void ScriptVm::FireBolt(MdkObject& obj, const OperandList& o)
    {
        auto bolt = _runtime->Fire(obj, L(o[0]), std::string(BetaBolt), 0);
        if (bolt == nullptr)
        {
            return;
        }

        if (I(o[1]) != 0)
        {
            Aim(*bolt, F(o[3]), Aiming::Always);
        }

        bolt->Speed = BetaBoltSpeed;
        bolt->Parameter = F(o[2]) / BetaBoltSpeed;
        bolt->TouchDamage = BetaBoltDamage;
    }

    void ScriptVm::Aim(MdkObject& obj, float accuracy, Aiming aiming)
    {
        constexpr float Perfect = 100.0f;
        constexpr float AimHeight = 3.0f;
        constexpr float MinAcross = 2.0f;
        auto target = _runtime->TargetPosition;
        auto p = obj.Position;
        auto horizontal = Distance2D(target, p);
        if (aiming == Aiming::Always || horizontal > MinAcross)
        {
            obj.Yaw = obj.YawTo(target);
        }

        obj.Pitch = ToDegrees(MathF::Atan2(target.Z + AimHeight - p.Z, horizontal));
        if (accuracy == Perfect)
        {
            return;
        }

        auto distance = obj.DistanceTo(target) + 0.1f;
        obj.Yaw = Wrap360(obj.Yaw + (_runtime->Rng->Next(20) - 10) * (Perfect - accuracy) / distance);
        obj.Pitch += (_runtime->Rng->Next(20) - 10) * (Perfect - accuracy) / (distance * PitchErrorScale);
    }

    void ScriptVm::TurnToTarget(MdkObject& obj, float rate, float accuracy, float a, float b, std::optional<float> zOffset)
    {
        constexpr float Perfect = 100.0f;
        auto target = _runtime->TargetPosition;
        auto [s, c] = MathF::SinCos(ToRadians(_runtime->TargetYaw));
        auto point = Vector3(target.X - (a * c + b * s), target.Y - (b * c + a * s), target.Z);
        auto horizontal = Distance2D(point, obj.Position);
        auto yaw = obj.YawTo(point);
        if (accuracy != Perfect)
        {
            yaw += (_runtime->Rng->Next(20) - 10) * (Perfect - accuracy) / (1.0f + horizontal);
        }

        auto maxStep = rate * Dt;
        obj.Yaw = Wrap360(obj.Yaw + Math::Clamp(WrapAngle(yaw - obj.Yaw), -maxStep, maxStep));
        if (!zOffset)
        {
            return;
        }

        point.Z = target.Z + *zOffset;
        auto pitch = ToDegrees(MathF::Atan2(point.Z - obj.Position.Z + obj.HeightOffset, horizontal));
        if (accuracy != Perfect)
        {
            pitch += (_runtime->Rng->Next(20) - 10) * (Perfect - accuracy) / (1.0f + horizontal * PitchErrorScale);
        }

        obj.Pitch += Math::Clamp(WrapAngle(pitch - obj.Pitch), -maxStep, maxStep);
    }

    void ScriptVm::CommandObjects(MdkObject& obj, const OperandList& o)
    {
        constexpr int GotoCommand = 7;
        constexpr int GosubCommand = 0xFC;
        constexpr int MoveCommand = 43;
        auto command = I(o[0]);
        auto selector = I(o[2]);
        const auto& args = L(o[3]);
        auto target = 0;
        auto destination = Vector3::Zero();
        if (command == GotoCommand)
        {
            auto action = std::get_if<BranchAction>(&o[1]);
            if (action == nullptr)
            {
                return;
            }

            switch (action->Type)
            {
                case BranchAction::Kind::Gosub:
                    command = GosubCommand;
                    target = action->Target;
                    break;
                case BranchAction::Kind::Goto:
                case BranchAction::Kind::GosubElse:
                    target = action->Target;
                    break;
                default:
                    return;
            }
        }
        else if (command == MoveCommand)
        {
            const auto& offset = L(o[1]);
            destination = _runtime->NearTargetDestination(obj, F(offset.at(0)), F(offset.at(1)));
        }

        size_t k = 0;
        auto param = 0.0f;
        std::string type;
        auto id = 0;
        if (selector == 6 || selector == 10)
        {
            param = F(args.at(k++));
        }

        if (selector == 2 || selector == 4 || selector == 5 || selector == 6 || selector == 7 || selector == 10)
        {
            type = Text(args.at(k++));
        }

        if (selector == 5)
        {
            id = I(args.at(k));
        }

        _runtime->CommandObjects(obj, command, target, destination, selector, type, param, id);
    }
}
