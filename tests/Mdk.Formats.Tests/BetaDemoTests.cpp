#include <doctest.h>

#include <cstdlib>
#include <format>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "Bytes.h"
#include "Mdk.Core/Files.h"
#include "Mdk.Formats/Arena.h"
#include "Mdk.Formats/BetaDemo.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Formats/ModelAnimation.h"
#include "Mdk.Formats/Scripts/ScriptDecoder.h"
#include "Mdk.Formats/Sni.h"
#include "Mdk.Formats/TextureArchive.h"
#include "Shared/GameData.h"
#include "Shared/TempDir.h"

using namespace Mdk;
using namespace Mdk::Formats;
using namespace Mdk::Formats::Scripts;
using Mdk::Formats::Tests::Bytes;
using Mdk::Tests::BetaData;

// The 1996 beta demo's formats (godot-mdk docs/beta96.md, mdk_beta.gd) on synthetic
// files, and the demo's levels themselves when it's installed (like the Godot port's
// beta96_test.gd).

namespace
{
    constexpr const char* BetaVariable = "MDK_BETA_DIR";
    constexpr uint NotDrawn = BetaDemo::NotDrawn;

    // Environment.SetEnvironmentVariable: empty removes it.
    void SetEnv(const char* name, const std::optional<std::string>& value)
    {
#if defined(_WIN32)
        _putenv_s(name, value ? value->c_str() : "");
#else
        if (value)
        {
            setenv(name, value->c_str(), 1);
        }
        else
        {
            unsetenv(name);
        }
#endif
    }

    // A square floor at z 0: one node (z = 0) holding two up-facing triangles.
    std::vector<byte> FloorWorld(float z = 0.0f)
    {
        Bytes b;
        b.U32(1).Name("wall", 16);
        b.U32(1).F32(0.0f, 0.0f, 1.0f, 0.0f).S16(-1, -1, 2, 0, 0, 0).S16(0, 0, 0, 0);
        b.U32(2);
        b.S16(0, 1, 2, 0).F32(0, 0, 1, 0, 1, 1).U32(NotDrawn);
        b.S16(0, 2, 3, 0).F32(0, 0, 1, 1, 0, 1).U32(0);
        b.U32(4).Vec({-100.0f, -100.0f, z}).Vec({100.0f, -100.0f, 0.0f}).Vec({100.0f, 100.0f, 0.0f}).Vec({-100.0f, 100.0f, 0.0f});
        return b.ToArray();
    }

    // A texture archive without the retail header: a 2 x 2 texture and a palette colour.
    std::vector<byte> Archive(const std::string& texture)
    {
        constexpr int Base = 4;
        Bytes b;
        b.U32(0).U32(2);
        auto offset = b.Position();
        b.Name(texture, 8).U32(0, 0).F32(0.0f).U32(0);
        b.Name("pen", 8).U32(0xFFFFFFFF, 5).F32(0.0f).U32(0);
        b.Patch(offset + 20, b.Position() - Base);
        b.U16(2).U16(2).U8(0, 1, 0, 2);
        return b.ToArray();
    }

    struct CmiFile
    {
        std::vector<byte> Bytes;
        int Script;
        int Path;
    };

    // A CMI file: one alien script following a path, one arena with its music and loop.
    CmiFile MakeCmi()
    {
        constexpr int FileBase = 4;
        Bytes b;
        b.U32(0);
        b.U32(1).Pascal("arena_1$xg_0");
        auto scriptField = b.Position();
        b.U32(0).U32(0).U32(0).U32(1).Pascal("arena_1");
        auto arenaField = b.Position();
        b.U32(0);
        b.Patch(arenaField, b.Position() - FileBase).Pascal("song_active").Pascal("amb1");
        auto script = b.Position();
        b.Patch(scriptField, script - FileBase).U8(2);
        auto pathField = b.Position();
        b.U32(0).U8(ScriptDecoder::End);
        auto path = b.Position();
        b.Patch(pathField, path - FileBase).U32(3).Vec({1.0f, 2.0f, 3.0f}).Vec(Vector3::UnitX()).Vec(Vector3::UnitY());
        return {b.ToArray(), script, path};
    }

