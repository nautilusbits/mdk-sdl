#include "formats/scripts/ScriptDecoder.h"

#include <unordered_set>

#include "formats/scripts/ScriptOpcodes.g.h"

namespace Mdk::Formats::Scripts
{
    namespace
    {
        // C#'s (int)operand: throws when it isn't an int.
        int I(const Operand& operand) { return std::get<int>(operand); }

        // Concat(params object?[][]): the parts one after another.
        OperandList Concat(OperandList first, const OperandList& second)
        {
            first.insert(first.end(), second.begin(), second.end());
            return first;
        }
    }

    ScriptDecoder::ScriptDecoder(SharedBytes bytes) : _bytes(std::move(bytes)) {}

    std::shared_ptr<const Instruction> ScriptDecoder::Decode(int pc)
    {
        if (auto* cached = _cache.Find(pc))
        {
            return *cached;
        }

        if (_dialect == ScriptDialect::Beta1996)
        {
            return _cache[pc] = DecodeBeta(pc);
        }

        int opcode = _bytes->at(pc);
        _p = pc + 1;
        _action.reset();
        OperandList operands;
        if (opcode != End)
        {
            auto codes = ScriptOpcodes::Operands.find(opcode);
            if (codes == ScriptOpcodes::Operands.end())
            {
                return _cache[pc] = nullptr;
            }

            for (auto& code : codes->second)
            {
                auto operand = Read(code, operands);
                operands.push_back(std::move(operand));
            }
        }

        return _cache[pc] = std::make_shared<const Instruction>(Instruction{pc, opcode, std::move(operands), _p, _action});
    }

    int ScriptDecoder::Preload(const std::vector<int>& starts)
    {
        if (_dialect != ScriptDialect::Retail)
        {
            return 0;
        }

        std::unordered_set<int> seen;
        std::vector<int> work(starts.begin(), starts.end());
        while (!work.empty())
        {
            auto p = work.back();
            work.pop_back();
            while (seen.insert(p).second)
            {
                auto ins = Decode(p);
                if (ins == nullptr || ins->Opcode == End)
                {
                    break;
                }

                for (auto target : Targets(*ins))
                {
                    work.push_back(target);
                }

                if (ScriptOpcodes::NoFallthrough.contains(ins->Opcode))
                {
                    break;
                }

                p = ins->Next;
            }
        }

        return static_cast<int>(seen.size());
    }

    std::vector<std::shared_ptr<const Instruction>> ScriptDecoder::Decoded() const
    {
        std::vector<std::shared_ptr<const Instruction>> decoded;
        for (auto& [pc, ins] : _cache)
        {
            if (ins != nullptr)
            {
                decoded.push_back(ins);
            }
        }

        return decoded;
    }

    std::vector<int> ScriptDecoder::Targets(const Instruction& ins)
    {
        std::vector<int> targets;
        static const std::vector<std::string> none;
        auto found = ScriptOpcodes::Operands.find(ins.Opcode);
        auto& codes = found == ScriptOpcodes::Operands.end() ? none : found->second;
        for (size_t i = 0; i < codes.size(); i++)
        {
            auto& code = codes[i];
            auto& operand = ins.Operands.at(i);
            auto* offset = std::get_if<int>(&operand);
            auto* items = std::get_if<OperandList>(&operand);
            if (code == "code32" && offset != nullptr && *offset != 0)
            {
                targets.push_back(*offset);
            }
            else if (Str::StartsWith(code, RepeatPrefix) && items != nullptr)
            {
                auto itemCodes = Str::Split(std::string_view(code).substr(RepeatPrefix.size()), ',');
                for (auto& entry : *items)
                {
                    auto& item = std::get<OperandList>(entry);
                    for (size_t k = 0; k < itemCodes.size(); k++)
                    {
                        auto* target = std::get_if<int>(&item.at(k));
                        if (itemCodes[k] == "code32" && target != nullptr && *target != 0)
                        {
                            targets.push_back(*target);
                        }
                    }
                }
            }
            else if (code == "op158_hit_target" && offset != nullptr && *offset != 0)
            {
                targets.push_back(*offset);
            }
        }

        if (ins.Action && ins.Action->Type != BranchAction::Kind::None)
        {
            for (auto t : {ins.Action->Target, ins.Action->Else})
            {
                if (t != 0)
                {
                    targets.push_back(t);
                }
            }
        }

        return targets;
    }

    byte ScriptDecoder::U8() { return _bytes->at(_p++); }

    float ScriptDecoder::F32()
    {
        _p += 4;
        return Bin::F32(*_bytes, _p - 4);
    }

    uint ScriptDecoder::U32()
    {
        _p += 4;
        return Bin::U32(*_bytes, _p - 4);
    }

    int ScriptDecoder::Offset()
    {
        auto value = U32();
        return value == 0 ? 0 : static_cast<int>(value) + FileBase;
    }

