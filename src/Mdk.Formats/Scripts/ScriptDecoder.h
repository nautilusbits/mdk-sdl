#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Formats/BinReader.h"

namespace Mdk::Formats
{
    class Cmi;
}

namespace Mdk::Formats::Scripts
{
    // The bytecode of a CMI file: the retail game's, or the 1996 demo's earlier one.
    enum class ScriptDialect { Retail, Beta1996 };

    // The 1996 demo's opcodes that have no retail equivalent, numbered above 255 (see
    // ScriptDecoder, godot-mdk beta_script_decoder.gd).
    struct BetaOpcodes
    {
        // Opcodes 41 (nothing) and 90 (name, u8: an arena by name; in no script).
        static constexpr int Nothing = 300;
        // follow_path (2): [path]. A path stopped by opcode 21 goes on; the
        // object turns along it.
        static constexpr int FollowPath = 302;
        // Opcode 5: branches when the object's field +0x108 is 0 (in no script).
        static constexpr int IfField108 = 305;
        // Opcode 28: branches while an object that sounded the alarm was just deleted
        // (0xe21de, 10 ticks).
        static constexpr int IfAlarmEnded = 328;
        // fire (61): [origin, aim, range, accuracy, ?], a bolt without a script.
        static constexpr int Fire = 361;
    };

    // What a branch operand does: goto, gosub, gosub one of two (then/else), or return.
    // Targets are absolute file offsets.
    struct BranchAction
    {
        enum class Kind { None, Goto, Gosub, GosubElse, Return };

        Kind Type = Kind::None;
        int Target = 0;
        int Else = 0;
        int Raw = 0;

        bool operator==(const BranchAction&) const = default;
    };

    // A script variable operand: kind (0-2) and index.
    struct Variable
    {
        int Kind = 0;
        int Index = 0;

        bool operator==(const Variable&) const = default;
    };

    struct Operand;
    using OperandList = std::vector<Operand>;

    // An operand: null (monostate), int, float, string, BranchAction, Variable or a list
    // (C#'s object?).
    struct Operand : std::variant<std::monostate, int, float, std::string, BranchAction, Variable, OperandList>
    {
        using Base = std::variant<std::monostate, int, float, std::string, BranchAction, Variable, OperandList>;
        using Base::Base;

        Operand() = default;

        bool operator==(const Operand& other) const { return static_cast<const Base&>(*this) == static_cast<const Base&>(other); }
    };

    // A decoded instruction. Offsets (code32/data32 operands, branch targets) are absolute
    // file offsets (0 stays 0). Operands are int, float, string, BranchAction, Variable,
    // lists (OperandList) or null.
    struct Instruction
    {
        int Pc = 0;
        int Opcode = 0;
        OperandList Operands;
        int Next = 0;
        std::optional<BranchAction> Action;
    };

    // Decodes MDK script bytecode (LEVELn.CMI), like the Godot port's
    // script_decoder.gd and mdk_script_dis.py (validated against every script).
    //
    // The 1996 demo's scripts (MDKDEMO.EXE 0x47f6c, godot-mdk docs/beta96.md), decoded
    // into the instructions the retail VM runs: opcodes 1-92 and 128-131 have the retail numbers and
    // mostly the retail operands; branch actions are 0x0C goto, 0xF0 gosub, 0xF1
    // return; gosub and return are the opcodes 0xF0 and 0xF1. Where an opcode differs, the operands the
    // retail handler expects are made here; names are made upper case.
    class ScriptDecoder
    {
    public:
        static constexpr int End = 0xFF;

        explicit ScriptDecoder(SharedBytes bytes);

        // The instruction at file offset `pc` (cached), or null for an invalid opcode.
        std::shared_ptr<const Instruction> Decode(int pc);

        // Decodes every instruction reachable from `starts` into the cache (a
        // level's load: the scripts then run without decoding); returns how many. The retail dialect only.
        int Preload(const std::vector<int>& starts);