    struct LevelCounts
    {
        int Level;
        int Arenas;
        int Aliens;
        int Instructions;
        int Paths;
    };

    // Level: arenas and corridors, aliens placed, instructions, paths (beta96_test.gd).
    const LevelCounts Levels[] = {{1, 19, 35, 640, 22}, {3, 2, 2, 143, 0}, {6, 1, 10, 337, 0}};

    void CheckLevel(int level, int arenas, int aliens, int instructions, int paths)
    {
        auto& demo = BetaData::Demo();
        auto dti = demo->LoadDti(level);
        CHECK(arenas == dti->Arenas.size());
        auto alienCount = 0;
        for (auto& a : dti->Arenas)
        {
            alienCount += static_cast<int>(std::count_if(a.Records.begin(), a.Records.end(), [](const Dti::Record& r) { return r.Type == 2; }));
        }

        CHECK(aliens == alienCount);
        CHECK(dti->Sky->Width * dti->Sky->Height == dti->Sky->Indices.size());

        auto mto = demo->LoadMto(level);
        for (auto& name : mto->ArenaNames())
        {
            auto arena = mto->GetArena(name);
            CHECK(!arena->Vertices.empty());
            CHECK(Arena::PaletteColors * 3 == arena->PaletteRgb.size());
            CHECK(0 == BetaDemo::CheckNodes(*arena));
        }

        auto cmi = demo->LoadCmi(level);
        CHECK(paths == cmi->BetaPaths.Count());
        auto decoder = ScriptDecoder::For(*cmi);
        std::unordered_set<int> seen;
        std::vector<int> todo(cmi->EntryPoints().begin(), cmi->EntryPoints().end());
        while (!todo.empty())
        {
            auto pc = todo.back();
            todo.pop_back();
            while (pc > 0 && !seen.contains(pc))
            {
                auto ins = decoder->Decode(pc);
                INFO(std::format("LEVEL{}: unknown opcode {} at {:x}", level, (*cmi->Bytes())[pc], pc));
                REQUIRE(ins != nullptr);
                seen.insert(pc);
                for (auto target : ScriptDecoder::Targets(*ins))
                {
                    todo.push_back(target);
                }

                if (ins->Opcode == 3 || ins->Opcode == 59)
                {
                    CHECK(cmi->GetBetaAnimation(std::get<int>(ins->Operands[0]))->FrameCount > 0);
                }

                if (ins->Opcode == ScriptDecoder::End || ins->Opcode == 9 || ins->Opcode == 12 || ins->Opcode == 253)
                {
                    break;
                }

                pc = ins->Next;
            }
        }

        CHECK(instructions == seen.size());
        for (auto& [name, offset] : cmi->ModelOffsets)
        {
            CHECK(cmi->GetModel(name) != nullptr);
        }
    }
}

TEST_CASE("BetaDemoTests.LevelNumbersAreAboveTheRetailOnes")
{
    CHECK(961 == BetaDemo::NumberOf(1));
    CHECK(6 == BetaDemo::LevelOf(966));
    CHECK(BetaDemo::IsBeta(963));
    CHECK(!BetaDemo::IsBeta(8));
}

TEST_CASE("BetaDemoTests.TheDemoIsFoundByItsVariable")
{
    Mdk::Tests::TempDir temp("mdk-beta");
    auto& dir = temp.Path();
    auto marker = Path::Combine(dir, "TRAVERSE", "LEVEL1", "LEVEL1.SET");
    Directory::CreateDirectory(Path::GetDirectoryName(marker));
    File::WriteAllText(marker, "");
    auto before = GetEnv(BetaVariable);
    SetEnv(BetaVariable, dir);
    auto demo = BetaDemo::Find(nullptr);
    SetEnv(BetaVariable, before);
    REQUIRE(demo != nullptr);
    CHECK(dir == demo->Dir());
}