    std::string ScriptDecoder::PascalString()
    {
        int length = U8();
        _p += length;
        return Bin::Ascii(*_bytes, _p - length, length);
    }

    OperandList ScriptDecoder::Floats(int count)
    {
        OperandList floats(count);
        for (auto i = 0; i < count; i++)
        {
            floats[i] = F32();
        }

        return floats;
    }

    BranchAction ScriptDecoder::ReadAction()
    {
        auto action = U8();
        BranchAction result;
        switch (action)
        {
            case ActionGosubElse:
            {
                auto target = Offset();
                result = BranchAction{BranchAction::Kind::GosubElse, target, Offset()};
                break;
            }
            case ActionGosub:
                result = BranchAction{BranchAction::Kind::Gosub, Offset()};
                break;
            case ActionGoto:
                result = BranchAction{BranchAction::Kind::Goto, Offset()};
                break;
            case ActionReturn:
                result = BranchAction{BranchAction::Kind::Return};
                break;
            default:
                result = BranchAction{BranchAction::Kind::None, 0, 0, action};
                break;
        }

        _action = result;
        return result;
    }

    Operand ScriptDecoder::ReadValue()
    {
        int kind = U8();
        if (kind == LiteralKind)
        {
            return F32();
        }

        return Variable{kind, U8()};
    }

    Operand ScriptDecoder::Read(const std::string& code, const OperandList& prev)
    {
        if (code == "u8") { return static_cast<int>(U8()); }
        if (code == "s8") { return static_cast<int>(static_cast<sbyte>(U8())); }
        if (code == "u16") { _p += 2; return static_cast<int>(Bin::U16(*_bytes, _p - 2)); }
        if (code == "s16") { _p += 2; return static_cast<int>(Bin::S16(*_bytes, _p - 2)); }
        if (code == "u32") { return static_cast<int>(U32()); }
        if (code == "s32") { _p += 4; return Bin::S32(*_bytes, _p - 4); }
        if (code == "f32") { return F32(); }
        if (code == "pstr") { return PascalString(); }
        if (code == "code32" || code == "data32") { return Offset(); }
        if (code == "action") { return ReadAction(); }
        if (code == "value") { return ReadValue(); }
        if (code == "cond")
        {
            int op = U8();
            constexpr int Between = 7;
            constexpr int Outside = 8;
            if (op == Between || op == Outside)
            {
                auto low = F32();
                return OperandList{op, low, F32()};
            }

            return OperandList{op, F32()};
        }

        // Layouts depending on earlier operands (complex_operand() in the Python tool).
        if (code == "op4_command_args")
        {
            switch (I(prev.at(0)))
            {
                case 7: return ReadAction();
                case 43: return Floats(2);
                default: return {};
            }
        }

        if (code == "op4_selector_args") { return SelectorArgs(I(prev.at(2))); }
        if (code == "op2_origin") { return I(prev.at(4)) == 0 ? Operand(Floats(3)) : Operand(); }
        if (code == "op164_params") { return I(prev.at(0)) != 0 ? Operand(Floats(4)) : Operand(); }
        if (code == "op42_part") { return PartName(); }
        if (code == "op249_sound") { return I(prev.at(0)) == 1 ? Operand(PascalString()) : Operand(); }
        if (code == "op250_type_name") { return I(prev.at(0)) == 0xFF ? Operand(PascalString()) : Operand(); }
        if (code == "op250_box") { return Floats(I(prev.at(2)) == 2 ? 4 : 6); }
        if (code == "op174_b" || code == "op175_b")
        {
            auto b = I(prev.at(1));
            return b == 7 || b == 8 ? Operand(F32()) : Operand();
        }

        if (code == "op129_parts")
        {
            auto mode = I(prev.at(0));
            if (mode != 0 && mode != 1 && mode != 2)
            {
                return {};
            }

            int count = U8();
            OperandList parts;
            for (auto i = 0; i < count; i++)
            {
                parts.push_back(PascalString());
            }

            return parts;
        }

        if (code == "op132_position") { return I(prev.at(1)) == 0xFF ? Operand(Floats(3)) : Operand(); }
        if (code == "op89_position") { return Op89Position(I(prev.at(0))); }
        if (code == "op61_origin") { return Op61Origin(); }
        if (code == "op83_args") { return Op83Args(); }
        if (code == "op159_position") { return ModeThenFloats([](int mode) { return mode == 1 || mode == 2 ? 3 : 0; }); }
        if (code == "op189_args") { return ModeThenFloats([](int mode) { return mode == 0 ? 2 : (mode == 1 ? 1 : 0); }); }
        if (code == "op193_mode") { return Op193Mode(); }
        if (code == "op245_args") { return Op245Args(); }
        if (code == "op248_args") { return Op248Args(); }
        if (code == "op172_position" || code == "op178_position") { return Op172Position(); }
        if (code == "op173_target") { return Op173Target(); }
        if (code == "op180_offset") { return ModeThenFloats([](int mode) { return mode != 0 ? 3 : 0; }); }
        if (code == "op181_source") { return Op181Source(); }
        if (code == "op203_height") { return ModeThenFloats([](int mode) { return mode == 1 ? 1 : 0; }); }
        if (code == "op224_params") { return ModeThenFloats([](int mode) { return mode != 0 ? 2 : 0; }); }
        if (code == "op228_type_name") { return Op228TypeName(); }
        if (code == "op242_params") { return ModeThenFloats([](int mode) { return mode != 0 ? 12 : 0; }); }
        if (code == "op158_hit_target") { return (I(prev.at(2)) & 2) != 0 ? Operand(Offset()) : Operand(); }
        if (Str::StartsWith(code, RepeatPrefix))
        {
            return Repeat(Str::Split(std::string_view(code).substr(RepeatPrefix.size()), ','), prev);
        }

        throw InvalidDataException(std::format("Unknown operand code {}", code));
    }

