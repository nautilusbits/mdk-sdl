#pragma once

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/Scripts/ScriptDecoder.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    // Runs MDK object scripts like the original interpreter (script_run). A port of godot-mdk's
    // script_vm.gd (see its docs/scripts.md and docs/script_opcodes.md). Opcodes not implemented
    // do nothing (their conditions are false), but they're always decoded, so scripts never lose sync.
    //
    //   restart ──► (wait over?) ──► decode ──► execute ──► next pc ─┐
    //                                   ▲                             │
    //                                   └──────── until YIELD, 0xFF ──┘
    class ScriptVm
    {
        using Instruction = Formats::Scripts::Instruction;
        using Operand = Formats::Scripts::Operand;
        using OperandList = Formats::Scripts::OperandList;

    public:
        ScriptVm(ScriptRuntime& runtime, std::shared_ptr<Formats::Scripts::ScriptDecoder> decoder)
            : _runtime(&runtime), _decoder(std::move(decoder))
        {
        }

        static constexpr int MaxOpcodesPerFrame = 1000;
        static constexpr int GosubDepth = 4;

    private:
        // Returned by handlers: stop running this object's script for this frame.
        static constexpr int Yield = -1;
        // Frame time in seconds and in 30 Hz ticks.
        static constexpr float Dt = ScriptRuntime::Tick;
        static constexpr float Ticks = 1.0f;
        // Comparisons' tolerance (compare_values).
        static constexpr float Epsilon = 0.05f;
        static constexpr float MinWait = 1e-5f;
        // The pitch error of inaccurate aims is a quarter of the yaw's.
        static constexpr float PitchErrorScale = 4.0f;
        static constexpr int FlagBits = 31;
        static constexpr int VariableCount = 4;
        // The 1996 demo's fire (FireBolt): a BOLT at 75 units a
        // second that takes 10 off Kurt's health (0x4e2ec).
        static constexpr std::string_view BetaBolt = "BOLT";
        static constexpr float BetaBoltSpeed = 75.0f;
        static constexpr int BetaBoltDamage = 10;

    public:
        // Opcodes seen that aren't implemented (opcode to count), for debugging.
        Dictionary<int, int> Unimplemented;

    private:
        // Opcodes run so far in the current object's frame (if_opcode_count).
        int _opcodeCount = 0;

    public:
        // Runs one frame of an object's script.
        void Run(Objects::MdkObject& obj);

    private:
        // Applies a branch action when the condition holds (or the else gosub when not). Returns the next pc.
        int Branch(Objects::MdkObject& obj, const Instruction& ins, bool condition);

        static int Goto(Objects::MdkObject& obj, int target);

        static int Gosub(Objects::MdkObject& obj, int returnPc, int target);

        static int Return(Objects::MdkObject& obj);

        static int Pop(std::vector<int>& stack);

        static void ClearStack(Objects::MdkObject& obj);

        // A value operand: a float literal, or a variable.
        float Value(Objects::MdkObject& obj, const Operand& operand);

        // Variables of a source: 0 global, 1 arena, 2 own, other = the linked object's.
        std::span<float> Variables(Objects::MdkObject& obj, int source);

        // The variables of a missing linked object: zeros, written to in vain (one array, cleared).
        std::span<float> NoVariables();

        std::array<float, VariableCount> _noVariables{};

        // Flag word 5: a door's own state (obj+0x312, 0x440a34); nothing on other objects.
        static constexpr int DoorFlags = 5;

        static bool IsDoor(Objects::MdkObject& obj);

        int GetFlags(Objects::MdkObject& obj, int source);

        void SetFlags(Objects::MdkObject& obj, int source, int value);

        // A comparison operand [op, a] or [op, a, b] (compare_values).
        static bool Compare(float value, const OperandList& cond);

        // A missing operand's 0, boxed once.
        static inline const Operand BoxedZero = 0.0f;
        // FindObject's candidates (kept: no list per call).
        std::vector<std::shared_ptr<Objects::MdkObject>> _found;

        // Compare(float, object?[]) on separate operands (no array).
        static bool Compare(float value, const Operand& op, const Operand& first, const Operand& second);

        // The highest instance id of the live objects following leader, or -1.
        // (No lambda in Execute: one capturing its object would be made at every instruction.)
        int LastFollowerId(Objects::MdkObject& leader);

        // The link of leader's chain (command 30) with the highest instance id, or null.
        std::shared_ptr<Objects::MdkObject> LastChainLink(Objects::MdkObject& leader);

        // The other live objects of obj's arena, as
        // GetArenaObjects, tested without a list.
        bool IsArenaOther(Objects::MdkObject& obj, Objects::MdkObject& other);

        // How the counting conditions count.
        enum class Counting { Commandable, Alive };

        int CountOthers(Objects::MdkObject& obj, const Operand& type, Counting counting);

        // The first other object of the arena of a type (and instance, unless any).
        std::shared_ptr<Objects::MdkObject> FirstOther(Objects::MdkObject& obj, const Operand& type, int instance);

        // Whether name is "TYPE_ID" of the object (as $"{TypeName}_{InstanceId}").
        static bool IsNamed(Objects::MdkObject& other, const Operand& name);

        std::shared_ptr<Objects::MdkObject> Bomb();

        // A 16-bit value as signed.
        static int S16(int value);

        // Picks a weighted random target ([[weight, target], ...]).
        int WeightedTarget(const OperandList& entries);

        // Picks a random target of a repeat operand of code offsets ([[target], ...]).
        int RandomTarget(const OperandList& entries);

        static bool Is(const std::string& a, const Operand& b);

        int Execute(Objects::MdkObject& obj, const Instruction& ins);

        // Whether a part mask operation hides the named parts or shows them again.
        enum class Masking { Hide, Show };

        // set_parts_mask / clear_parts_mask: the parts named in the list ("ALL" = every part);
        // blown off parts stay hidden.
        static int PartsMask(Objects::MdkObject& obj, const OperandList& entries, int mask, Masking masking);

        // A triangle group number (1-16) as an index.
        static int GroupIndex(const Operand& group);

        // boss_bar: mode 0 max - a group counter; mode 1 an arena variable going from high (0)
        // to low (max). Only while Kurt fires.
        void BossBar(Objects::MdkObject& obj, const OperandList& source);

        // lob_to_kurt: throws the object so it lands on Kurt (mode 1) or at a height (mode 0).
        void LobToKurt(Objects::MdkObject& obj, const OperandList& args);

        // if_in_box: an object of a type (o[0] = 0xFF) inside a box (mode 2: XY [x0, y0, x1, y1];
        // else [x0, y0, z0, x1, y1, z1], Z tested with mode 3).
        // A box operand's values: min x, y, z, max x, y, z (4 for a flat one).
        static constexpr int BoxValues = 6;

        bool InBox(Objects::MdkObject& obj, const OperandList& o);

        // An object of the arena (not obj) of a type (find_object): mode 0 a random one, 1 the
        // nearest, 2 the nearest of Kurt's items (0x1000) within 200, at most 9 units higher and in sight.
        std::shared_ptr<Objects::MdkObject> FindObject(Objects::MdkObject& obj, const std::string& type, int mode);

        // Pairs up the objects of a type without a partner that are closer than 100 units (link_partners).
        void LinkPartners(Objects::MdkObject& obj, const std::string& type);

        // A position operand (op172_position): mode 3 a reference point of the object, 1 relative
        // to the object, 2 turned by its yaw, other modes absolute.
        static Vector3 Point(Objects::MdkObject& obj, const OperandList& operand);

        void StartMove(Objects::MdkObject& obj, int command, Vector3 destination);

        // follow_path: operands [path, flags1, flags2, start frame, relative, origin].
        void FollowPath(Objects::MdkObject& obj, const OperandList& o);

        // The 1996 demo's follow_path (0x47f6c): a path stopped by opcode 21 goes on from
        // where it stopped, another one starts at its first frame. The object turns along the path,
        // whose positions are absolute, and goes round it again and again (0x32ff8).
        void FollowBetaPath(Objects::MdkObject& obj, int path);

        // The 1996 demo's fire: [origin, aim, range, accuracy, ?]. A bolt leaves a
        // reference point or a part, aimed at the target when aim is set (with aim_target's
        // error), else along the object's yaw; it has no script, flies range units and hurts Kurt
        // when it touches him.
        void FireBolt(Objects::MdkObject& obj, const OperandList& o);

        // Whether aim_target turns only when the target is more than 2 units away across.
        enum class Aiming { CheckDistance, Always };

        // Aims the yaw and pitch at the target (aim_target, aim_target_inaccurate), with a
        // random error unless the accuracy is 100.
        void Aim(Objects::MdkObject& obj, float accuracy, Aiming aiming);

        // Turns towards a point offset from the target in the target's frame
        // (turn_to_target_offset, and with a z offset the pitch too: turn_to_target_offset3d).
        void TurnToTarget(Objects::MdkObject& obj, float rate, float accuracy, float a, float b, std::optional<float> zOffset);

        // command_objects: operands [command, command args, selector, selector args].
        void CommandObjects(Objects::MdkObject& obj, const OperandList& o);

        ScriptRuntime* _runtime;
        std::shared_ptr<Formats::Scripts::ScriptDecoder> _decoder;
    };
}
