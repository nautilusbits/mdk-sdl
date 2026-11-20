#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <doctest.h>
#include <json.hpp>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/Snapshot.h"

using namespace Mdk;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;

// Full saves' packing of objects (snapshot.gd): fields, references between objects and to the
// arenas' script objects survive a round trip through JSON text.
namespace
{
    constexpr const char* ControllerKey = "arena:HMO_1";

    std::shared_ptr<MdkObject> Alien(const std::string& type)
    {
        auto obj = std::make_shared<MdkObject>();
        obj->TypeName = type;
        obj->Arena = "HMO_1";
        return obj;
    }

    std::shared_ptr<Formats::ModelAnimation> NoAnimation(MdkObject*, const std::string&) { return nullptr; }
}

TEST_CASE("SnapshotTests.ObjectsComeBackAsTheyWere")
{
    auto controller = Alien("");
    auto leader = Alien("XG");
    auto follower = Alien("XS");
    leader->Position = Vector3(1.5f, -2.0f, 300.25f);
    leader->Health = 42;
    leader->Variables[2] = 7.5f;
    leader->GosubReturns.insert(leader->GosubReturns.end(), {10, 20});
    leader->AttachPoints = {1, 3};
    leader->DoorSounds = {"OPEN", "", "", "SHUT"};
    leader->RollingBasis = Matrix4x4::CreateRotationZ(0.5f);
    leader->MaxSpeed = std::numeric_limits<float>::infinity();
    follower->Leader = leader;
    follower->Linked = controller;

    Dictionary<std::string, std::shared_ptr<MdkObject>> fixedObjects{{ControllerKey, controller}};
    Snapshot packer({leader, follower}, fixedObjects, NoAnimation);
    auto text = nlohmann::json::array({packer.Pack(*leader), packer.Pack(*follower)}).dump();

    // Into new objects, as a load creates them.
    auto leader2 = Alien("XG");
    auto follower2 = Alien("XS");
    Snapshot unpacker({leader2, follower2}, fixedObjects, NoAnimation);
    auto data = nlohmann::json::parse(text);
    unpacker.Unpack(*leader2, data[0]);
    unpacker.Unpack(*follower2, data[1]);

    CHECK(leader2->Position == leader->Position);
    CHECK(leader2->Health == 42);
    CHECK(leader2->Variables[2] == 7.5f);
    CHECK(leader2->GosubReturns == std::vector<int>{10, 20});
    CHECK(leader2->AttachPoints == MdkObject::PointPair{1, 3});
    CHECK(leader2->DoorSounds[3] == "SHUT");
    CHECK(leader2->RollingBasis == leader->RollingBasis);
    CHECK(leader2->MaxSpeed == std::numeric_limits<float>::infinity());
    CHECK(follower2->Leader == leader2);
    CHECK(follower2->Linked == controller);
    CHECK(nlohmann::json::array({unpacker.Pack(*leader2), unpacker.Pack(*follower2)}).dump() == text);
}

TEST_CASE("SnapshotTests.HashIsStable")
{
    CHECK(Snapshot::Hash("{\"a\":1}") == Snapshot::Hash("{\"a\":1}"));
    CHECK(Snapshot::Hash("{\"a\":1}") != Snapshot::Hash("{\"a\":2}"));
}
