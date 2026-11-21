#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/FrameBlend.h"
#include "game/kurt/Kurt.h"
#include "game/objects/MdkObject.h"
#include "game/objects/RopeView.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Objects;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Objects' rope lines (update_ropes): a swing's rope, or opcode 242's lines.
namespace
{
    const Vector3 Pivot{10.0f, 20.0f, 50.0f};
    constexpr float Half = 0.5f;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }
}

TEST_CASE("RopeTests.SwingRopeGoesFromAboveTheObjectToThePivot")
{
    MdkObject obj;
    obj.Position = Vector3(10.0f, 20.0f, 0.0f);
    obj.RopeMask = 1;
    obj.RopePoints[0] = obj.Position;
    obj.RopePoints[1] = Pivot;
    CHECK(RopeView::LinesOf(obj) == std::vector<Vector3>{Vector3(10.0f, 20.0f, 5.0f), Pivot});
}

TEST_CASE("RopeTests.AllLinesSkipZeroPointsAndStartAtTheObjectWithoutModel")
{
    MdkObject obj;
    obj.Position = Vector3::UnitX();
    obj.RopeMask = RopeView::AllLines;
    obj.RopePoints[1] = Pivot;

    // Without a model a reference point is the object's position.
    CHECK(RopeView::LinesOf(obj) == std::vector<Vector3>{Vector3::UnitX(), Pivot});
}

TEST_CASE("RopeTests.RopesHangFromTheObjectAsDrawn")
{
    // A swing between ticks: its rope starts at the object as the frame draws it, not where
    // the tick left it.
    MdkObject obj;
    obj.Arena = "A";
    obj.RopeMask = 1;
    obj.Remember();
    obj.Position = Vector3(10.0f, 0.0f, 0.0f);
    obj.RopePoints[0] = obj.Position;
    obj.RopePoints[1] = Pivot;

    ArenaSpace space;
    SoundMixer mixer(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
    KurtPlayer kurt(space, mixer, [](auto) { return 1; });
    FrameBlend blend(Blending::On);
    blend.Update(kurt, 1.0f, Half);

    CHECK(RopeView::LinesOf(obj, blend) == std::vector<Vector3>{Vector3(5.0f, 0.0f, 5.0f), Pivot});
}

TEST_CASE("RopeTests.NoMaskNoLines")
{
    MdkObject obj;
    CHECK(RopeView::LinesOf(obj).empty());
}
