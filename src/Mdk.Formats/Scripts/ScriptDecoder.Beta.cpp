#include <algorithm>
#include <unordered_set>

#include "Mdk.Formats/Cmi.h"
#include "Mdk.Formats/Scripts/ScriptDecoder.h"

namespace Mdk::Formats::Scripts
{
    const std::vector<int> ScriptDecoder::BetaStops = {End, 9, 12, BetaReturn};

    const std::unordered_map<int, ScriptDecoder::BetaLayout> ScriptDecoder::BetaLayouts = {
        {1, {{}, 1}},
        {3, {{"data32"}, 3}},
        {5, {{"action"}, BetaOpcodes::IfField108}},
        {6, {{}, 6}},
        {8, {{"s16"}, 8}},
        {9, {{}, 9}},
        {10, {{"pstr", "u8", "action"}, 10}},
        {11, {{"u8"}, 11}},
        {12, {{"repeat:code32"}, 12}},
        {13, {{"action"}, 13}},
        {14, {{"u16", "u8", "action"}, 14}},
        {15, {{}, 15}},
        {16, {{"u16"}, 16}},
        {17, {{"action"}, 17}},
        {18, {{"f32", "action"}, 18}},
        {19, {{}, 19}},
        {20, {{}, 20}},
        {21, {{}, 21}},
        {22, {{"action"}, 22}},
        {23, {{}, 23}},
        {24, {{"u8", "pstr"}, 24}},
        {25, {{"pstr"}, 25}},
        {26, {{"pstr"}, 26}},
        {27, {{"action"}, 27}},
        {28, {{"action"}, BetaOpcodes::IfAlarmEnded}},
        {29, {{"u8", "pstr", "code32"}, 29}},
        {31, {{"repeat:pstr"}, 31}},
        {32, {{"repeat:pstr"}, 32}},
        {34, {{"action"}, 34}},
        {35, {{"u8"}, 35}},
        {36, {{"u8"}, 36}},
        {37, {{"action"}, 37}},
        {38, {{"cond", "action"}, 38}},
        {39, {{"value"}, 39}},
        {40, {{"value"}, 40}},
        {41, {{}, BetaOpcodes::Nothing}},
        {42, {{"pstr", "action"}, 42}},
        {43, {{"f32", "f32"}, 43}},
        {44, {{"action"}, 44}},
        {45, {{"cond", "action"}, 45}},
        {46, {{"action"}, 46}},
        {47, {{"f32", "action"}, 47}},
        {48, {{"f32", "action"}, 48}},
        {49, {{"u8", "action"}, 49}},
        {50, {{"value"}, 50}},
        {51, {{"value"}, 51}},
        {52, {{"value"}, 52}},
        {53, {{"value"}, 53}},
        {54, {{"cond", "action"}, 54}},
        {57, {{"u16", "u8", "action"}, 57}},
        {58, {{"value"}, 58}},
        {59, {{"data32"}, 59}},
        {60, {{}, 60}},
        {62, {{"cond", "action"}, 62}},
        {63, {{"u8"}, 63}},
        {64, {{"value"}, 64}},
        {65, {{"u8", "u8", "f32"}, 65}},
        {66, {{"u8", "u8", "f32"}, 66}},
        {67, {{"u8", "u8", "cond", "action"}, 67}},
        {68, {{"u8", "u8"}, 68}},
        {69, {{"u8", "u8"}, 69}},
        {70, {{"u8", "u8"}, 70}},
        {71, {{"u8", "u8", "action"}, 71}},
        {72, {{"u8", "u8", "action"}, 72}},
        {73, {{"u8"}, 73}},
        {74, {{"u8", "u8", "pstr"}, 74}},
        {75, {{}, 75}},
        {76, {{"code32"}, 76}},
        {77, {{"pstr"}, 77}},
        {78, {{"f32", "f32", "f32"}, 78}},
        {79, {{"f32", "f32", "f32"}, 79}},
        {80, {{"f32", "f32", "f32"}, 80}},
        {81, {{"value"}, 81}},
        {82, {{"value"}, 82}},
        {83, {{"value"}, 83}},
        {84, {{"value"}, 84}},
        {85, {{"u8"}, 85}},
        {86, {{"f32", "f32", "f32", "pstr", "code32"}, 86}},
        {87, {{"u16", "u16", "u8", "action"}, 87}},
        {88, {{"u8"}, 88}},
        {89, {{"pstr"}, 89}},
        {90, {{"pstr", "u8"}, BetaOpcodes::Nothing}},
        {91, {{"value"}, 91}},
        {92, {{"u8", "action"}, 92}},
        {128, {{"pstr", "u8", "u8"}, 128}},
        {129, {{"repeat:pstr"}, 129}},
        {130, {{}, 130}},
        {131, {{"u8"}, 131}},
        {BetaGosub, {{"repeat:code32"}, RetailGosub}},
        {BetaReturn, {{}, RetailReturn}},
    };