        // The instructions decoded so far (after Preload: every reachable one).
        std::vector<std::shared_ptr<const Instruction>> Decoded() const;

        // The code offsets an instruction can continue to besides the next one.
        static std::vector<int> Targets(const Instruction& ins);

        // The decoder of a CMI file's scripts, in its dialect.
        static std::shared_ptr<ScriptDecoder> For(const Cmi& cmi);

        // A decoder of the demo's bytecode, its paths not converted (tests, path search).
        static std::shared_ptr<ScriptDecoder> Beta(SharedBytes bytes);

        // The paths the demo's scripts follow (the raw offsets of opcode 2), found by walking
        // every script from its entry point, in the order met.
        static std::vector<int> BetaPaths(const Cmi& cmi);

    private:
        static constexpr int FileBase = 4;
        static constexpr int LiteralKind = 3;
        static constexpr byte ActionGosubElse = 0xFE;
        static constexpr byte ActionGosub = 0xFC;
        static constexpr byte ActionGoto = 0x0C;
        static constexpr byte ActionReturn = 0xFD;
        static constexpr std::string_view RepeatPrefix = "repeat:";

        static constexpr byte BetaGoto = 0x0C;
        static constexpr byte BetaGosub = 0xF0;
        static constexpr byte BetaReturn = 0xF1;
        static constexpr int RetailGosub = 252;
        static constexpr int RetailReturn = 253;
        static constexpr int SelectorById = 5;
        static constexpr int CommandGoto = 7;
        static constexpr int CommandMove = 43;

        struct BetaLayout
        {
            std::vector<std::string> Codes;
            int Opcode = 0;
        };

        // Opcodes after which a script doesn't go on: end, stop, goto, return.
        static const std::vector<int> BetaStops;
        // Demo opcode to its operands and, when it isn't the same, the retail opcode. Opcodes 2,
        // 4 and 61 are read by DecodeSpecial.
        static const std::unordered_map<int, BetaLayout> BetaLayouts;

        SharedBytes _bytes;
        Dictionary<int, std::shared_ptr<const Instruction>> _cache;
        int _p = 0;
        std::optional<BranchAction> _action;
        ScriptDialect _dialect = ScriptDialect::Retail;
        // The demo's path offsets to the converted ones (Cmi::BetaPaths).
        std::unordered_map<int, int> _paths;

        ScriptDecoder(SharedBytes bytes, ScriptDialect dialect, std::unordered_map<int, int> paths);

        byte U8();
        float F32();
        uint U32();
        int Offset();
        std::string PascalString();
        OperandList Floats(int count);
        BranchAction ReadAction();

        // A variable, or a float literal (kind 3).
        Operand ReadValue();

        Operand Read(const std::string& code, const OperandList& prev);
        OperandList SelectorArgs(int selector);
        Operand PartName();
        Operand Op89Position(int flags);
        OperandList Op61Origin();
        Operand Op83Args();

        // A mode byte, then as many floats as the mode asks for.
        OperandList ModeThenFloats(const std::function<int(int)>& count);

        OperandList Op193Mode();
        OperandList Op245Args();
        OperandList Op248Args();
        OperandList Op172Position();
        OperandList Op173Target();
        OperandList Op181Source();
        Operand Op228TypeName();
        OperandList Repeat(const std::vector<std::string>& itemCodes, const OperandList& prev);

        std::shared_ptr<const Instruction> DecodeBeta(int pc);

        // Opcodes 2, 4 and 61, whose operands depend on earlier ones. Returns the opcode to
        // run, or empty for the others.
        std::optional<int> DecodeSpecial(int opcode, OperandList& o);

        // The operands the retail handlers take, where the demo's differ.
        static OperandList RetailOperands(int opcode, const OperandList& o);

        Operand ReadBeta(const std::string& code, const OperandList& prev);
        BranchAction ReadBetaAction();

        // Names are in lower case in the demo's scripts; the port's are in upper case.
        std::string BetaString();
    };
}