TEST_CASE("BetaDemoTests.SettingsGiveTheStartSkyAndGlass")
{
    const std::string Set = "2 10.5 -20 30 180 ; start\n\n200 201 120\n1 2 3 40\n5 6 7 80\n9 10 11 120\n13 14 15 160\n";
    auto dti = BetaDemo::ParseSettings(Set);
    CHECK(2 == dti->StartArena);
    CHECK(Vector3(10.5f, -20.0f, 30.0f) == dti->StartPosition);
    CHECK(180.0f == dti->StartAngle);
    CHECK(std::tuple(200, 201, 120) == std::tuple(dti->SkyTopColor, dti->SkyBottomColor, dti->SkyHorizonRow));
    CHECK(Dti::GlassCount == dti->Glass.size());
    CHECK(std::vector<uint>{13u, 14u, 15u, 160u} == dti->Glass[3]);
}

TEST_CASE("BetaDemoTests.LevelThreeHasNoStartAngle")
{
    CHECK(90.0f == BetaDemo::ParseSettings("0 1 2 3\n1 2 3\n0 0 0 0\n0 0 0 0\n0 0 0 0\n0 0 0 0\n")->StartAngle);
}

TEST_CASE("BetaDemoTests.ArenaNamesSkipThePortals")
{
    const std::string Con = "3\narena_1 2\nC1 0 5 1 0 1\nC2 1 5 1 0 1\ncorr_1\t0\r\nhmo_1 1\nC0 0 0 0 0 0\n";
    CHECK(std::vector<std::string>{"ARENA_1", "CORR_1", "HMO_1"} == BetaDemo::ParseArenaNames(Con));
}

TEST_CASE("BetaDemoTests.HotLinesBecomeDtiRecords")
{
    const std::string Hot = "ASHOW corr_1 1 2 3 4\n"
                            "ASHOW NONE 0 0 0 0\n"
                            "ALIEN xg 3 7 10 20 30 ; a grunt\n"
                            "PICKUP sw_inter 1 2 3\n"
                            "HIDEPT 4 5 6 7\n"
                            "CONNECT 9 1 2 3 4 5 6\n"
                            "MSWAP 1 2 3 4 5 6\n"
                            "UNKNOWN 1";
    auto records = BetaDemo::ParseRecords(Hot, {"ARENA_1", "CORR_1"});
    REQUIRE(7 == records.size());
    CHECK(std::tuple(1u, 1, std::string("CORR_1")) == std::tuple(records[0].Type, records[0].Id, records[0].Name));
    CHECK(Vector3(3.0f, 4.0f, 0.0f) == records[0].BoxEnd);
    CHECK(-1 == records[1].Id);
    CHECK(std::tuple(2u, 3, std::string("XG"), Vector3(10.0f, 20.0f, 30.0f)) == std::tuple(records[2].Type, records[2].Id, records[2].Name, records[2].Position));
    CHECK(std::tuple(4u, std::string("SW_INTER")) == std::tuple(records[3].Type, records[3].Name));
    CHECK(std::tuple(5u, 4, Vector3(5.0f, 6.0f, 7.0f)) == std::tuple(records[4].Type, records[4].Id, records[4].Position));
    CHECK(std::tuple(6u, 9) == std::tuple(records[5].Type, records[5].Id));
    CHECK(Vector3(1.0f, 3.0f, 5.0f) == records[5].Position);
    CHECK(Vector3(2.0f, 4.0f, 6.0f) == records[5].BoxEnd);
    CHECK(103u == records[6].Type);
}

TEST_CASE("BetaDemoTests.TeleportsAreLinesOfArenaAndPosition")
{
    auto teleports = BetaDemo::ParseTeleports("arena_4 1 2 -29\nbad line\nolym_1 0 0 5 ; tel0\n");
    std::vector<std::pair<std::string, Vector3>> expected = {{"ARENA_4", Vector3(1.0f, 2.0f, -29.0f)}, {"OLYM_1", Vector3(0.0f, 0.0f, 5.0f)}};
    CHECK(expected == teleports);
}

