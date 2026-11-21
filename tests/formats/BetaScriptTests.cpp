#include <doctest.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "Bytes.h"
#include "formats/scripts/ScriptDecoder.h"

using namespace Mdk;
using namespace Mdk::Formats;
using namespace Mdk::Formats::Scripts;
using Mdk::Formats::Tests::Bytes;

// The 1996 demo's bytecode (ScriptDialect::Beta1996) on synthetic scripts:
// every opcode's length against the reference decoder's table (godot-mdk
// tools/python/beta96.py LAYOUTS, copied here as is), and the operands made for the
// retail handlers.

namespace
{
    constexpr int Target = 0x40;
    constexpr int FileBase = 4;

    // beta96.py's LAYOUTS (in the C# dictionary's order).
    const std::vector<std::pair<int, std::vector<std::string>>> Reference = {
        {1, {}}, {2, {"data"}}, {3, {"data"}}, {4, {"op4"}},
        {5, {"act"}}, {6, {}}, {8, {"s16"}}, {9, {}},
        {10, {"pstr", "u8", "act"}}, {11, {"u8"}}, {12, {"rep"}}, {13, {"act"}},
        {14, {"u16", "u8", "act"}}, {15, {}}, {16, {"u16"}}, {17, {"act"}},
        {18, {"f32", "act"}}, {19, {}}, {20, {}}, {21, {}},
        {22, {"act"}}, {23, {}}, {24, {"u8", "pstr"}}, {25, {"pstr"}},
        {26, {"pstr"}}, {27, {"act"}}, {28, {"act"}}, {29, {"u8", "pstr", "code"}},
        {31, {"reps"}}, {32, {"reps"}}, {34, {"act"}}, {35, {"u8"}},
        {36, {"u8"}}, {37, {"act"}}, {38, {"cond", "act"}}, {39, {"val"}},
        {40, {"val"}}, {41, {}}, {42, {"pstr", "act"}}, {43, {"f32", "f32"}},
        {44, {"act"}}, {45, {"cond", "act"}}, {46, {"act"}}, {47, {"f32", "act"}},
        {48, {"f32", "act"}}, {49, {"u8", "act"}}, {50, {"val"}}, {51, {"val"}},
        {52, {"val"}}, {53, {"val"}}, {54, {"cond", "act"}}, {57, {"u16", "u8", "act"}},
        {58, {"val"}}, {59, {"data"}}, {60, {}}, {61, {"op61"}},
        {62, {"cond", "act"}}, {63, {"u8"}}, {64, {"val"}}, {65, {"u8", "u8", "f32"}},
        {66, {"u8", "u8", "f32"}}, {67, {"u8", "u8", "cond", "act"}}, {68, {"u8", "u8"}}, {69, {"u8", "u8"}},
        {70, {"u8", "u8"}}, {71, {"u8", "u8", "act"}}, {72, {"u8", "u8", "act"}}, {73, {"u8"}},
        {74, {"u8", "u8", "pstr"}}, {75, {}}, {76, {"code"}}, {77, {"pstr"}},
        {78, {"f32", "f32", "f32"}}, {79, {"f32", "f32", "f32"}}, {80, {"f32", "f32", "f32"}}, {81, {"val"}},
        {82, {"val"}}, {83, {"val"}}, {84, {"val"}}, {85, {"u8"}},
        {86, {"f32", "f32", "f32", "pstr", "code"}}, {87, {"u16", "u16", "u8", "act"}}, {88, {"u8"}}, {89, {"pstr"}},
        {90, {"pstr", "u8"}}, {91, {"val"}}, {92, {"u8", "act"}}, {128, {"pstr", "u8", "u8"}},
        {129, {"reps"}}, {130, {}}, {131, {"u8"}}, {0xF0, {"rep"}},
        {0xF1, {}},
    };

    const std::vector<std::string>& ReferenceOf(int opcode)
    {
        return std::find_if(Reference.begin(), Reference.end(), [&](const auto& r) { return r.first == opcode; })->second;
    }

    // Sample operands of a reference code: a goto action, a literal value, a "between"
    // comparison, two targets, two names, a command 7 to selector 5, a part to fire from.
    void Write(Bytes& b, const std::string& code)
    {
        if (code == "u8")
        {
            b.U8(7);
        }
        else if (code == "u16" || code == "s16")
        {
            b.S16(2);
        }
        else if (code == "f32")
        {
            b.F32(1.5f);
        }
        else if (code == "pstr")
        {
            b.Pascal("abc");
        }
        else if (code == "data" || code == "code")
        {
            b.U32(Target);
        }
        else if (code == "act")
        {
            b.U8(0x0C).U32(Target);
        }
        else if (code == "val")
        {
            b.U8(3).F32(2.0f);
        }
        else if (code == "cond")
        {
            b.U8(7).F32(1.0f, 2.0f);
        }
        else if (code == "rep")
        {
            b.U8(2).U32(Target, Target + 1);
        }
        else if (code == "reps")
        {
            b.U8(2).Pascal("ab").Pascal("c");
        }
        else if (code == "op4")
        {
            b.U8(7, 0x0C).U32(Target).U8(5).Pascal("xg").U8(3);
        }
        else if (code == "op61")
        {
            b.U8(1).Pascal("gun").U8(1).F32(10.0f, 50.0f, 0.0f);
        }
    }