    ScriptDecoder::ScriptDecoder(SharedBytes bytes, ScriptDialect dialect, std::unordered_map<int, int> paths)
        : ScriptDecoder(std::move(bytes))
    {
        _dialect = dialect;
        _paths = std::move(paths);
    }

    std::shared_ptr<ScriptDecoder> ScriptDecoder::For(const Cmi& cmi)
    {
        if (cmi.Dialect() == ScriptDialect::Retail)
        {
            return std::make_shared<ScriptDecoder>(cmi.Bytes());
        }

        std::unordered_map<int, int> paths;
        for (auto& [from, to] : cmi.BetaPaths)
        {
            paths[from] = to;
        }

        return std::shared_ptr<ScriptDecoder>(new ScriptDecoder(cmi.Bytes(), cmi.Dialect(), std::move(paths)));
    }

    std::shared_ptr<ScriptDecoder> ScriptDecoder::Beta(SharedBytes bytes)
    {
        return std::shared_ptr<ScriptDecoder>(new ScriptDecoder(std::move(bytes), ScriptDialect::Beta1996, {}));
    }

    std::vector<int> ScriptDecoder::BetaPaths(const Cmi& cmi)
    {
        constexpr int FollowPath = 2;
        auto& bytes = *cmi.Bytes();
        auto decoder = Beta(cmi.Bytes());
        std::vector<int> paths;
        std::unordered_set<int> seen;
        std::vector<int> todo(cmi.EntryPoints().begin(), cmi.EntryPoints().end());
        while (!todo.empty())
        {
            auto pc = todo.back();
            todo.pop_back();
            while (pc > 0 && pc < static_cast<int>(bytes.size()) && seen.insert(pc).second)
            {
                int opcode = bytes[pc];
                if (opcode == FollowPath)
                {
                    auto raw = Bin::U32(bytes, pc + 1);
                    auto path = static_cast<int>(raw) + FileBase;
                    if (raw != 0 && std::find(paths.begin(), paths.end(), path) == paths.end())
                    {
                        paths.push_back(path);
                    }
                }

                auto ins = decoder->Decode(pc);
                if (ins == nullptr)
                {
                    break;
                }

                for (auto target : Targets(*ins))
                {
                    todo.push_back(target);
                }

                if (std::find(BetaStops.begin(), BetaStops.end(), opcode) != BetaStops.end())
                {
                    break;
                }

                pc = ins->Next;
            }
        }

        return paths;
    }

    std::shared_ptr<const Instruction> ScriptDecoder::DecodeBeta(int pc)
    {
        int opcode = _bytes->at(pc);
        _p = pc + 1;
        _action.reset();
        if (opcode == End)
        {
            return std::make_shared<const Instruction>(Instruction{pc, opcode, {}, _p, std::nullopt});
        }

        OperandList operands;
        if (auto special = DecodeSpecial(opcode, operands))
        {
            return std::make_shared<const Instruction>(Instruction{pc, *special, std::move(operands), _p, _action});
        }

        auto layout = BetaLayouts.find(opcode);
        if (layout == BetaLayouts.end())
        {
            return nullptr;
        }

        for (auto& code : layout->second.Codes)
        {
            auto operand = ReadBeta(code, operands);
            operands.push_back(std::move(operand));
        }

        return std::make_shared<const Instruction>(Instruction{pc, layout->second.Opcode, RetailOperands(opcode, operands), _p, _action});
    }

