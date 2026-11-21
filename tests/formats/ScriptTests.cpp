#include <doctest.h>

#include <format>
#include <unordered_set>
#include <utility>
#include <vector>

#include "formats/Cmi.h"
#include "formats/scripts/ScriptDecoder.h"
#include "formats/scripts/ScriptOpcodes.g.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Formats;
using namespace Mdk::Formats::Scripts;
using Mdk::Tests::GameData;

// Decodes every script of the game, walking from each entry point like the Godot port's
// mdk_script_dis.py check.

namespace
{
    constexpr int FirstLevel = 3;
    constexpr int LastLevel = 8;
    // Counts of mdk_script_dis.py over the same entry points (one bad opcode in
    // data the scripts never reach at run time).
    constexpr int ExpectedDecoded = 99063;
    constexpr int ExpectedInvalid = 1;

    // Instructions reachable from `start`: decoded and invalid.
    std::pair<int, int> Walk(ScriptDecoder& decoder, int start)
    {
        std::unordered_set<int> seen;
        auto invalid = 0;
        std::vector<int> work = {start};
        while (!work.empty())
        {
            auto p = work.back();
            work.pop_back();
            while (seen.insert(p).second)
            {
                auto ins = decoder.Decode(p);
                if (ins == nullptr)
                {
                    invalid++;
                    break;
                }

                if (ins->Opcode == ScriptDecoder::End)
                {
                    break;
                }

                for (auto target : ScriptDecoder::Targets(*ins))
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

        return {static_cast<int>(seen.size()) - invalid, invalid};
    }
}

DATA_TEST_CASE("ScriptTests.EveryScriptDecodes")
{
    auto decoded = 0;
    auto invalid = 0;
    for (auto level = FirstLevel; level <= LastLevel; level++)
    {
        auto cmi = Cmi::Load(GameData::Data()->PathOf(std::format("TRAVERSE/LEVEL{}/LEVEL{}.CMI", level, level)));
        ScriptDecoder decoder(cmi->Bytes());
        for (auto start : cmi->EntryPoints())
        {
            auto [ok, bad] = Walk(decoder, start);
            decoded += ok;
            invalid += bad;
        }
    }

    CHECK(ExpectedInvalid == invalid);
    CHECK(ExpectedDecoded == decoded);
}
