using System.Numerics;
using Mdk.Formats.Scripts;
using Mdk.Game.Objects;
using static Mdk.Game.Scripts.ScriptMath;

namespace Mdk.Game.Scripts;

/// <summary>Runs MDK object scripts like the original interpreter (script_run). A port of godot-mdk's
/// <c>script_vm.gd</c> (see its docs/scripts.md and docs/script_opcodes.md). Opcodes not implemented
/// do nothing (their conditions are false), but they're always decoded, so scripts never lose sync.
/// <code>
///   restart ──► (wait over?) ──► decode ──► execute ──► next pc ─┐
///                                   ▲                             │
///                                   └──────── until YIELD, 0xFF ──┘
/// </code></summary>
public sealed class ScriptVm(ScriptRuntime runtime, ScriptDecoder decoder)
{
    public const int MaxOpcodesPerFrame = 1000;
    public const int GosubDepth = 4;
    /// <summary>Returned by handlers: stop running this object's script for this frame.</summary>
    private const int Yield = -1;
    /// <summary>Frame time in seconds and in 30 Hz ticks.</summary>
    private const float Dt = ScriptRuntime.Tick;
    private const float Ticks = 1f;
    /// <summary>Comparisons' tolerance (compare_values).</summary>
    private const float Epsilon = 0.05f;
    private const float MinWait = 1e-5f;
    /// <summary>The pitch error of inaccurate aims is a quarter of the yaw's.</summary>
    private const float PitchErrorScale = 4f;
    private const int FlagBits = 31;
    private const int VariableCount = 4;
    /// <summary>The 1996 demo's <c>fire</c> (<see cref="FireBolt"/>): a <c>BOLT</c> at 75 units a
    /// second that takes 10 off Kurt's health (0x4e2ec).</summary>
    private const string BetaBolt = "BOLT";
    private const float BetaBoltSpeed = 75f;
    private const int BetaBoltDamage = 10;

    /// <summary>Opcodes seen that aren't implemented (opcode to count), for debugging.</summary>
    public readonly Dictionary<int, int> Unimplemented = [];

    /// <summary>Opcodes run so far in the current object's frame (if_opcode_count).</summary>
    private int _opcodeCount;

    /// <summary>Runs one frame of an object's script.</summary>
    public void Run(MdkObject obj)
    {
        if (obj.Restart == 0 || obj.Dead)
        {
            return;
        }

        runtime.SelectTarget(obj);
        var pc = obj.Restart;
        if (obj.WaitTime > 0f)
        {
            obj.WaitTime -= Dt;
            if (obj.WaitTime > 0f)
            {
                return;
            }

            obj.WaitTime = 0f;
            pc = obj.WaitResume;
        }

        var count = 0;
        while (true)
        {
            var ins = decoder.Decode(pc);
            if (ins == null)
            {
                // Unknown opcode: the script stops (the original only writes to its debug log).
                obj.Restart = 0;
                return;
            }

            if (ins.Opcode == ScriptDecoder.End)
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

            pc = Execute(obj, ins);
            if (pc == Yield || obj.Dead)
            {
                return;
            }
        }
    }

    /// <summary>Applies a branch action when the condition holds (or the else gosub when not). Returns the next pc.</summary>
    private int Branch(MdkObject obj, Instruction ins, bool condition)
    {
        var action = ins.Action;
        if (action == null)
        {
            return ins.Next;
        }

        return action.Type switch
        {
            BranchAction.Kind.Goto => condition ? Goto(obj, action.Target) : ins.Next,
            BranchAction.Kind.Gosub => condition ? Gosub(obj, ins.Next, action.Target) : ins.Next,
            BranchAction.Kind.GosubElse => Gosub(obj, ins.Next, condition ? action.Target : action.Else),
            BranchAction.Kind.Return => condition ? Return(obj) : ins.Next,
            _ => ins.Next,
        };
    }

    private static int Goto(MdkObject obj, int target)
    {
        if (target == 0)
        {
            obj.Restart = 0;
            return Yield;
        }

        obj.Restart = target;
        obj.LevelTimers[obj.GosubReturns.Count] = 0f;
        return target;
    }

    private static int Gosub(MdkObject obj, int returnPc, int target)
    {
        if (obj.GosubReturns.Count >= GosubDepth || target == 0)
        {
            obj.Restart = 0;
            return Yield;
        }

        obj.GosubReturns.Add(returnPc);
        obj.GosubRestarts.Add(obj.Restart);
        obj.Restart = target;
        obj.LevelTimers[obj.GosubReturns.Count] = 0f;
        return target;
    }

    private static int Return(MdkObject obj)
    {
        if (obj.GosubReturns.Count == 0)
        {
            obj.Restart = 0;
            return Yield;
        }

        obj.Restart = Pop(obj.GosubRestarts);
        return Pop(obj.GosubReturns);
    }

    private static int Pop(List<int> stack)
    {
        var value = stack[^1];
        stack.RemoveAt(stack.Count - 1);
        return value;
    }

    private static void ClearStack(MdkObject obj)
    {
        obj.GosubReturns.Clear();
        obj.GosubRestarts.Clear();
    }

    /// <summary>A value operand: a float literal, or a variable.</summary>
    private float Value(MdkObject obj, object? operand)
    {
        if (operand is not Variable variable)
        {
            return F(operand);
        }

        return Variables(obj, variable.Kind)[variable.Index is >= 0 and < VariableCount ? variable.Index : 0];
    }

    /// <summary>Variables of a source: 0 global, 1 arena, 2 own, other = the linked object's.</summary>
    private float[] Variables(MdkObject obj, int source) => source switch
    {
        0 => runtime.GlobalVariables,
        1 => runtime.GetArenaState(obj.Arena).Variables,
        2 => obj.Variables,
        _ => obj.Linked?.Variables ?? NoVariables(),
    };

    /// <summary>The variables of a missing linked object: zeros, written to in vain (one array, cleared).</summary>
    private float[] NoVariables()
    {
        Array.Clear(_noVariables);
        return _noVariables;
    }

    private readonly float[] _noVariables = new float[VariableCount];

    /// <summary>Flag word 5: a door's own state (obj+0x312, 0x440a34); nothing on other objects.</summary>
    private const int DoorFlags = 5;

    private static bool IsDoor(MdkObject obj) => (obj.Flags & MdkObject.FlagDoor) != 0;

    private int GetFlags(MdkObject obj, int source) => source switch
    {
        0 => runtime.GlobalFlags,
        1 => runtime.GetArenaState(obj.Arena).Flags,
        2 => obj.ScriptFlags,
        DoorFlags => IsDoor(obj) ? obj.DoorState : 0,
        _ => obj.Linked?.ScriptFlags ?? 0,
    };

    private void SetFlags(MdkObject obj, int source, int value)
    {
        switch (source)
        {
            case 0:
                runtime.GlobalFlags = value;
                break;
            case 1:
                runtime.GetArenaState(obj.Arena).Flags = value;
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
                if (obj.Linked != null)
                {
                    obj.Linked.ScriptFlags = value;
                }

                break;
        }
    }

    /// <summary>A comparison operand [op, a] or [op, a, b] (compare_values).</summary>
    private static bool Compare(float value, object?[] cond) =>
        Compare(value, cond[0], cond[1], cond.Length > 2 ? cond[2] : null);

    /// <summary>A missing operand's 0, boxed once.</summary>
    private static readonly object BoxedZero = 0f;
    /// <summary>FindObject's candidates (kept: no list per call).</summary>
    private readonly List<MdkObject> _found = [];

    /// <summary><see cref="Compare(float, object?[])"/> on separate operands (no array).</summary>
    private static bool Compare(float value, object? op, object? first, object? second)
    {
        var a = F(first);
        return I(op) switch
        {
            1 => value < a,
            2 => value > a,
            3 => value < a + Epsilon,
            4 => value > a - Epsilon,
            5 => MathF.Abs(value - a) < Epsilon,
            6 => MathF.Abs(value - a) >= Epsilon,
            7 => value >= a && value <= F(second),
            8 => value <= a || value >= F(second),
            _ => false,
        };
    }

    /// <summary>The highest instance id of the live objects following <paramref name="leader"/>, or -1.
    /// (No lambda in <see cref="Execute"/>: one capturing its object would be made at every instruction.)</summary>
    private int LastFollowerId(MdkObject leader)
    {
        int? maxId = null;
        foreach (var other in runtime.Objects)
        {
            if (other.Leader == leader && !other.Dead)
            {
                maxId = Math.Max(maxId ?? int.MinValue, other.InstanceId);
            }
        }

        return maxId ?? -1;
    }

    /// <summary>The link of <paramref name="leader"/>'s chain (command 30) with the highest instance id, or null.</summary>
    private MdkObject? LastChainLink(MdkObject leader)
    {
        const int ChainCommand = 30;
        MdkObject? best = null;
        foreach (var other in runtime.Objects)
        {
            if (IsArenaOther(leader, other) && other.Leader == leader && other.MoveCommand == ChainCommand
                && (best == null || other.InstanceId > best.InstanceId))
            {
                best = other;
            }
        }

        return best;
    }

    /// <summary>The other live objects of <paramref name="obj"/>'s arena, as
    /// <see cref="ScriptRuntime.GetArenaObjects"/>, tested without a list.</summary>
    private bool IsArenaOther(MdkObject obj, MdkObject other) => other != obj && !other.Dead && other.Arena == obj.Arena;

    /// <summary>How the counting conditions count.</summary>
    private enum Counting { Commandable, Alive }

    private int CountOthers(MdkObject obj, object? type, Counting counting)
    {
        var count = 0;
        foreach (var other in runtime.Objects)
        {
            if (IsArenaOther(obj, other) && Is(other.TypeName, type)
                && (counting == Counting.Alive ? other.Health > 0 : ScriptRuntime.MayCommand(obj, other)))
            {
                count++;
            }
        }

        return count;
    }