    std::optional<int> ScriptDecoder::DecodeSpecial(int opcode, OperandList& o)
    {
        constexpr int FollowPath = 2;
        constexpr int CommandObjects = 4;
        constexpr int Fire = 61;
        switch (opcode)
        {
            case FollowPath:
            {
                auto found = _paths.find(Offset());
                o.push_back(found == _paths.end() ? 0 : found->second);
                return BetaOpcodes::FollowPath;
            }
            case CommandObjects:
            {
                // The selector's id is a byte.
                int command = U8();
                Operand arguments;
                if (command == CommandGoto)
                {
                    arguments = ReadBetaAction();
                }
                else if (command == CommandMove)
                {
                    arguments = Floats(2);
                }

                int selector = U8();
                OperandList selected;
                if (selector == 2 || selector == 4 || selector == SelectorById)
                {
                    selected.push_back(BetaString());
                }

                if (selector == SelectorById)
                {
                    selected.push_back(static_cast<int>(U8()));
                }

                o.insert(o.end(), {command, arguments, selector, selected});
                return CommandObjects;
            }
            case Fire:
            {
                // Where from (a reference point or a part), whether it's aimed, then three floats.
                int mode = U8();
                OperandList origin = mode == 0 ? OperandList{mode, static_cast<int>(U8())} : OperandList{mode, BetaString()};
                o.insert(o.end(), {origin, static_cast<int>(U8()), F32(), F32(), F32()});
                return BetaOpcodes::Fire;
            }
        }

        return std::nullopt;
    }

    OperandList ScriptDecoder::RetailOperands(int opcode, const OperandList& o)
    {
        constexpr int StopHere = -2;
        switch (opcode)
        {
            case 21:
                // Stops the path where it is.
                return OperandList{StopHere};
            case 23:
                return OperandList{1};
            case 77:
                return OperandList{1, o.at(0)};
            case 81:
                // Not multiplied by the frame time.
                return OperandList{0, o.at(0)};
            case 89:
                // Played where the object is.
                return OperandList{0, Operand(), o.at(0)};
            case 92:
                // The frame itself, not the frame + 1.
                return OperandList{std::get<int>(o.at(0)) + 1, o.at(1)};
            case 129:
            {
                // No mode.
                OperandList names;
                for (auto& entry : std::get<OperandList>(o.at(0)))
                {
                    names.push_back(std::get<OperandList>(entry).at(0));
                }

                return OperandList{0, names};
            }
        }

        return o;
    }

    Operand ScriptDecoder::ReadBeta(const std::string& code, const OperandList& prev)
    {
        if (code == "action")
        {
            return ReadBetaAction();
        }

        if (code == "pstr")
        {
            return BetaString();
        }

        if (!Str::StartsWith(code, RepeatPrefix))
        {
            return Read(code, prev);
        }

        int count = U8();
        auto item = code.substr(RepeatPrefix.size());
        OperandList items(count);
        for (auto i = 0; i < count; i++)
        {
            items[i] = OperandList{ReadBeta(item, prev)};
        }

        return items;
    }

    BranchAction ScriptDecoder::ReadBetaAction()
    {
        auto action = U8();
        BranchAction result;
        switch (action)
        {
            case BetaGosub:
                result = BranchAction{BranchAction::Kind::Gosub, Offset()};
                break;
            case BetaGoto:
                result = BranchAction{BranchAction::Kind::Goto, Offset()};
                break;
            case BetaReturn:
                result = BranchAction{BranchAction::Kind::Return};
                break;
            default:
                result = BranchAction{BranchAction::Kind::None, 0, 0, action};
                break;
        }

        _action = result;
        return result;
    }

    std::string ScriptDecoder::BetaString() { return Str::ToUpper(PascalString()); }
}
