// Generated from mdk-sdl's ScriptOpcodes.g.cs (tools/gen_opcodes.py, godot-mdk/tools/python/script_opcodes.json). Don't edit.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace Mdk::Formats::Scripts
{
    // Operand layouts of the script opcodes (see ScriptDecoder).
    struct ScriptOpcodes
    {
        // Opcode to its mnemonic.
        static const std::map<int, std::string> Mnemonics;

        // Opcode to its operand codes: plain types (u8, f32, pstr...), code32/data32 offsets,
        // repeat:<items>, action, value, cond, or opN_name for special layouts.
        static const std::map<int, std::vector<std::string>> Operands;

        // Opcodes that never continue with the next instruction.
        static const std::set<int> NoFallthrough;
    };
}