    /// <summary>The first other object of the arena of a type (and instance, unless any).</summary>
    private MdkObject? FirstOther(MdkObject obj, object? type, int instance)
    {
        const int AnyInstance = -1;
        foreach (var other in runtime.Objects)
        {
            if (IsArenaOther(obj, other) && Is(other.TypeName, type) && (instance == AnyInstance || other.InstanceId == instance))
            {
                return other;
            }
        }

        return null;
    }

    /// <summary>Whether <paramref name="name"/> is "TYPE_ID" of the object (as $"{TypeName}_{InstanceId}").</summary>
    private static bool IsNamed(MdkObject other, object? name)
    {
        const int MaxDigits = 11;
        var text = (name as string ?? "").AsSpan();
        var type = other.TypeName.AsSpan();
        Span<char> id = stackalloc char[MaxDigits];
        other.InstanceId.TryFormat(id, out var digits);
        return text.Length == type.Length + 1 + digits && text[type.Length] == '_'
            && text[..type.Length].Equals(type, StringComparison.OrdinalIgnoreCase)
            && text[(type.Length + 1)..].Equals(id[..digits], StringComparison.OrdinalIgnoreCase);
    }

    private MdkObject? Bomb() => runtime.Items.Bomb is { Dead: false } bomb ? bomb : null;

    /// <summary>A 16-bit value as signed.</summary>
    private static int S16(int value)
    {
        const int SignBit = 0x8000;
        const int Range = 0x10000;
        return value >= SignBit ? value - Range : value;
    }

    /// <summary>Picks a weighted random target ([[weight, target], ...]).</summary>
    private int WeightedTarget(object?[] entries)
    {
        var total = 0;
        foreach (var entry in entries)
        {
            total += I(L(entry)[0]);
        }

        var r = runtime.Rng.Next(Math.Max(total, 1));
        foreach (var entry in entries)
        {
            r -= I(L(entry)[0]);
            if (r < 0)
            {
                return I(L(entry)[1]);
            }
        }

        return 0;
    }

    /// <summary>Picks a random target of a repeat operand of code offsets ([[target], ...]).</summary>
    private int RandomTarget(object?[] entries) => entries.Length == 0 ? 0 : I(L(entries[runtime.Rng.Next(entries.Length)])[0]);

    private static bool Is(string a, object? b) => a.Equals(b as string ?? "", StringComparison.OrdinalIgnoreCase);

