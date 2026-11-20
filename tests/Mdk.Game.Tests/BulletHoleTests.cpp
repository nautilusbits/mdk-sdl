#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "Asserts.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/BulletHoles.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Scripts;
using Mdk::Formats::Model;
using Mdk::Formats::Texture;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Bullet holes stamped into textures (special_130, stamp_bullet_hole).

namespace
{
    constexpr int Size = 8;
    constexpr byte Wall = 7;
    constexpr byte Dark = 3;

    Texture Plain(int width, int height, byte index)
    {
        Texture texture;
        texture.Width = width;
        texture.Height = height;
        texture.Indices = std::vector<byte>(static_cast<size_t>(width * height), index);
        return texture;
    }
}

TEST_CASE("BulletHoleTests.HoleIsCentredOnTheTexelAndKeepsItsTransparentPixels")
{
    auto texture = Plain(Size, Size, Wall);
    // A 3 x 3 hole whose corners are transparent.
    Texture hole;
    hole.Width = 3;
    hole.Height = 3;
    hole.Indices = {0, Dark, 0, Dark, Dark, Dark, 0, Dark, 0};
    BulletHoles::Stamp(texture, Vector2(4.f, 4.f), hole);
    CHECK(Dark == texture.Indices[4 * Size + 4]);
    CHECK(Dark == texture.Indices[3 * Size + 4]);
    CHECK(Wall == texture.Indices[3 * Size + 3]);
    CHECK(5 == std::count(texture.Indices.begin(), texture.Indices.end(), Dark));
}

TEST_CASE("BulletHoleTests.HoleWrapsAroundTheEdges")
{
    auto texture = Plain(Size, Size, Wall);
    auto hole = Plain(2, 2, Dark);
    BulletHoles::Stamp(texture, Vector2::Zero(), hole);
    // Centred on (0, 0): from (-1, -1) to (0, 0).
    CHECK(Dark == texture.Indices[(Size - 1) * Size + Size - 1]);
    CHECK(Dark == texture.Indices[0]);
}

TEST_CASE("BulletHoleTests.NearestTexturedFaceGivesTheWeights")
{
    Model::Part part;
    part.TriangleIndices = {0, 1, 2, 3, 4, 5};
    part.TriangleMaterials = {0, 1};
    std::vector<Vector3> vertices = {Vector3(0.f, 0.f, 0.f), Vector3(1.f, 0.f, 0.f), Vector3(0.f, 1.f, 0.f),
        Vector3(0.f, 0.f, 5.f), Vector3(1.f, 0.f, 5.f), Vector3(0.f, 1.f, 5.f)};
    Vector3 point(0.5f, 0.25f, 4.5f);
    auto face = BulletHoles::Nearest(part, vertices, point, [](int) { return true; });
    REQUIRE(face.has_value());
    CHECK(1 == face->Triangle);
    CHECK_NEAR(0.25f, face->Weights.X, 1e-4f);
    CHECK_NEAR(0.5f, face->Weights.Y, 1e-4f);
    CHECK_NEAR(0.25f, face->Weights.Z, 1e-4f);

    // The upper face isn't textured: the lower one is taken.
    auto lower = BulletHoles::Nearest(part, vertices, point, [](int m) { return m == 0; });
    REQUIRE(lower.has_value());
    CHECK(0 == lower->Triangle);
}

DATA_TEST_CASE("BulletHoleTests.SniperHitStampsTheObjectsTexture")
{
    auto& data = *GameData::Data();
    auto level = std::make_shared<LevelData>(data, 7);
    auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL7/LEVEL7.CMI"));
    auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
    auto space = std::make_shared<ArenaSpace>();
    AudioDevice device(Output::Muted);
    auto mixer = std::make_shared<SoundMixer>(device, [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
    auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
    auto runtime = std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    auto obj = runtime->Spawn(*runtime->GetArenaState("DANT_1")->Controller, "SW_H150", Vector3(-11.f, -13.f, 0.5f), 0.f, -1, 0,
        ScriptRuntime::Spawning::Plain);
    REQUIRE(obj != nullptr);
    Texture* stamped = nullptr;
    runtime->TextureStamped = [&stamped](Texture& texture) { stamped = &texture; };

    obj->ShotPart = 1;
    obj->ShotPoint = obj->Position + obj->Pose()[0][0] * obj->Scale;
    runtime->StampBulletHole(*obj);

    REQUIRE(stamped != nullptr);
    auto hole = sprites->GetImage(std::string(BulletHoles::Hole));
    REQUIRE(hole != nullptr);
    auto opaque = std::find_if(hole->Indices.begin(), hole->Indices.end(), [](byte i) { return i != 0; });
    REQUIRE(opaque != hole->Indices.end());
    CHECK(std::find(stamped->Indices.begin(), stamped->Indices.end(), *opaque) != stamped->Indices.end());
}