TEST_CASE("BetaDemoTests.WorldsHaveLongNamesAndShortNodes")
{
    auto arena = Arena::ParseBetaWorld("HMO_1", FloorWorld());
    CHECK(std::vector<std::string>{"WALL"} == arena->Materials);
    REQUIRE(arena->Nodes.size() == 1);
    auto& node = arena->Nodes[0];
    CHECK(std::tuple(Vector3::UnitZ(), -1, -1, 0, 2, 0) == std::tuple(node.Normal, node.Negative, node.Positive, node.FrontFirst, node.FrontCount, node.BackCount));
    CHECK(2 == arena->TriangleCount());
    CHECK(std::vector<int>{0, 2, 3} == std::vector<int>(arena->TriangleIndices.begin() + 3, arena->TriangleIndices.end()));
    CHECK(NotDrawn == arena->TriangleFlags[0]);
    CHECK(Vector3(-100.0f, 100.0f, 0.0f) == arena->Vertices[3]);
    CHECK(0 == BetaDemo::CheckNodes(*arena));
}

TEST_CASE("BetaDemoTests.NodesWhoseTrianglesAreOffTheirPlaneAreReported")
{
    CHECK(1 == BetaDemo::CheckNodes(*Arena::ParseBetaWorld("HMO_1", FloorWorld(5.0f))));
}

TEST_CASE("BetaDemoTests.ArchivesHaveNoHeaderAndLowerCaseNames")
{
    auto archive = TextureArchive::Parse(Archive("brick"), static_cast<int>(sizeof(uint)), 0, TextureArchive::Names::Upper);
    CHECK(5 == archive->Colors.At("PEN"));
    CHECK(std::vector<byte>{0, 1, 0, 2} == archive->Textures.At("BRICK")->Indices);
}

TEST_CASE("BetaDemoTests.IndexZeroIsBlackButInProjectiles")
{
    std::pair<std::string, std::vector<byte>> data[] = {{"brick", {16, 1, 16, 2}}, {"bolt", {0, 1, 0, 2}}};
    for (auto& [name, expected] : data)
    {
        CAPTURE(name);
        auto archive = TextureArchive::Parse(Archive(name), static_cast<int>(sizeof(uint)), 0, TextureArchive::Names::Upper);
        BetaDemo::MakeOpaque(*archive);
        CHECK(expected == archive->Textures.At(Str::ToUpper(name))->Indices);
    }
}

TEST_CASE("BetaDemoTests.SoundArchivesHaveTheirCountAfterTheSize")
{
    Bytes b;
    b.U32(0).U32(1).Name("shot", 12).U16(1).U16(100);
    auto offset = b.Position();
    b.U32(0).U32(4);
    b.Patch(offset, b.Position() - static_cast<int>(sizeof(uint)));
    auto sni = Sni::ParseBeta(Share(b.Raw(std::vector<byte>{'R', 'I', 'F', 'F'}).ToArray()));
    REQUIRE(sni->Entries.size() == 1);
    auto& [name, entry] = sni->Entries[0];
    CHECK(std::tuple(std::string("SHOT"), 1, 100) == std::tuple(name, entry.Flags, entry.Volume));
    CHECK(sni->IsSound(entry));
}

TEST_CASE("BetaDemoTests.AnimationsCarryADeltaForEveryFrame")
{
    // 1 track, 2 frames: offsets, root motion, boxes, no reference points, then the track.
    constexpr int TrackOffset = 8 + 4 + 2 * 12 + 2 * 24 + 4;
    Bytes b;
    b.U32(1, 2, TrackOffset).Vec(Vector3::Zero()).Vec(Vector3::UnitX()).Raw(std::vector<byte>(2 * 24)).U32(0);
    b.Name("body", 12).U32(1).F32(0.5f).Vec({1.0f, 2.0f, 3.0f}).U8(2, 0xFE, 4);
    auto animation = ModelAnimation::ParseBeta("@0", Share(b.ToArray()), 0);
    CHECK(2 == animation->FrameCount);
    CHECK(Vector3::UnitX() == animation->RootMotion[1]);
    Model model;
    auto part = std::make_shared<Model::Part>();
    part->Name = "BODY";
    part->Vertices = {Vector3::Zero()};
    model.PartList = {part};
    auto frames = animation->Bake(model);
    CHECK(Vector3(1.0f, 2.0f, 3.0f) == frames[0][0][0]);
    CHECK(Vector3(2.0f, 1.0f, 5.0f) == frames[1][0][0]);
}