    private int Execute(MdkObject obj, Instruction ins)
    {
        var o = ins.Operands;
        var rng = runtime.Rng;
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
                obj.WaitTime = MathF.Max(Value(obj, o[0]), MinWait);
                obj.WaitResume = ins.Next;
                return Yield;
            case 18: // if_timer: the counter of the current gosub level counts ticks
            {
                var level = obj.GosubReturns.Count;
                if (F(o[0]) * ScriptRuntime.TicksPerSecond > obj.LevelTimers[level])
                {
                    obj.LevelTimers[level] += Ticks;
                    return Branch(obj, ins, false);
                }

                obj.LevelTimers[level] = 0f;
                return Branch(obj, ins, true);
            }

            // Spawning.
            case 86: // spawn
                runtime.Spawn(obj, (string)o[3]!, V(o), 0f, -1, I(o[4]), ScriptRuntime.Spawning.Plain);
                break;
            case 161: // spawn_flagged
                runtime.SpawnFlagged(obj, (string)o[3]!, V(o), I(o[4]));
                break;
            case 206: // spawn_along_path: [path, step, type, script]: one object every step along the path
            {
                // (from time 0 to before its last key), flagged like spawn_flagged (0x2008a6)
                var path = I(o[0]);
                var end = runtime.Motion.PathKeyFrame(path, runtime.Motion.PathKeyCount(path) - 1);
                for (var time = 0f; time < end; time += F(o[1]))
                {
                    runtime.Spawn(obj, (string)o[2]!, runtime.Motion.PathPosition(path, time), 0f, -1, I(o[3]), ScriptRuntime.Spawning.Flagged);
                }

                break;
            }
            case 230: // spawn_ex
                runtime.Spawn(obj, (string)o[5]!, V(o), F(o[3]), I(o[4]), I(o[6]), ScriptRuntime.Spawning.Plain);
                break;
            case 113: // spawn_relative
            {
                var offset = Rotated(new Vector2(F(o[0]), F(o[1])), obj.Yaw);
                runtime.Spawn(obj, (string)o[3]!, obj.Position + new Vector3(offset.X, offset.Y, F(o[2])), 0f, -1, I(o[4]), ScriptRuntime.Spawning.Plain);
                break;
            }
            case 150: // door_set_anims: opening and closing animations
                obj.DoorAnimations = [I(o[0]), I(o[1])];
                break;
            case 151: // door_set_names: sounds when opening, closing, open, closed
                obj.DoorSounds = [(string)o[0]!, (string)o[1]!, (string)o[2]!, (string)o[3]!];
                break;
            case 152: // door_set_flags (the low 4 bits are the engine's state)
                obj.DoorState = (obj.DoorState & ObjectBehaviors.DoorEngineBits) | (I(o[0]) & ~ObjectBehaviors.DoorEngineBits);
                break;
            case 153: // door_set_param: distance at which the door opens
                obj.DoorDistance = F(o[0]);
                break;
            case 100: // arena_show
                runtime.ShowArena((string)o[0]!);
                break;
            case 149: // spawn_connector
                runtime.SpawnConnector(obj, (string)o[5]!, V(o), F(o[3]), I(o[4]), (string)o[6]!, I(o[7]));
                break;
            case 111: // set_instance
                obj.InstanceId = I(o[0]) & 0xFFFF;
                break;
            case 110: // delete_self
                runtime.Remove(obj);
                return Yield;
            case 76: // set_death_script
                obj.DeathScript = I(o[0]);
                break;
            case 16: // set_health
                obj.Health = I(o[0]);
                obj.MaxHealth = I(o[0]);
                obj.Indestructible = I(o[0]) >= ScriptRuntime.Indestructible;
                if (I(o[0]) == 0)
                {
                    runtime.Kill(obj);
                    return Yield;
                }

                break;
            case 56: // add_health
                if (obj.Health < ScriptRuntime.Indestructible)
                {
                    obj.Health += I(o[0]);
                    if (obj.Health <= 0)
                    {
                        runtime.Kill(obj);
                        return Yield;
                    }
                }

                break;

            // Animation.
            case 3: // anim_once
            case 59: // anim_loop
                obj.PlayAnimation(runtime.GetAnimation(obj, I(o[0])), ins.Opcode == 59 ? MdkObject.Looping.Loop : MdkObject.Looping.Once);
                break;
            case 17: // if_anim_done
                return Branch(obj, ins, obj.IsAnimationDone);
            case 92: // if_anim_frame
                return Branch(obj, ins, obj.IsAnimationDone || obj.AnimationFrame >= I(o[0]) - 1);
            case 118: // anim_end_frame (s16)
                obj.AnimationEndFrame = S16((I(o[0]) & 0xFFFF) - 1);
                break;
            case 24: // set_id_and_name: a sound played when the animation reaches frame n - 1
                obj.FrameSoundFrame = I(o[0]) - 1;
                obj.FrameSound = (string)o[1]!;
                break;
            case 25: // set_string154
            case 26: // set_string150
                obj.Labels[ins.Opcode - 25] = (string)o[0]!;
                break;
            case 19: // anim_mark_ended
                obj.AnimationEndFrame = MdkObject.AnimationEnded;
                break;
            case 31: // set_parts_mask
                obj.HiddenParts = PartsMask(obj, L(o[0]), obj.HiddenParts, Masking.Hide);
                break;
            case 83: // set_scale: a value, or [target, rate] to grow or shrink towards the target
                if (o[0] is object?[] { Length: 2 } scaling && scaling[0] is float target)
                {
                    var rate = F(scaling[1]);
                    if (obj.Scale < target)
                    {
                        obj.Scale = MathF.Min(obj.Scale * (1f + rate * Dt), target);
                    }
                    else if (obj.Scale > target)
                    {
                        obj.Scale = MathF.Max(obj.Scale * (1f - rate * Dt * 0.5f), target);
                    }
                }
                else
                {
                    obj.Scale = Value(obj, o[0]);
                }

                break;

            // Hits.
            case 22: // if_hit: any hit but blasts; the event is cleared when true
            {
                var hit = obj.HitEvent != 0 && obj.HitEvent != -3;
                if (hit)
                {
                    obj.HitEvent = 0;
                }

                return Branch(obj, ins, hit);
            }
            case 212: // if_hit_fd: a blast
            {
                var hit = obj.HitEvent == -3;
                if (hit)
                {
                    obj.HitEvent = 0;
                }

                return Branch(obj, ins, hit);
            }
            case 42: // if_hit_part: name (cleared when true) or ["", name] (not cleared); "ANY" = any part
            {
                var name = o[0] as string ?? (string)L(o[0])[1]!;
                var hit = obj.Model != null && obj.HitEvent > 0 && obj.HitEvent <= obj.Model.PartList.Count
                    && (Is("ANY", name) || Is(obj.Model.PartList[obj.HitEvent - 1].Name, name));
                if (hit && o[0] is string)
                {
                    obj.HitEvent = 0;
                }

                return Branch(obj, ins, hit);
            }
            case 198: // set_weak_parts
            {
                const int WeakPartCount = 8;
                obj.Flags |= MdkObject.FlagWeakParts;
                obj.WeakPrefix = (string)o[0]!;
                obj.WeakPrefixLength = I(o[1]);
                obj.PartHealth = Enumerable.Repeat(I(o[2]) & 0xFFFF, WeakPartCount).ToArray();
                obj.PartMaxHealth = (int[])obj.PartHealth.Clone();
                break;
            }
            case 81: // push_hit_dir: velocity along the direction of the last hit
            {
                var speed = Value(obj, o[1]) * (I(o[0]) == 1 ? Dt : 1f);
                var direction = FromAngle(obj.HitDirection) * speed;
                obj.Velocity.X += direction.X;
                obj.Velocity.Y += direction.Y;
                break;
            }
            case 108: // if_touching_kurt
                if (obj.MoveCommand == 61)
                {
                    return Branch(obj, ins, (obj.ContactFlags & MdkObject.ContactTouchedKurt) != 0);
                }

                return Branch(obj, ins, runtime.GetWorldBounds(obj).Intersects(runtime.GetKurtBox()));
            case 109: // hurt_kurt
                runtime.HurtKurt(I(o[0]));
                break;
            case 61: // fire
                runtime.Fire(obj, L(o[0]), (string)o[1]!, I(o[2]));
                break;

            // Orientation.
            case 8: // set_yaw
                obj.Yaw = Wrap360(F(o[0]));
                break;
            case 60: // face_target
                obj.Yaw = obj.YawTo(runtime.TargetPosition);
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
                obj.Pitch += Math.Clamp(WrapAngle(F(o[1]) - obj.Pitch), -F(o[0]) * Dt, F(o[0]) * Dt);
                break;
            case 97: // set_banking (automatic banking, ObjectMotion.Bank)
                if (I(o[0]) != 0)
                {
                    obj.Flags &= ~MdkObject.FlagNoBanking;
                }
                else
                {
                    obj.Flags |= MdkObject.FlagNoBanking;
                    obj.Roll = 0f;
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
                    obj.Roll = 0f;
                }

                break;
            case 207: // turn_to_yaw
            {
                var goal = F(o[1]);
                var diff = WrapAngle(goal - obj.Yaw);
                var step = F(o[0]) * Dt;
                obj.Yaw = Wrap360(MathF.Abs(diff) <= step ? goal : obj.Yaw + MathF.Sign(diff) * step);
                if (ins.Action?.Type == BranchAction.Kind.None)
                {
                    return ins.Next;
                }

                return Branch(obj, ins, MathF.Abs(obj.Yaw - Wrap360(goal)) < 1e-4f);
            }
            case 101: // aim_target
                Aim(obj, 100f, Aiming.CheckDistance);
                break;
            case 104: // aim_target_inaccurate
                Aim(obj, F(o[0]), Aiming.Always);
                break;
            case 235: // turn_to_target_offset
                TurnToTarget(obj, F(o[0]), F(o[1]), F(o[2]), F(o[3]), null);
                break;
            case 105: // turn_to_target_offset3d
                TurnToTarget(obj, F(o[0]), F(o[1]), F(o[2]), F(o[3]), F(o[4]));
                break;
            case 62: // if_target_angle
            {
                var angle = MathF.Abs(WrapAngle(obj.YawTo(runtime.TargetPosition) - obj.Yaw));
                return Branch(obj, ins, Compare(angle, L(o[0])));
            }
            case 193: // face_mode
            {
                var face = L(o[0]);
                var mode = I(face[0]);
                if (mode is 1 or 2 && obj.Velocity.X != 0f && obj.Velocity.Y != 0f)
                {
                    obj.Yaw = Wrap360(float.RadiansToDegrees(MathF.Atan2(obj.Velocity.Y, obj.Velocity.X)));
                    if (mode == 2 && obj.Velocity.Z != 0f)
                    {
                        obj.Pitch = float.RadiansToDegrees(MathF.Atan2(obj.Velocity.Z, new Vector2(obj.Velocity.X, obj.Velocity.Y).Length()));
                    }
                }
                else if (mode == 3)
                {
                    var other = FirstOther(obj, face[1], -1);
                    if (other != null)
                    {
                        obj.Yaw = other.Yaw;
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
                if (obj.Arena == runtime.CurrentArena)
                {
                    StartMove(obj, 43, runtime.NearTargetDestination(obj, F(o[0]), F(o[1])));
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
                const int Stop = 2;
                obj.Path = 0;
                if (I(o[0]) != Stop)
                {
                    obj.MoveCommand = 88;
                    obj.MoveParameter = I(o[0]);
                }
                else
                {
                    obj.MoveCommand = 0;
                    obj.Speed = 0f;
                }

                break;
            }
            case 44: // if_move_done
                return Branch(obj, ins, obj.MoveCommand == 0);
            case 54: // if_dest_dist (2D when on the floor)
            {
                var toDestination = obj.MoveDestination - obj.Position;
                if ((obj.ContactFlags & MdkObject.ContactFloor) != 0)
                {
                    toDestination.Z = 0f;
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
                var direction = FromAngle(obj.Yaw) * Value(obj, o[0]);
                obj.Push += new Vector3(direction.X, direction.Y, 0f);
                break;
            }
            case 201: // push_dir
            {
                var direction = FromAngle(obj.Yaw + F(o[1])) * F(o[0]);
                obj.Push += new Vector3(direction.X, direction.Y, 0f);
                break;
            }
            case 80: // add_vel_local
            {
                var (s, c) = MathF.SinCos(float.DegreesToRadians(obj.Yaw));
                obj.Velocity += new Vector3(-F(o[0]) * c - F(o[1]) * s, -F(o[1]) * c - F(o[0]) * s, F(o[2]));
                break;
            }
            case 93: // accel_toward_point: velocity towards a point (Manhattan-normalized)
            {
                var toPoint = V(o, 1) - obj.Position;
                var length = MathF.Abs(toPoint.X) + MathF.Abs(toPoint.Y) + MathF.Abs(toPoint.Z);
                if (length > 0f)
                {
                    obj.Velocity += toPoint / length * F(o[0]);
                }

                break;
            }
            case 211: // stop_velocity
                obj.Velocity = Vector3.Zero;
                break;
            case 38: // if_vel_z
                return Branch(obj, ins, Compare(obj.Velocity.Z, L(o[0])));
            case 37: // if_flag14c_2 (on the floor)
                return Branch(obj, ins, (obj.ContactFlags & MdkObject.ContactFloor) != 0);
            case 200: // move_to_point
            {
                // 0x459555: a Manhattan step, clamped per axis; gravity leaves z alone. (Euclid made
                // diagonals faster: XGEN's targets hit their pedestals' glass sides too low.)
                const float Arrived = 0.5f;
                const float MinDistance = 0.1f;
                var toPoint = V(o, 1) - obj.Position;
                if ((obj.Flags & MdkObject.FlagGravity) != 0)
                {
                    toPoint.Z = 0f;
                }

                var distance = MathF.Abs(toPoint.X) + MathF.Abs(toPoint.Y) + MathF.Abs(toPoint.Z);
                if (distance < Arrived)
                {
                    return Branch(obj, ins, true);
                }

                var step = F(o[0]) * Dt / MathF.Max(distance, MinDistance) * toPoint;
                obj.Push += Vector3.Clamp(step, -Vector3.Abs(toPoint), Vector3.Abs(toPoint)) / Dt;
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
                const int CurrentFrame = -2;
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
                return Branch(obj, ins, Bomb() == null);
            case 166: // if_bomb_visible
            {
                var bomb = Bomb();
                var up = new Vector3(0f, 0f, 5f);
                return Branch(obj, ins, bomb != null && runtime.Raycast(obj.Position + up, bomb.Position + up) == null);
            }
            case 167: // move_to_bomb: next to the bomb, on this side, a little aside
            {
                const float MaxAside = 6f;
                const float AboveBomb = 2.5f;
                var bomb = Bomb();
                if (bomb == null)
                {
                    break;
                }

                var away = new Vector2(obj.Position.X - bomb.Position.X, obj.Position.Y - bomb.Position.Y);
                away = away == Vector2.Zero ? away : Vector2.Normalize(away);
                var aside = new Vector2(away.Y, -away.X) * (rng.NextSingle() * 2f - 1f) * MathF.Min(F(o[0]), MaxAside);
                var spot = new Vector2(bomb.Position.X, bomb.Position.Y) + away * F(o[0]) + aside;
                StartMove(obj, 78, new Vector3(spot.X, spot.Y, bomb.Position.Z + AboveBomb + obj.HeightOffset));
                break;
            }
            case 177: // set_blast_range: blasts farther than this don't hurt the object
                obj.BlastRange = F(o[0]);
                break;

            // Variables and flags.
            case 65: // set_var
                Variables(obj, I(o[0]))[Math.Clamp(I(o[1]), 0, VariableCount - 1)] = F(o[2]);
                break;
            case 66: // add_var
                Variables(obj, I(o[0]))[Math.Clamp(I(o[1]), 0, VariableCount - 1)] += F(o[2]);
                break;
            case 216: // add_var_dt
                Variables(obj, I(o[0]))[Math.Clamp(I(o[1]), 0, VariableCount - 1)] += F(o[2]) * Dt;
                break;
            case 67: // if_var
                return Branch(obj, ins, Compare(Variables(obj, I(o[0]))[Math.Clamp(I(o[1]), 0, VariableCount - 1)], L(o[2])));
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
                return Branch(obj, ins, (obj.Flags & MdkObject.FlagCollected) != 0);
            case 116: // flags_set
                obj.Flags |= I(o[0]);
                break;
            case 117: // flags_clear
                obj.Flags &= ~I(o[0]);
                break;
            case 232: // if_option (a cheat toggled option, 1 by default)
                return Branch(obj, ins, I(o[0]) == runtime.Option);
            case 35: // set_flag148_4
                obj.Flags = I(o[0]) != 0 ? obj.Flags | MdkObject.FlagCollides : obj.Flags & ~MdkObject.FlagCollides;
                break;
            case 36: // set_flag148_2
                obj.Flags = I(o[0]) != 0 ? obj.Flags | MdkObject.FlagGravity : obj.Flags & ~MdkObject.FlagGravity;
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
                var count = CountOthers(obj, o[0], Counting.Commandable);
                return Branch(obj, ins, count == I(o[1]));
            }
            case 119: // if_count_alive
            {
                var count = CountOthers(obj, o[0], Counting.Alive);
                return Branch(obj, ins, Compare(count, L(o[1])));
            }
            case 74: // attach_to: the object named "<type>_<id>"
            {
                MdkObject? found = null;
                foreach (var other in runtime.Objects)
                {
                    if (IsArenaOther(obj, other) && IsNamed(other, o[2]))
                    {
                        found = other;
                    }
                }

                if (found != null)
                {
                    obj.Leader = found;
                    obj.AttachPoints = (I(o[0]), I(o[1]));
                    obj.MoveCommand = 74;
                }

                break;
            }
            case 175: // if_inventory: how many items of a type Kurt has
            {
                var count = 0;
                var slots = runtime.Kurt.Inventory.Slots;
                for (var i = 0; i < slots.Count; i++)
                {
                    count += (int)slots[i].Item == I(o[0]) ? slots[i].Count : 0;
                }

                return Branch(obj, ins, Compare(count, o[1], o[2], o[3] ?? BoxedZero));
            }
            case 174: // if_ammo: 0 the super chain gun's ticks, 1-5 sniper ammo
            {
                var inventory = runtime.Kurt.Inventory;
                var kind = I(o[0]);
                var amount = kind == 0 ? inventory.SuperChainGun : kind <= inventory.Ammo.Count ? inventory.Ammo[kind - 1] : 0;
                return Branch(obj, ins, Compare(amount, o[1], o[2], o[3] ?? BoxedZero));
            }
            case 29: // spawn_chain: links that hang off this object, nearest one first
            {
                const int ChainCommand = 30;
                var maxId = LastFollowerId(obj);
                for (var n = I(o[0]) - 1; n >= 0; n--)
                {
                    var link = runtime.Spawn(obj, (string)o[1]!, obj.Position, 0f, n + maxId + 1, I(o[2]), ScriptRuntime.Spawning.Plain);
                    if (link == null)
                    {
                        break;
                    }

                    link.MoveCommand = ChainCommand;
                    link.Leader = obj;
                }

                break;
            }
            case 159: // spawn_box: [[position mode, x, y, z], size x, y, z, texture, script]
            {
                var where = L(o[0]);
                var point = obj.Position;
                if (I(where[0]) == 1)
                {
                    var offset = Rotated(new Vector2(F(where[1]), F(where[2])), obj.Yaw);
                    point += new Vector3(offset.X, offset.Y, F(where[3]));
                }
                else if (I(where[0]) == 2)
                {
                    point = V(where, 1);
                }

                runtime.SpawnBox(obj, point, V(o, 1), (string)o[4]!, I(o[5]));
                break;
            }
            case 133: // arena_texture_frame: [texture, mode, value]; the levels only use mode 1 (frames per second)
                runtime.AnimatedTextures.Set(obj.Arena, (string)o[0]!, AnimatedTextures.ModeOf(I(o[1])), F(o[2]), Dt);
                break;
            case 131: // special_event: cutscenes and the end of the level
                runtime.SpecialEvent(obj, I(o[0]));
                break;
            case 85: // set_rolling
                obj.SetRolling(I(o[0]) != 0 ? MdkObject.Rolling.On : MdkObject.Rolling.Off);
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
                    runtime.ShatterPoint = V(o, 4);
                    runtime.ShatterDirection = V(o, 7);
                }
                else if (ins.Opcode == 139)
                {
                    runtime.ShatterPoint = V(o, 4);
                    runtime.ShatterDirection = Vector3.Zero;
                }

                if (I(o[0]) is >= 1 and <= 255)
                {
                    runtime.Debris.Shatter(obj.Arena, I(o[0]), F(o[1]), F(o[2]), F(o[3]), runtime.ShatterPoint, runtime.ShatterDirection);
                }

                if (runtime.ShatterDirection == Vector3.Zero)
                {
                    // A radial burst leaves the direction straight up for the next 137 (0x40c828).
                    runtime.ShatterDirection = Vector3.UnitZ;
                }

                break;
            case 128: // attach_effect: [name, point, second point]: a wound bleeding slime at a reference point
                // (only with the effects detail on); "OFF" with the same point twice stops it
                if ((string)o[0]! == "OFF" && I(o[1]) == I(o[2]))
                {
                    runtime.Effects.Detach(obj, I(o[1]));
                }
                else if (runtime.Option != 0)
                {
                    runtime.Effects.Attach(obj, I(o[1]), I(o[2]));
                }

                break;
            case 132: // spawn_effect: [chance, reference point or 255, point]: a bubble now and then
            {
                // (chance >= 150 would make sparks, which no level uses)
                const int SparkChance = 150;
                const int NoPoint = 255;
                if (I(o[0]) < SparkChance && rng.Next(100) < I(o[0]))
                {
                    var point = I(o[1]) != NoPoint ? obj.ReferencePoint(I(o[1])) : V(L(o[2]));
                    runtime.Effects.SpawnBubble(obj.Arena, point);
                }

                break;
            }
            case 136: // spawn_debris: [count, velocity, spread, absolute, point, size]: slime drops
            {
                const float Spread = 1f / 32768f;
                var count = runtime.Option != 0 ? I(o[0]) : I(o[0]) > 1 ? Math.Min(I(o[0]), 1) : 0;
                var origin = V(o, 6) + (I(o[5]) == 0 ? obj.Position : Vector3.Zero);
                for (var i = 0; i < count; i++)
                {
                    var velocity = new Vector3(F(o[1]) + (rng.Next(32768) - 0x4000) * F(o[4]) * Spread,
                        F(o[2]) + (rng.Next(32768) - 0x4000) * F(o[4]) * Spread, F(o[3]) + (rng.Next(32768) - 0x800) * F(o[4]) * Spread);
                    runtime.Effects.SpawnDrop(obj.Arena, origin, velocity, F(o[9]) + rng.Next(32768) * 0.0005f);
                }

                break;
            }
            case 219: // if_gun_aim: [target row y, z, action]
                return Branch(obj, ins, runtime.GunAim(obj, F(o[0]), F(o[1])));
            case 220: // place_x_near_player: [x min, x max, y limit]
                runtime.PlaceNearKurt(obj, F(o[0]), F(o[1]), F(o[2]));
                break;
            case 203: // camera_track: [[mode(, height)]] (nothing in sniper mode)
            {
                var track = L(o[0]);
                runtime.CameraTrack(obj, I(track[0]), track.Length > 1 ? F(track[1]) : 0f);
                break;
            }
            case 28: // bomb_follow_path: the sniper mortar round that just hit a triangle group follows a path
                runtime.SniperRounds.GuideLastMortar(I(o[0]));
                break;
            case 130: // special_130: a bullet hole on the texture where a sniper round hit
                runtime.StampBulletHole(obj);
                break;
            case 229: // turn_and_jump_to_dest: [turn rate, action while jumping]
                runtime.Motion.TurnAndJump(obj, F(o[0]));
                return Branch(obj, ins, obj.MoveCommand == 229);
            case 226: // jump_to: [pivot x, y, z, angular speed, gain], starts swinging below the pivot
                obj.SwingPivot = V(o);
                obj.SwingSpeed = F(o[3]);
                obj.SwingGain = F(o[4]);
                obj.SwingYaw = obj.Yaw;
                obj.SwingLength = obj.SwingPivot.Z - obj.Position.Z;
                obj.Flags |= MdkObject.FlagSwinging;
                break;
            case 242: // set_2d0_block: [[count, 4 points]]: lines from reference points 1-4 to the points
            {
                const int AllLines = 0xFF;
                var block = L(o[0]);
                if (I(block[0]) == 0)
                {
                    obj.RopeMask = 0;
                    break;
                }

                obj.RopeColor = 1;
                obj.RopeMask = AllLines;
                for (var i = 0; i < obj.RopePoints.Length; i++)
                {
                    obj.RopePoints[i] = V(block, 1 + i * 3);
                }

                break;
            }
            case 224: // wind_zone: [enable] or [enable, yaw, speed]
            {
                var wind = L(o[0]);
                runtime.WindZone(obj.Arena, I(wind[0]) != 0 ? ScriptRuntime.Wind.On : ScriptRuntime.Wind.Off, wind.Length > 2 ? F(wind[1]) : 0f, wind.Length > 2 ? F(wind[2]) : 0f);
                break;
            }

            // Fans (updrafts).
            case 142: // fan_create: [hotspot, name, param, type, strength]
                runtime.Fans.Create(obj.Arena, I(o[0]), (string)o[1]!, I(o[2]), I(o[3]), F(o[4]));
                break;
            case 143: // fan_remove
                runtime.Fans.Remove(obj.Arena, (string)o[0]!);
                break;
            case 190: // fan_enable: [enable, name]
                runtime.Fans.Enable(obj.Arena, (string)o[1]!, I(o[0]) != 0 ? Fans.Power.On : Fans.Power.Off);
                break;

            // The HUD.
            case 247: // hud_message: [flags, text name, seconds]
                runtime.Messages?.Push((string)o[1]!, I(o[0]), F(o[2]));
                break;
            case 181: // boss_bar: the health bar shows a counter (mode 0) or an arena variable (mode 1)
                BossBar(obj, L(o[0]));
                break;

            // Triangle groups of the arena.
            case 98: // group_set_state
                runtime.SetGroupState(obj.Arena, I(o[0]), I(o[1]));
                break;
            case 140: // group_set_texture
                runtime.SetGroupTexture(obj.Arena, I(o[0]), I(o[1]));
                break;
            case 99: // group_on_hit
            {
                var state = runtime.GetArenaState(obj.Arena);
                state.GroupHitMasks[GroupIndex(o[1])] = I(o[0]);
                state.GroupHitScripts[GroupIndex(o[1])] = I(o[2]);
                break;
            }
            case 168: // group_set_hit_flags: 0x80 = destructible (hidden until hit)
                runtime.GetArenaState(obj.Arena).GroupHitFlags[GroupIndex(o[0])] = I(o[1]);
                if ((I(o[1]) & ScriptRuntime.GroupDestructible) != 0)
                {
                    runtime.SetGroupState(obj.Arena, I(o[0]), ScriptRuntime.GroupHide);
                }

                break;
            case 162: // arena_counter_set
                runtime.GetArenaState(obj.Arena).GroupCounters[GroupIndex(o[0])] = I(o[1]);
                break;
            case 163: // if_arena_counter
                return Branch(obj, ins, Compare(runtime.GetArenaState(obj.Arena).GroupCounters[GroupIndex(o[0])], L(o[1])));
            case 194: // group_state_near_player: groups around the one under Kurt get the opposite op, the others the op (0x453a1e)
            {
                var floorGroup = runtime.GetKurtFloorGroup();
                var first = I(o[1]);
                var last = I(o[2]);
                if (first <= 0 || floorGroup < first || floorGroup > last)
                {
                    break;
                }

                for (var group = first; group <= last; group++)
                {
                    var near = group >= floorGroup - I(o[3]) && group <= floorGroup + I(o[4]);
                    runtime.SetGroupState(runtime.CurrentArena, group, near ? I(o[0]) ^ 1 : I(o[0]));
                }

                break;
            }

            // Conditions about Kurt, the arena and chance.
            case 123: // if_not_in_player_arena
                return Branch(obj, ins, obj.Arena != runtime.CurrentArena);
            case 222: // if_opcode_count
                return Branch(obj, ins, Compare(_opcodeCount, L(o[0])));
            case 231: // if_move_idle_flag: the movement gave up (stuck, ObjectMotion.HandleStuck)
                return Branch(obj, ins, obj.MoveCommand == 0 && obj.StuckCount != 0);
            case 236: // if_no_floor_at: no up-facing face of its arena below a point (0x46046c)
            {
                // From 3 above the point down to 3 below it (or to z + depth). The offset turns like
                // add_vel_local's: −dx is ahead (the XG's −20 checks 20 units in front).
                const float ProbeTop = 3f;
                const float DefaultDrop = 6f;
                var (dx, dy, depth) = (F(o[0]), F(o[1]), F(o[2]));
                var (s, c) = MathF.SinCos(float.DegreesToRadians(obj.Yaw));
                var top = obj.Position + new Vector3(-dx * c - dy * s, -dy * c - dx * s, ProbeTop);
                var bottom = depth != 0f ? obj.Position.Z + depth : top.Z - DefaultDrop;
                return Branch(obj, ins, !runtime.CrossesFloor(obj.Arena, top, top with { Z = bottom }));
            }
            case 189: // lob_to_kurt: throw itself so that it lands on Kurt (or at a height)
                LobToKurt(obj, L(o[0]));
                break;
            case 46: // find_cover_spot
                runtime.FindCoverSpot(obj);
                return Branch(obj, ins, obj.MoveCommand == 43);
            case 197: // find_advance_spot
                runtime.FindAdvanceSpot(obj);
                return Branch(obj, ins, obj.MoveCommand == 197);
            case 221: // pick_waypoint8
                runtime.PickWaypoint8(obj, I(o[0]));
                return Branch(obj, ins, obj.MoveCommand == 221);
            case 209: // if_player_on_me_low: Kurt stands on it and fires
                // 0x458a59: his feet not above his floor on it (0x573a1c), which standing on it
                // already means; its model's top can be below them (LEVEL6's XBGUN: by 0.02).
                return Branch(obj, ins, runtime.GetKurtPlatform() == obj && runtime.Kurt.Firing);
            case 164: // path_speed_by_kurt: [enable, [distance, far, middle, near speeds]]
                if (I(o[0]) != 0)
                {
                    obj.PathSpeeds = L(o[1]).Select(F).ToArray();
                    obj.PathSpeed = obj.PathSpeeds[2];
                    obj.Flags |= MdkObject.FlagPathSpeedByKurt;
                }
                else
                {
                    obj.Flags &= ~MdkObject.FlagPathSpeedByKurt;
                }

                break;
            case 173: // teleport_player_keep: as teleport_player, from Kurt's place and yaw (0x454556)
            {
                // [arena] keeps them; ["", arena, dx, dy, dz, dyaw] adds an offset (LEVEL4's
                // CMEAT_6 lifts Kurt 1971 up into MEAT_7).
                var keep = L(o[0]);
                var name = (string)keep[0]!;
                var shifted = name.Length == 0;
                var offset = shifted ? V(keep, 2) : Vector3.Zero;
                var turn = shifted ? F(keep[5]) : 0f;
                runtime.TeleportKurt(shifted ? (string)keep[1]! : name, runtime.KurtPosition + offset, runtime.KurtYaw + turn);
                obj.Restart = 0;
                ClearStack(obj);
                return Yield;
            }
            case 103: // if_kurt_in_box: each axis between its min and max (corners given backwards never hold)
            {
                var kurt = runtime.KurtPosition;
                var inside = true;
                for (var axis = 0; axis < 3; axis++)
                {
                    inside = inside && kurt[axis] >= F(o[axis]) && kurt[axis] <= F(o[axis + 3]);
                }

                return Branch(obj, ins, inside);
            }
            case 115: // if_wall
            {
                const float WalkerHeight = 2f;
                var from = obj.Position + new Vector3(0f, 0f, (obj.Flags & MdkObject.FlagCollides) != 0 ? WalkerHeight : 0f);
                var direction = FromAngle(obj.Yaw + F(o[0])) * F(o[1]);
                return Branch(obj, ins, runtime.Raycast(from, from + new Vector3(direction.X, direction.Y, 0f)) != null);
            }
            case 121: // if_floor_below
                return Branch(obj, ins, runtime.Raycast(obj.Position, obj.Position - new Vector3(0f, 0f, F(o[0]))) != null);
            case 96: // if_kurt_in_rect
            {
                var kurt = runtime.KurtPosition;
                return Branch(obj, ins, kurt.X >= F(o[0]) && kurt.X <= F(o[2]) && kurt.Y >= F(o[1]) && kurt.Y <= F(o[3]));
            }
            case 14: // if_sees_kurt
            case 57: // if_not_sees_kurt
            {
                var sees = runtime.CanSeeKurt(obj, F(o[0]), F(o[1]));
                return Branch(obj, ins, ins.Opcode == 14 ? sees : !sees);
            }
            case 45: // if_target_dist
                return Branch(obj, ins, Compare(obj.DistanceTo(runtime.TargetPosition), L(o[0])));
            case 47: // if_chance
                return Branch(obj, ins, rng.Next(10000) < F(o[0]) * 100f);
            case 48: // if_chance_per_second
            {
                const float MinPeriod = 0.001f;
                return Branch(obj, ins, rng.Next(10000) < Dt / MathF.Max(F(o[0]), MinPeriod) * 10000f);
            }

            // Sounds.
            case 89: // play_sound
                runtime.PlaySound(obj, (string)o[2]!, I(o[0]), o[1]);
                break;
            case 107: // set_loop_sound
                runtime.SetLoopSound(obj, (string)o[0]!);
                break;
            case 249: // if_own_sound: mode 0 playing, 1 playing this sound, other: not playing
            {
                var playing = runtime.Mixer.IsVoicePlaying(obj.TrackedVoice);
                var condition = I(o[0]) switch
                {
                    0 => playing,
                    1 => playing && Is(obj.TrackedSound, o[2]),
                    _ => !playing,
                };
                return Branch(obj, ins, condition);
            }

            // Animation speed.
            case 58: // anim_fps
                obj.AnimationRate = Value(obj, o[0]);
                break;
            case 186: // anim_fps_from_speed: the animation keeps up with the walking speed
                obj.AnimationRate = F(o[1]) != 0f ? obj.Speed * ScriptRuntime.TicksPerSecond / F(o[1]) : MdkObject.AnimationFps;
                break;
            case 154: // wait_anim_frame: runs again every frame until the animation reaches the frame
            {
                const int HoldFrame = -1;
                obj.Restart = ins.Pc;
                var frame = I(o[0]);
                var reached = obj.IsAnimationDone || (frame >= 0 && obj.AnimationFrame >= frame)
                    || (frame == HoldFrame && (obj.AnimationEndFrame < 0 || obj.AnimationFrame == obj.AnimationEndFrame));
                if (!reached)
                {
                    return Yield;
                }

                break;
            }

            // More conditions.
            case 127: // if_health
                return Branch(obj, ins, Compare(obj.Health, L(o[0])));
            case 185: // if_speed
                return Branch(obj, ins, Compare(obj.Speed, L(o[0])));
            case 160: // if_yaw
                return Branch(obj, ins, Compare(Wrap360(obj.Yaw), L(o[0])));
            case 241: // if_player_health
                return Branch(obj, ins, Compare(runtime.Kurt.Health, L(o[0])));
            case 238: // if_self_pos: 0 x, 1 y, other z
                return Branch(obj, ins, Compare(obj.Position[Math.Min(I(o[0]), 2)], L(o[1])));
            case 237: // if_self_in_box
            {
                var p = obj.Position;
                return Branch(obj, ins, p.X >= F(o[0]) && p.Y >= F(o[1]) && p.Z >= F(o[2]) && p.X <= F(o[3]) && p.Y <= F(o[4]) && p.Z <= F(o[5]));
            }
            case 191: // if_kurt_delta: Kurt's offset along an axis, absolute unless +0x80
            {
                const int Signed = 0x80;
                var delta = (runtime.KurtPosition - obj.Position)[Math.Min(I(o[0]) & 0x7F, 2)];
                if ((I(o[0]) & Signed) == 0)
                {
                    delta = MathF.Abs(delta);
                }

                return Branch(obj, ins, Compare(delta, L(o[1])));
            }
            case 188: // if_target_dist2d
                return Branch(obj, ins, Compare(Distance2D(runtime.TargetPosition, obj.Position), L(o[0])));
            case 234: // if_target_angle_signed
                return Branch(obj, ins, Compare(WrapAngle(obj.Yaw - obj.YawTo(runtime.TargetPosition)), L(o[0])));
            case 87: // if_kurt_looks_at_me
                return Branch(obj, ins, runtime.KurtLooksAt(obj, F(o[0]), F(o[1]), F(o[2])));
            case 114: // if_player_on_me
                return Branch(obj, ins, runtime.GetKurtPlatform() == obj);
            case 250: // if_in_box: an object of a type inside a box (Kurt's projectiles aren't done yet)
                return Branch(obj, ins, InBox(obj, o));
            case 33: // if_path_frame
                return Branch(obj, ins, obj.Path == 0 || Round(obj.PathTime) >= I(o[0]) - 1);
            case 34: // if_no_leader (the movement command is cleared when true)
            {
                var alone = obj.Leader is null or { Dead: true };
                var next = Branch(obj, ins, alone);
                if (alone)
                {
                    obj.MoveCommand = 0;
                }

                return next;
            }

            // Partners (obj+0x2b8).
            case 245: // find_object
            {
                var args = L(o[0]);
                obj.Linked = FindObject(obj, (string)args[^1]!, args.Length == 2 ? I(args[0]) : 0);
                break;
            }
            case 213: // if_partner_distance (0 without a partner, at least 1)
            {
                var distance = obj.Linked is { Dead: false } partner ? MathF.Max(obj.DistanceTo(partner.Position), 1f) : 0f;
                return Branch(obj, ins, Compare(distance, L(o[0])));
            }
            case 214: // if_partner_angle
            {
                var angle = obj.Linked is { Dead: false } partner ? WrapAngle(obj.Yaw - obj.YawTo(partner.Position)) : 0f;
                return Branch(obj, ins, Compare(angle, L(o[0])));
            }
            case 179: // spawn_partner
            {
                var partner = runtime.Spawn(obj, (string)o[0]!, obj.Position, 0f, -1, I(o[1]), ScriptRuntime.Spawning.Plain);
                if (partner != null)
                {
                    partner.Linked = obj;
                    obj.Linked = partner;
                }

                break;
            }
            case 180: // snap_to_partner
            {
                if (obj.Linked is not { Dead: false } partner)
                {
                    break;
                }

                obj.Position = partner.Position;
                var offset = L(o[0]);
                if (I(offset[0]) != 0)
                {
                    obj.Position += V(offset, 1);
                }

                break;
            }
            case 228: // link_partners: pairs up unlinked objects of a type closer than 100
                if (o[0] is string type)
                {
                    LinkPartners(obj, type);
                }
                else if (I(L(o[0])[1]) == 0)
                {
                    obj.Linked = null;
                }

                break;
            case 246: // turn_to_linked (bit 7: the pitch too)
            {
                const int Turns = 1;
                const int AlsoPitch = 0x80;
                if ((I(o[0]) & Turns) == 0 || obj.Linked is not { Dead: false } partner)
                {
                    break;
                }

                var step = F(o[1]) * Dt;
                var diff = WrapAngle(obj.YawTo(partner.Position) - obj.Yaw);
                obj.Yaw = Wrap360(obj.Yaw + Math.Clamp(diff, -step, step));
                if ((I(o[0]) & AlsoPitch) != 0)
                {
                    var d = partner.Position - obj.Position;
                    var targetPitch = float.RadiansToDegrees(MathF.Atan2(d.Z + obj.HeightOffset, new Vector2(d.X, d.Y).Length()));
                    obj.Pitch += Math.Clamp(WrapAngle(targetPitch - obj.Pitch), -step, step);
                }

                break;
            }

            // Parts, hits and damage.
            case 208: // if_part_hit: the part is hidden (blown off or destroyed)
            {
                var part = obj.FindPart((string)o[0]!);
                var hit = part >= 0 && (obj.HiddenParts & (1 << part)) != 0;
                if (hit)
                {
                    obj.HitEvent = 0;
                }

                return Branch(obj, ins, hit);
            }
            case 32: // clear_parts_mask: shows parts again, except blown off ones
                obj.HiddenParts = PartsMask(obj, L(o[0]), obj.HiddenParts, Masking.Show);
                break;
            case 195: // if_hit_weapon
                return Branch(obj, ins, obj.HitType == I(o[0]));
            case 158: // touch_damage: stops the script when the object died of it
            {
                const int GoesOnHit = 2;
                var hit = runtime.TouchDamage(obj, I(o[0]), I(o[1]), I(o[2]));
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
                const int AllTargets = 7;
                const int BlastHitType = -5;
                obj.Flags |= MdkObject.FlagNotTarget;
                if (F(o[0]) != 0f && F(o[1]) != 0f)
                {
                    runtime.Items.Blast(obj.Position, Round(F(o[0])), F(o[1]), AllTargets, BlastHitType, null, Items.Kills.Ignored);
                }

                runtime.Kill(obj);
                return Yield;
            }
            case 172: // explosion
            {
                var point = Point(obj, L(o[0]));
                runtime.PlaySoundAt(ScriptRuntime.ExplodeSound, point);
                runtime.SpawnExplosion(obj.Arena, point, F(o[1]));
                break;
            }
            case 178: // explosion_damage: flags are the targets (1 Kurt, 2 objects, 4 triangle groups)
            {
                const int BlastHitType = -5;
                var point = Point(obj, L(o[0]));
                runtime.PlaySoundAt(ScriptRuntime.ExplodeSound, point);
                runtime.SpawnExplosion(obj.Arena, point, F(o[1]));
                runtime.Items.Blast(point, Round(F(o[2])), F(o[3]), I(o[4]), BlastHitType, null);
                break;
            }
            case 156: // spawn_at_point
                runtime.Spawn(obj, (string)o[1]!, obj.ReferencePoint(I(o[0])), 0f, -1, I(o[2]), ScriptRuntime.Spawning.Plain);
                break;
            case 205: // set_21f: indestructible
                obj.Indestructible = I(o[0]) != 0;
                break;

            // Kurt and the screen.
            case 135: // raise_573aa8: shakes the screen
                runtime.RaiseShake(F(o[0]));
                break;
            case 215: // screen_flash
                runtime.Kurt.WhiteFlash = MathF.Max(runtime.Kurt.WhiteFlash, MathF.Round(Value(obj, o[0])));
                break;
            case 240: // set_hurt_flash: Kurt's knock-down counter (1 means 5), unless he's invulnerable
            {
                const float DefaultKnock = 5f;
                if (runtime.Kurt.Invulnerable <= 0f)
                {
                    runtime.Kurt.KnockDamage = I(o[0]) == 1 ? DefaultKnock : F(o[0]);
                }

                break;
            }
            case 244: // set_global_573bd4: Kurt's invulnerability time
                runtime.Kurt.Invulnerable = I(o[0]) == 0 ? F(o[1]) : runtime.Kurt.Invulnerable + F(o[1]);
                break;
            case 248: // push_kurt: knocks Kurt down, pushed away
                runtime.PushKurt(obj, L(o[0]));
                break;
            case 170: // inherit_kurt_vel
                obj.Velocity += runtime.Kurt.Velocity * F(o[0]);
                break;
            case 217: // global_573c4c_add (a count shown after the level)
                if (I(o[0]) == 1)
                {
                    runtime.Stats.HeadShots += I(o[1]);
                }

                break;
            case 202: // set_574304: how the sky is drawn (0 normally)
                runtime.SkyMode = I(o[0]);
                break;

            // More movement.
            case 63: // set_flag148_10_inv: Kurt goes through the object (0x10)
                obj.Flags = I(o[0]) != 0 ? obj.Flags & ~MdkObject.FlagNotSolid : obj.Flags | MdkObject.FlagNotSolid;
                break;
            case 41: // set_targetable: flag 0x100 (a platform), and 0x800000 with mode 2
            {
                const int Platform = 0x100;
                const int StandableMode = 2;
                obj.Flags = I(o[0]) != 0 ? obj.Flags | Platform : obj.Flags & ~Platform;
                obj.Flags = I(o[0]) == StandableMode ? obj.Flags | MdkObject.FlagStandable : obj.Flags & ~MdkObject.FlagStandable;
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
                var kick = FromAngle(rng.NextSingle() * FullTurn) * F(o[0]) * (0.5f + rng.NextSingle());
                obj.Velocity += new Vector3(kick.X, kick.Y, F(o[1]) * (0.5f + rng.NextSingle()));
                break;
            }
            case 126: // aim_pitch_target
            {
                const float AimHeight = 3f;
                var d = runtime.TargetPosition - obj.Position;
                obj.Pitch = float.RadiansToDegrees(MathF.Atan2(d.Z + AimHeight, new Vector2(d.X, d.Y).Length()));
                break;
            }
            case 218: // set_f13c: the pitch
                obj.Pitch = F(o[0]);
                break;
            case 157: // vel_away_from (a negative speed moves towards it)
            {
                var other = FirstOther(obj, o[0], I(o[1]));
                var away = other == null ? Vector3.Zero : obj.Position - other.Position;
                if (away != Vector3.Zero)
                {
                    obj.Velocity = Vector3.Normalize(away) * F(o[2]);
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
                var index = (int)Variables(obj, I(o[0]))[Math.Clamp(I(o[1]), 0, VariableCount - 1)];
                var table = L(o[2]);
                if (index >= 0 && index < table.Length)
                {
                    return Gosub(obj, ins.Next, I(L(table[index])[0]));
                }

                break;
            }
            case 251: // set_target_mode: 1 the aliens' target, 2 always targets Kurt
                obj.TargetMode = I(o[0]);
                if (I(o[0]) == 1)
                {
                    runtime.AlienTarget = obj;
                }
                else if (I(o[0]) == 0 && runtime.AlienTarget == obj)
                {
                    runtime.AlienTarget = null;
                }

                break;
            case 27: // if_alarm: an object sounds the alarm (movement command 15)
                return Branch(obj, ins, runtime.AlarmTicks > 0);
            case 233: // if_sound_playing
                return Branch(obj, ins, runtime.IsSoundPlaying((string)o[0]!));
            case 192: // if_no_flat_floor_at: no floor below a point in front, or a steep one
            {
                var offset = Rotated(new Vector2(F(o[0]), F(o[1])), obj.Yaw);
                var point = obj.Position + new Vector3(offset.X, offset.Y, 1f);
                var hit = runtime.Raycast(point, point - new Vector3(0f, 0f, F(o[2]) + 1f));
                return Branch(obj, ins, hit is not { } floor || MathF.Abs(floor.Normal.Z) < F(o[3]));
            }
            case 243: // if_point_sees_player
            {
                var point = obj.ReferencePoint(I(o[0]));
                var chest = runtime.KurtPosition + new Vector3(0f, 0f, 4f);
                return Branch(obj, ins, Vector3.Distance(point, runtime.KurtPosition) <= F(o[1]) && runtime.Raycast(point, chest) == null);
            }
            case 13: // if_cheat_key (cheat keys aren't done)
                return Branch(obj, ins, false);
            case 171: // if_is_573c30: the object Kurt rides
                return Branch(obj, ins, runtime.Rides.Ridden == obj);
            case 77: // debug_msg
                break;
            case 204: // avoid_objects: turns away from an object it overlaps
            {
                var box = runtime.GetWorldBounds(obj);
                foreach (var other in runtime.GetArenaObjects(obj))
                {
                    if ((other.Flags & (MdkObject.FlagNotSolid | MdkObject.FlagNotSolid2)) != 0 || !box.Intersects(runtime.GetWorldBounds(other)))
                    {
                        continue;
                    }

                    var away = WrapAngle(obj.YawTo(other.Position) + HalfTurn - obj.Yaw);
                    obj.Yaw = Wrap360(obj.Yaw + MathF.Sign(away) * MathF.Min(MathF.Abs(away), HalfTurn * Dt));
                    break;
                }

                break;
            }
            case 49: // release_children: lets go of the last followers of a chain
                for (var i = 0; i < I(o[0]); i++)
                {
                    var last = LastChainLink(obj);
                    if (last == null)
                    {
                        return Branch(obj, ins, true);
                    }

                    last.Leader = null;
                    last.MoveCommand = 0;
                }

                break;
            case 129: // blow_off_parts: the parts go (the debris isn't drawn yet) and stay hidden; mode 2 only
            {
                // with gore (0x45c498)
                const int GoreMode = 2;
                var mode = I(o[0]);
                if (mode is < 0 or > GoreMode || obj.Model == null || (mode == GoreMode && runtime.Option == 0))
                {
                    break;
                }

                var mask = 0;
                foreach (var name in L(o[1]))
                {
                    var part = obj.FindPart((string)name!);
                    if (part >= 0)
                    {
                        mask |= 1 << part;
                    }
                }

                obj.LockedParts |= mask;
                obj.HiddenParts |= mask;
                break;
            }
            case 112: // teleport_player
                runtime.TeleportKurt((string)o[0]!, V(o, 1), F(o[4]));
                if (((string)o[0]!).Length != 0)
                {
                    obj.Restart = 0;
                    ClearStack(obj);
                    return Yield;
                }

                break;
            case 223: // arena_set_neighbour
                runtime.PreloadArena((string)o[0]!);
                break;
            case 239: // set_27c (not identified)
                break;

            // The 1996 demo's own opcodes (ScriptDecoder.Beta.cs).
            case BetaOpcodes.FollowPath:
                FollowBetaPath(obj, I(o[0]));
                break;
            case BetaOpcodes.Fire:
                FireBolt(obj, o);
                break;
            case BetaOpcodes.IfAlarmEnded:
                return Branch(obj, ins, runtime.AlarmEndedTicks > 0);
            case BetaOpcodes.IfField108:
                return Branch(obj, ins, false);
            case BetaOpcodes.Nothing:
                break;
            default:
                Unimplemented[ins.Opcode] = Unimplemented.GetValueOrDefault(ins.Opcode) + 1;
                if (ins.Action != null)
                {
                    return Branch(obj, ins, false);
                }

                break;
        }

        return ins.Next;
    }

    /// <summary>Whether a part mask operation hides the named parts or shows them again.</summary>
    private enum Masking { Hide, Show }

    /// <summary>set_parts_mask / clear_parts_mask: the parts named in the list ("ALL" = every part);
    /// blown off parts stay hidden.</summary>
    private static int PartsMask(MdkObject obj, object?[] entries, int mask, Masking masking)
    {
        const string All = "ALL";
        if (obj.Model == null)
        {
            return mask;
        }

        foreach (var entry in entries)
        {
            var name = (string)L(entry)[0]!;
            for (var i = 0; i < obj.Model.PartList.Count; i++)
            {
                if (!Is(All, name) && !Is(obj.Model.PartList[i].Name, name))
                {
                    continue;
                }

                if (masking == Masking.Hide)
                {
                    mask |= 1 << i;
                }
                else if ((obj.LockedParts & (1 << i)) == 0)
                {
                    mask &= ~(1 << i);
                }
            }
        }

        return mask;
    }

    /// <summary>A triangle group number (1-16) as an index.</summary>
    private static int GroupIndex(object? group) => (I(group) - 1) & (ScriptRuntime.GroupCount - 1);

    /// <summary>boss_bar: mode 0 max - a group counter; mode 1 an arena variable going from high (0)
    /// to low (max). Only while Kurt fires.</summary>
    private void BossBar(MdkObject obj, object?[] source)
    {
        var state = runtime.GetArenaState(obj.Arena);
        var maxHealth = 0;
        var value = 0;
        switch (I(source[0]))
        {
            case 0:
                maxHealth = I(source[2]);
                value = maxHealth - state.GroupCounters[GroupIndex(source[1])];
                break;
            case 1:
                maxHealth = I(source[4]);
                var index = I(source[1]) <= VariableCount - 1 ? I(source[1]) : 0;
                var high = F(source[3]);
                value = Round((high - state.Variables[index]) / (high - F(source[2])) * maxHealth);
                break;
        }

        if (maxHealth > 0 && runtime.Kurt.Firing)
        {
            runtime.ShowBarValues(Math.Min(value, maxHealth), maxHealth);
        }
    }

    /// <summary>lob_to_kurt: throws the object so it lands on Kurt (mode 1) or at a height (mode 0).</summary>
    private void LobToKurt(MdkObject obj, object?[] args)
    {
        if (I(args[0]) > 1)
        {
            return;
        }

        var targetZ = I(args[0]) == 0 ? F(args[2]) : runtime.KurtPosition.Z;
        var fall = targetZ - obj.Position.Z;

        // The fall time of 0.5 × gravity × t² = fall with the current vertical speed.
        var a = obj.Gravity * -0.5f;
        var discriminant = obj.Velocity.Z * obj.Velocity.Z + 4f * a * fall;
        if (fall > 0f || discriminant < 0f)
        {
            return;
        }

        var root = MathF.Sqrt(discriminant);
        var time = MathF.Max((root - obj.Velocity.Z) / (a * 2f), (-obj.Velocity.Z - root) / (a * 2f));
        var toKurt = new Vector2(runtime.KurtPosition.X - obj.Position.X, runtime.KurtPosition.Y - obj.Position.Y);
        var distance = toKurt.Length();
        if (time <= 0f || distance <= 0f)
        {
            return;
        }

        // The friction over the flight has to be made up for.
        var speed = MathF.Min(obj.Friction * 0.5f * time + distance / time, F(args[1]));
        var velocity = toKurt * (speed / distance);
        obj.Velocity.X = velocity.X;
        obj.Velocity.Y = velocity.Y;
    }

    /// <summary>if_in_box: an object of a type (o[0] = 0xFF) inside a box (mode 2: XY [x0, y0, x1, y1];
    /// else [x0, y0, z0, x1, y1, z1], Z tested with mode 3).</summary>
    /// <summary>A box operand's values: min x, y, z, max x, y, z (4 for a flat one).</summary>
    private const int BoxValues = 6;

    private bool InBox(MdkObject obj, object?[] o)
    {
        const int ByType = 0xFF;
        const int Flat = 2;
        const int Solid = 3;
        if (I(o[0]) != ByType)
        {
            return false;
        }

        Span<float> box = stackalloc float[BoxValues];
        var values = L(o[3]);
        for (var i = 0; i < Math.Min(values.Length, BoxValues); i++)
        {
            box[i] = F(values[i]);
        }

        var mode = I(o[2]);
        foreach (var other in runtime.Objects)
        {
            if (other.Dead || other.Arena != obj.Arena || !Is(other.TypeName, o[1]))
            {
                continue;
            }

            var p = other.Position;
            var inside = mode == Flat
                ? p.X >= box[0] && p.Y >= box[1] && p.X <= box[2] && p.Y <= box[3]
                : p.X >= box[0] && p.Y >= box[1] && p.X <= box[3] && p.Y <= box[4] && (mode != Solid || (p.Z >= box[2] && p.Z <= box[5]));
            if (inside)
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>An object of the arena (not obj) of a type (find_object): mode 0 a random one, 1 the
    /// nearest, 2 the nearest of Kurt's items (0x1000) within 200, at most 9 units higher and in sight.</summary>
    private MdkObject? FindObject(MdkObject obj, string type, int mode)
    {
        const int ItemsMode = 2;
        const float ItemRange = 200f;
        const float ItemAbove = 9f;
        var found = _found;
        found.Clear();
        MdkObject? best = null;
        var bestDistance = float.PositiveInfinity;
        foreach (var other in runtime.GetArenaObjects(obj))
        {
            if (!Is(other.TypeName, type))
            {
                continue;
            }

            var distance = obj.DistanceTo(other.Position);
            if (mode == ItemsMode && (distance > ItemRange || (other.Flags & Items.FlagThrown) == 0 || other.Position.Z > obj.Position.Z + ItemAbove
                || runtime.Raycast(obj.Position + new Vector3(0f, 0f, 5f), other.Position + new Vector3(0f, 0f, 2f)) != null))
            {
                continue;
            }

            found.Add(other);
            if (distance < bestDistance)
            {
                best = other;
                bestDistance = distance;
            }
        }

        if (found.Count == 0)
        {
            return null;
        }

        return mode == 0 ? found[runtime.Rng.Next(found.Count)] : best;
    }

    /// <summary>Pairs up the objects of a type without a partner that are closer than 100 units (link_partners).</summary>
    private void LinkPartners(MdkObject obj, string type)
    {
        const float RangeSquared = 10000f;
        var loose = runtime.Objects
            .Where(other => !other.Dead && other.Arena == obj.Arena && Is(other.TypeName, type) && other.Linked is null or { Dead: true })
            .ToList();
        for (var i = 0; i < loose.Count; i++)
        {
            if (loose[i].Linked != null)
            {
                continue;
            }

            for (var j = i + 1; j < loose.Count; j++)
            {
                if (loose[j].Linked != null || Vector3.DistanceSquared(loose[i].Position, loose[j].Position) >= RangeSquared)
                {
                    continue;
                }

                loose[i].Linked = loose[j];
                loose[j].Linked = loose[i];
                break;
            }
        }
    }

    /// <summary>A position operand (op172_position): mode 3 a reference point of the object, 1 relative
    /// to the object, 2 turned by its yaw, other modes absolute.</summary>
    private static Vector3 Point(MdkObject obj, object?[] operand) => I(operand[0]) switch
    {
        3 => obj.ReferencePoint(I(operand[1])),
        1 => obj.Position + V(operand, 1),
        2 => obj.Position + RotatedZ(V(operand, 1), obj.Yaw),
        _ => V(operand, 1),
    };

    private void StartMove(MdkObject obj, int command, Vector3 destination)
    {
        obj.MoveCommand = command;
        obj.MoveDestination = destination;
        obj.Path = 0;
        runtime.Motion.PlanMove(obj);
    }

    /// <summary>follow_path: operands [path, flags1, flags2, start frame, relative, origin].</summary>
    private void FollowPath(MdkObject obj, object?[] o)
    {
        const int Flag200 = 0x200;
        var path = I(o[0]);
        if (path == 0)
        {
            obj.Path = 0;
            return;
        }

        var motion = runtime.Motion;
        obj.Flags = (I(o[1]) & 1) != 0 ? obj.Flags | Flag200 : obj.Flags & ~Flag200;
        obj.Flags = (I(o[1]) & 2) != 0 ? obj.Flags | MdkObject.FlagPathPushes : obj.Flags & ~MdkObject.FlagPathPushes;
        obj.Flags = (I(o[2]) & 1) != 0 ? obj.Flags | MdkObject.FlagPathOnce : obj.Flags & ~MdkObject.FlagPathOnce;
        float time = I(o[3]);
        if ((I(o[2]) & 2) != 0)
        {
            // Backwards: start frame 0 means the end of the path.
            obj.PathSpeed = -1f;
            if (I(o[3]) == 0)
            {
                time = motion.PathKeyFrame(path, motion.PathKeyCount(path) - 1) - 1;
            }
        }

        var origin = I(o[4]) != 0 ? obj.Position - motion.PathPosition(path, time) : V(L(o[5]));
        motion.StartPath(obj, path, time, origin);
    }

    /// <summary>The 1996 demo's <c>follow_path</c> (0x47f6c): a path stopped by opcode 21 goes on from
    /// where it stopped, another one starts at its first frame. The object turns along the path,
    /// whose positions are absolute, and goes round it again and again (0x32ff8).</summary>
    private void FollowBetaPath(MdkObject obj, int path)
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

        obj.Flags &= ~(MdkObject.FlagPathPushes | MdkObject.FlagPathOnce | MdkObject.FlagNoTurning);
        runtime.Motion.StartPath(obj, path, 0f, Vector3.Zero);
    }

    /// <summary>The 1996 demo's <c>fire</c>: <c>[origin, aim, range, accuracy, ?]</c>. A bolt leaves a
    /// reference point or a part, aimed at the target when <c>aim</c> is set (with aim_target's
    /// error), else along the object's yaw; it has no script, flies <c>range</c> units and hurts Kurt
    /// when it touches him.</summary>
    private void FireBolt(MdkObject obj, object?[] o)
    {
        if (runtime.Fire(obj, L(o[0]), BetaBolt, 0) is not { } bolt)
        {
            return;
        }

        if (I(o[1]) != 0)
        {
            Aim(bolt, F(o[3]), Aiming.Always);
        }

        bolt.Speed = BetaBoltSpeed;
        bolt.Parameter = F(o[2]) / BetaBoltSpeed;
        bolt.TouchDamage = BetaBoltDamage;
    }

    /// <summary>Whether aim_target turns only when the target is more than 2 units away across.</summary>
    private enum Aiming { CheckDistance, Always }

    /// <summary>Aims the yaw and pitch at the target (aim_target, aim_target_inaccurate), with a
    /// random error unless the accuracy is 100.</summary>
    private void Aim(MdkObject obj, float accuracy, Aiming aiming)
    {
        const float Perfect = 100f;
        const float AimHeight = 3f;
        const float MinAcross = 2f;
        var target = runtime.TargetPosition;
        var p = obj.Position;
        var horizontal = Distance2D(target, p);
        if (aiming == Aiming.Always || horizontal > MinAcross)
        {
            obj.Yaw = obj.YawTo(target);
        }

        obj.Pitch = float.RadiansToDegrees(MathF.Atan2(target.Z + AimHeight - p.Z, horizontal));
        if (accuracy == Perfect)
        {
            return;
        }

        var distance = obj.DistanceTo(target) + 0.1f;
        obj.Yaw = Wrap360(obj.Yaw + (runtime.Rng.Next(20) - 10) * (Perfect - accuracy) / distance);
        obj.Pitch += (runtime.Rng.Next(20) - 10) * (Perfect - accuracy) / (distance * PitchErrorScale);
    }

    /// <summary>Turns towards a point offset from the target in the target's frame
    /// (turn_to_target_offset, and with a z offset the pitch too: turn_to_target_offset3d).</summary>
    private void TurnToTarget(MdkObject obj, float rate, float accuracy, float a, float b, float? zOffset)
    {
        const float Perfect = 100f;
        var target = runtime.TargetPosition;
        var (s, c) = MathF.SinCos(float.DegreesToRadians(runtime.TargetYaw));
        var point = new Vector3(target.X - (a * c + b * s), target.Y - (b * c + a * s), target.Z);
        var horizontal = Distance2D(point, obj.Position);
        var yaw = obj.YawTo(point);
        if (accuracy != Perfect)
        {
            yaw += (runtime.Rng.Next(20) - 10) * (Perfect - accuracy) / (1f + horizontal);
        }

        var maxStep = rate * Dt;
        obj.Yaw = Wrap360(obj.Yaw + Math.Clamp(WrapAngle(yaw - obj.Yaw), -maxStep, maxStep));
        if (zOffset is not { } z)
        {
            return;
        }

        point.Z = target.Z + z;
        var pitch = float.RadiansToDegrees(MathF.Atan2(point.Z - obj.Position.Z + obj.HeightOffset, horizontal));
        if (accuracy != Perfect)
        {
            pitch += (runtime.Rng.Next(20) - 10) * (Perfect - accuracy) / (1f + horizontal * PitchErrorScale);
        }

        obj.Pitch += Math.Clamp(WrapAngle(pitch - obj.Pitch), -maxStep, maxStep);
    }

    /// <summary>command_objects: operands [command, command args, selector, selector args].</summary>
    private void CommandObjects(MdkObject obj, object?[] o)
    {
        const int GotoCommand = 7;
        const int GosubCommand = 0xFC;
        const int MoveCommand = 43;
        var command = I(o[0]);
        var selector = I(o[2]);
        var args = L(o[3]);
        var target = 0;
        var destination = Vector3.Zero;
        if (command == GotoCommand)
        {
            if (o[1] is not BranchAction action)
            {
                return;
            }

            switch (action.Type)
            {
                case BranchAction.Kind.Gosub:
                    command = GosubCommand;
                    target = action.Target;
                    break;
                case BranchAction.Kind.Goto:
                case BranchAction.Kind.GosubElse:
                    target = action.Target;
                    break;
                default:
                    return;
            }
        }
        else if (command == MoveCommand)
        {
            var offset = L(o[1]);
            destination = runtime.NearTargetDestination(obj, F(offset[0]), F(offset[1]));
        }

        var k = 0;
        var param = 0f;
        var type = "";
        var id = 0;
        if (selector is 6 or 10)
        {
            param = F(args[k++]);
        }

        if (selector is 2 or 4 or 5 or 6 or 7 or 10)
        {
            type = (string)args[k++]!;
        }

        if (selector == 5)
        {
            id = I(args[k]);
        }

        runtime.CommandObjects(obj, command, target, destination, selector, type, param, id);
    }
}
