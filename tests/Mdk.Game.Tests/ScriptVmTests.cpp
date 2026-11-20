#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <doctest.h>

#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Formats/Scripts/ScriptDecoder.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ObjectBehaviors.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Mdk.Game/Scripts/ScriptVm.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Formats::Scripts;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// The script VM on tiny hand-assembled programs, with level 7's runtime around it (no window).
namespace
{
    constexpr int Level = 7;
    // Opcodes (docs/script_opcodes.md).
    constexpr int SetRestart = 1;
    constexpr int Goto = 12;
    constexpr int Wait = 64;
    constexpr int SetVar = 65;
    constexpr int AddVar = 66;
    constexpr int IfVar = 67;
    constexpr int SetFlag = 68;
    constexpr int IfFlagSet = 71;
    constexpr int Gosub = 252;
    constexpr int Return = 253;
    // Operand values.
    constexpr int Global = 0;
    constexpr int Own = 2;
    // Flag word 5: the object's own door state (obj+0x312, 0x440a34).
    constexpr int Door = 5;
    constexpr int LessThan = 1;
    constexpr int Literal = 3;
    constexpr byte ActionGoto = 0x0C;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime()
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, Level);
        auto number = std::to_string(Level);
        auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto space = std::make_shared<ArenaSpace>();
        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }

    // Shared by every test, as the C# static.
    ScriptRuntime& Runtime()
    {
        static std::shared_ptr<ScriptRuntime> runtime = CreateRuntime();
        return *runtime;
    }

    // A program's bytes: code starts after a gap (offset 0 means "no target"), targets are
    // written as stored in a CMI (file offset - 4).
    class Code
    {
        static constexpr int FileBase = 4;
        static constexpr int Start = 8;
        std::vector<byte> _bytes = std::vector<byte>(Start);
        std::unordered_map<std::string, int> _labels;
        std::vector<std::pair<int, std::string>> _fixups;

        void AddInt(int value)
        {
            byte raw[sizeof(int)];
            std::memcpy(raw, &value, sizeof(int));
            _bytes.insert(_bytes.end(), raw, raw + sizeof(int));
        }

    public:
        int Entry() const { return Start; }

        Code& Label(const std::string& name)
        {
            _labels[name] = static_cast<int>(_bytes.size());
            return *this;
        }

        Code& U8(int value)
        {
            _bytes.push_back(static_cast<byte>(value));
            return *this;
        }

        Code& F32(float value)
        {
            byte raw[sizeof(float)];
            std::memcpy(raw, &value, sizeof(float));
            _bytes.insert(_bytes.end(), raw, raw + sizeof(float));
            return *this;
        }

        Code& Target(const std::string& label)
        {
            _fixups.emplace_back(static_cast<int>(_bytes.size()), label);
            AddInt(0);
            return *this;
        }

        Code& End() { return U8(ScriptDecoder::End); }

        std::vector<byte> Build() const
        {
            auto bytes = _bytes;
            for (const auto& [at, label] : _fixups)
            {
                int value = _labels.at(label) - FileBase;
                std::memcpy(bytes.data() + at, &value, sizeof(int));
            }

            return bytes;
        }
    };

    struct Loaded
    {
        ScriptVm Vm;
        std::shared_ptr<MdkObject> Obj;
    };

    Loaded Load(const Code& code)
    {
        auto bytes = std::make_shared<const std::vector<byte>>(code.Build());
        ScriptVm vm(Runtime(), std::make_shared<ScriptDecoder>(bytes));
        auto obj = std::make_shared<MdkObject>();
        obj->Restart = code.Entry();
        return {std::move(vm), obj};
    }
}

DATA_TEST_CASE("ScriptVmTests.LoopCountsWithVariablesAndGoto")
{
    Code code;
    code.U8(SetVar).U8(Own).U8(0).F32(0.f)
        .Label("loop")
        .U8(AddVar).U8(Own).U8(0).F32(1.f)
        .U8(IfVar).U8(Own).U8(0).U8(LessThan).F32(5.f).U8(ActionGoto).Target("loop")
        .U8(SetVar).U8(Global).U8(1).F32(7.f)
        .End();
    auto [vm, obj] = Load(code);

    vm.Run(*obj);

    CHECK(obj->Variables[0] == 5.f);
    CHECK(Runtime().GlobalVariables[1] == 7.f);
    CHECK(obj->Restart != 0);
}