TEST_CASE("BetaDemoTests.SpritesAddedOverTheRetailOnesReplaceThem")
{
    Bytes b;
    b.U32(0, 1).Name("SC_STAT", 12);
    auto offset = b.Position();
    b.U32(0);
    b.Patch(offset, b.Position() - static_cast<int>(sizeof(uint)));
    b.U16(1).U16(1).U8(7);
    auto index = ArchiveIndex::Read(Share(b.ToArray()), 12);
    index->Add("SC_STAT", std::vector<byte>{1, 0, 1, 0, 9});
    CHECK(std::vector<byte>{1, 0, 1, 0, 9} == index->GetBytes("SC_STAT"));
}

TEST_CASE("BetaDemoTests.SkiesAreRemappedToTheNearestColours")
{
    std::vector<byte> from(Palette::Size * 3);
    std::vector<byte> to(Palette::Size * 3);
    from[1 * 3] = 250;
    to[7 * 3] = 255;
    to[8 * 3 + 1] = 255;
    CHECK(std::vector<byte>{0, 7} == BetaDemo::Remap({0, 1}, *Palette::FromRgb(from), *Palette::FromRgb(to)));
}

TEST_CASE("BetaDemoTests.CmiArenasHaveMusicAndAmbienceButNoScript")
{
    auto cmi = Cmi::ParseBeta(Share(MakeCmi().Bytes));
    CHECK(ScriptDialect::Beta1996 == cmi->Dialect());
    CHECK("SONG_ACTIVE" == cmi->ArenaMusic.At("ARENA_1"));
    CHECK("AMB1" == cmi->ArenaAmbience.At("ARENA_1"));
    CHECK(0 == cmi->ArenaScripts.At("ARENA_1"));
    CHECK(std::vector<int>{MakeCmi().Script} == cmi->EntryPoints());
}

TEST_CASE("BetaDemoTests.PathsBecomeSplinesThroughEveryFrame")
{
    constexpr int KeySize = 40;
    auto [bytes, script, path] = MakeCmi();
    auto cmi = Cmi::ParseBeta(Share(bytes));
    REQUIRE(cmi->BetaPaths.Count() == 1);
    auto spline = *cmi->BetaPaths.begin();
    CHECK(std::pair(path, static_cast<int>(bytes.size())) == std::pair(spline.first, spline.second));

    // The positions, then back to the first.
    auto s = spline.second;
    auto& data = *cmi->Bytes();
    CHECK(4u == Bin::U32(data, s));
    auto key = [&](int k, int field)
    {
        return Vector3(Bin::F32(data, s + 8 + k * KeySize + field * 12), Bin::F32(data, s + 12 + k * KeySize + field * 12), Bin::F32(data, s + 16 + k * KeySize + field * 12));
    };
    std::vector<Vector3> expected = {{1.0f, 2.0f, 3.0f}, {2.0f, 2.0f, 3.0f}, {2.0f, 3.0f, 3.0f}, {1.0f, 2.0f, 3.0f}};
    std::vector<Vector3> keys;
    for (auto k = 0; k < 4; k++)
    {
        keys.push_back(key(k, 0));
    }

    CHECK(expected == keys);
    CHECK(2 == Bin::S32(data, s + 4 + 2 * KeySize));
    CHECK(Vector3::UnitX() == key(1, 1));
    CHECK(Vector3::UnitY() == key(1, 2));

    auto ins = ScriptDecoder::For(*cmi)->Decode(script);
    REQUIRE(ins != nullptr);
    CHECK(std::pair(BetaOpcodes::FollowPath, s) == std::pair(ins->Opcode, std::get<int>(ins->Operands[0])));
}

BETA_TEST_CASE("BetaDemoTests.TheDemoLevelsLoadAndTheirScriptsDecode")
{
    for (auto& [level, arenas, aliens, instructions, paths] : Levels)
    {
        CheckLevel(level, arenas, aliens, instructions, paths);
    }
}