    Instruction Decode(int opcode, const std::function<void(Bytes&)>& operands = nullptr)
    {
        Bytes b;
        b.U8(opcode);
        if (operands)
        {
            operands(b);
        }
        else
        {
            for (auto& code : ReferenceOf(opcode))
            {
                Write(b, code);
            }
        }

        auto ins = ScriptDecoder::Beta(Formats::Share(b.ToArray()))->Decode(0);
        if (ins == nullptr)
        {
            throw InvalidDataException(std::format("opcode {}", opcode));
        }

        return *ins;
    }
}

TEST_CASE("BetaScriptTests.EveryOpcodeHasTheReferenceLength")
{
    for (auto& [opcode, codes] : Reference)
    {
        Bytes b;
        b.U8(opcode);
        for (auto& code : codes)
        {
            Write(b, code);
        }

        CAPTURE(opcode);
        CHECK(b.Position() == Decode(opcode).Next);
    }
}

TEST_CASE("BetaScriptTests.RetailOnlyOpcodesAreInvalid")
{
    for (auto opcode : {7, 30, 33, 55, 56, 93, 0xFC})
    {
        CAPTURE(opcode);
        CHECK(ScriptDecoder::Beta(Formats::Share({static_cast<byte>(opcode), 0, 0, 0, 0, 0, 0, 0, 0}))->Decode(0) == nullptr);
    }
}

TEST_CASE("BetaScriptTests.ActionsHaveTheirOwnCodes")
{
    std::pair<int, BranchAction::Kind> data[] = {
        {0x0C, BranchAction::Kind::Goto}, {0xF0, BranchAction::Kind::Gosub}, {0xF1, BranchAction::Kind::Return}, {0xFC, BranchAction::Kind::None}};
    for (auto [code, kind] : data)
    {
        CAPTURE(code);
        auto ins = Decode(17, [&](Bytes& b) { b.U8(code).U32(Target); });
        REQUIRE(ins.Action.has_value());
        CHECK(kind == ins.Action->Type);
        if (kind == BranchAction::Kind::Goto || kind == BranchAction::Kind::Gosub)
        {
            CHECK(Target + FileBase == ins.Action->Target);
        }
    }
}

TEST_CASE("BetaScriptTests.GosubAndReturnAreRetailOpcodes")
{
    auto gosub = Decode(0xF0);
    CHECK(252 == gosub.Opcode);
    CHECK(std::vector<int>{Target + FileBase, Target + 1 + FileBase} == ScriptDecoder::Targets(gosub));
    CHECK(253 == Decode(0xF1).Opcode);
}

TEST_CASE("BetaScriptTests.OperandsAreMadeForTheRetailHandlers")
{
    CHECK(OperandList{-2} == Decode(21).Operands);
    CHECK(OperandList{1} == Decode(23).Operands);
    CHECK(OperandList{1, std::string("ABC")} == Decode(77).Operands);
    CHECK(OperandList{0, Operand(), std::string("ABC")} == Decode(89).Operands);
    CHECK(OperandList{0, 2.0f} == Decode(81).Operands);
    CHECK(Operand(8) == Decode(92).Operands[0]);
    CHECK(OperandList{0, OperandList{std::string("AB"), std::string("C")}} == Decode(129).Operands);
}

TEST_CASE("BetaScriptTests.OwnOpcodesGetNumbersAboveTheRetailOnes")
{
    CHECK(BetaOpcodes::IfField108 == Decode(5).Opcode);
    CHECK(BetaOpcodes::IfAlarmEnded == Decode(28).Opcode);
    CHECK(BetaOpcodes::Nothing == Decode(41).Opcode);
    CHECK(BetaOpcodes::Nothing == Decode(90).Opcode);
    CHECK(BetaOpcodes::FollowPath == Decode(2).Opcode);
}

TEST_CASE("BetaScriptTests.CommandObjectsSelectByAByteId")
{
    auto ins = Decode(4);
    CHECK(Operand(7) == ins.Operands[0]);
    CHECK(BranchAction::Kind::Goto == std::get<BranchAction>(ins.Operands[1]).Type);
    CHECK(Operand(5) == ins.Operands[2]);
    CHECK(Operand(OperandList{std::string("XG"), 3}) == ins.Operands[3]);
    CHECK(std::vector<int>{Target + FileBase} == ScriptDecoder::Targets(ins));
}

TEST_CASE("BetaScriptTests.FireIsABoltFromAPart")
{
    auto ins = Decode(61);
    CHECK(BetaOpcodes::Fire == ins.Opcode);
    CHECK(Operand(OperandList{1, std::string("GUN")}) == ins.Operands[0]);
    CHECK(OperandList{1, 10.0f, 50.0f, 0.0f} == OperandList(ins.Operands.begin() + 1, ins.Operands.end()));
}