    OperandList ScriptDecoder::SelectorArgs(int selector)
    {
        OperandList args;
        if (selector == 6 || selector == 10)
        {
            args.push_back(F32());
        }

        if (selector == 2 || selector == 4 || selector == 5 || selector == 6 || selector == 7 || selector == 10)
        {
            args.push_back(PascalString());
        }

        if (selector == 5)
        {
            args.push_back(static_cast<int>(U32()));
        }

        return args;
    }

    Operand ScriptDecoder::PartName()
    {
        auto part = PascalString();
        if (part.empty())
        {
            return OperandList{std::string(), PascalString()};
        }

        return part;
    }

    Operand ScriptDecoder::Op89Position(int flags)
    {
        constexpr int Point = 0x10;
        constexpr int Index = 0x20;
        constexpr int Offset = 0x40;
        if ((flags & Point) != 0)
        {
            return Floats(3);
        }

        if ((flags & Index) != 0)
        {
            return static_cast<int>(U8());
        }

        return (flags & Offset) != 0 ? Operand(Floats(3)) : Operand();
    }

    OperandList ScriptDecoder::Op61Origin()
    {
        int mode = U8();
        if (mode == 0)
        {
            return OperandList{mode, static_cast<int>(U8())};
        }

        return OperandList{mode, PascalString()};
    }

    Operand ScriptDecoder::Op83Args()
    {
        if (_bytes->at(_p) != 0xFF)
        {
            return ReadValue();
        }

        _p++;
        return Floats(2);
    }

    OperandList ScriptDecoder::ModeThenFloats(const std::function<int(int)>& count)
    {
        int mode = U8();
        return Concat(OperandList{mode}, Floats(count(mode)));
    }

    OperandList ScriptDecoder::Op193Mode()
    {
        int mode = U8();
        if (mode == 3)
        {
            return OperandList{mode, PascalString()};
        }

        return OperandList{mode};
    }

    OperandList ScriptDecoder::Op245Args()
    {
        if (_bytes->at(_p) != 0)
        {
            return OperandList{PascalString()};
        }

        _p++;
        int value = U8();
        return OperandList{value, PascalString()};
    }

    OperandList ScriptDecoder::Op248Args()
    {
        int mode = U8();
        auto x = F32();
        return Concat(OperandList{mode, x}, Floats(mode == 0 ? 2 : 1));
    }

    OperandList ScriptDecoder::Op172Position()
    {
        int mode = U8();
        if (mode == 3)
        {
            return OperandList{mode, static_cast<int>(U8())};
        }

        return Concat(OperandList{mode}, Floats(3));
    }

    OperandList ScriptDecoder::Op173Target()
    {
        auto name = PascalString();
        if (!name.empty())
        {
            return OperandList{name};
        }

        auto second = PascalString();
        return Concat(OperandList{std::string(), second}, Floats(4));
    }

    OperandList ScriptDecoder::Op181Source()
    {
        int mode = U8();
        switch (mode)
        {
            case 0:
            {
                int value = U8();
                return OperandList{mode, value, static_cast<int>(U32())};
            }
            case 1:
            {
                int value = U8();
                auto floats = Floats(2);
                return Concat(Concat(OperandList{mode, value}, floats), OperandList{static_cast<int>(U32())});
            }
            default:
                return OperandList{mode};
        }
    }

    Operand ScriptDecoder::Op228TypeName()
    {
        if (_bytes->at(_p) != 0)
        {
            return PascalString();
        }

        _p++;
        return OperandList{std::string(), static_cast<int>(U8())};
    }

    OperandList ScriptDecoder::Repeat(const std::vector<std::string>& itemCodes, const OperandList& prev)
    {
        int count = U8();
        OperandList items(count);
        for (auto i = 0; i < count; i++)
        {
            OperandList item;
            for (auto& c : itemCodes)
            {
                auto operand = Read(c, prev);
                item.push_back(std::move(operand));
            }

            items[i] = std::move(item);
        }

        return items;
    }
}