DATA_TEST_CASE("ScriptVmTests.GosubReturnsAfterTheCall")
{
    Code code;
    code.U8(Gosub).U8(1).Target("sub")
        .U8(SetVar).U8(Own).U8(1).F32(2.f)
        .End()
        .Label("sub")
        .U8(SetVar).U8(Own).U8(0).F32(1.f)
        .U8(Return)
        .End();
    auto [vm, obj] = Load(code);

    vm.Run(*obj);

    CHECK(obj->Variables[0] == 1.f);
    CHECK(obj->Variables[1] == 2.f);
    CHECK(obj->GosubReturns.empty());
    CHECK(obj->Restart == code.Entry());
}

DATA_TEST_CASE("ScriptVmTests.ReturnWithoutGosubStopsTheScript")
{
    Code code;
    code.U8(Return).End();
    auto [vm, obj] = Load(code);

    vm.Run(*obj);

    CHECK(obj->Restart == 0);
}

DATA_TEST_CASE("ScriptVmTests.WaitResumesAfterItsTicks")
{
    // 0.09 s: the script goes on at the 4th tick (30 ticks per second).
    constexpr float Seconds = 0.09f;
    Code code;
    code.U8(SetRestart)
        .U8(AddVar).U8(Own).U8(0).F32(1.f)
        .U8(Wait).U8(Literal).F32(Seconds)
        .U8(AddVar).U8(Own).U8(0).F32(10.f)
        .End();
    auto [vm, obj] = Load(code);

    vm.Run(*obj);
    CHECK(obj->Variables[0] == 1.f);
    vm.Run(*obj);
    vm.Run(*obj);
    CHECK(obj->Variables[0] == 1.f);
    vm.Run(*obj);
    CHECK(obj->Variables[0] == 11.f);
}

DATA_TEST_CASE("ScriptVmTests.FlagsBranchAndGotoZeroStops")
{
    constexpr int Bit = 3;
    Code code;
    code.U8(SetFlag).U8(Own).U8(Bit)
        .U8(IfFlagSet).U8(Own).U8(Bit).U8(ActionGoto).Target("set")
        .End()
        .Label("set")
        .U8(SetVar).U8(Own).U8(2).F32(4.f)
        .U8(Goto).U8(1).U8(0).U8(0).U8(0).U8(0)
        .End();
    auto [vm, obj] = Load(code);

    vm.Run(*obj);

    CHECK(obj->ScriptFlags == 1 << Bit);
    CHECK(obj->Variables[2] == 4.f);
    CHECK(obj->Restart == 0);
}

// Found by playtest: LEVEL4's airlock door waits for flag word 5 bit 3, its own
// "closed" bit; word 5 read the linked object's flags instead.
DATA_TEST_CASE("ScriptVmTests.FlagWordFiveIsTheDoorState")
{
    constexpr int ClosedBit = 3;
    constexpr int LockedBit = 6;
    Code code;
    code.U8(IfFlagSet).U8(Door).U8(ClosedBit).U8(ActionGoto).Target("closed")
        .End()
        .Label("closed")
        .U8(SetFlag).U8(Door).U8(LockedBit)
        .End();
    auto [vm, obj] = Load(code);
    obj->Flags |= MdkObject::FlagDoor;
    obj->DoorState = ObjectBehaviors::DoorClosed;
    obj->Linked = std::make_shared<MdkObject>();

    vm.Run(*obj);

    CHECK(obj->DoorState == (ObjectBehaviors::DoorClosed | ObjectBehaviors::DoorLocked));
    CHECK(obj->Linked->ScriptFlags == 0);
}

DATA_TEST_CASE("ScriptVmTests.UnimplementedConditionsAreFalse")
{
    // if_cheat_key (13) never holds: the goto isn't taken.
    constexpr int IfCheatKey = 13;
    Code code;
    code.U8(IfCheatKey).U8(ActionGoto).Target("cheat")
        .U8(SetVar).U8(Own).U8(0).F32(1.f)
        .End()
        .Label("cheat")
        .U8(SetVar).U8(Own).U8(0).F32(2.f)
        .End();
    auto [vm, obj] = Load(code);

    vm.Run(*obj);

    CHECK(obj->Variables[0] == 1.f);
}
