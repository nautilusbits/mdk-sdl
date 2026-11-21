#include <doctest.h>

#include "Asserts.h"
#include "core/DotNet.h"
#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "engine/platform/Input.h"
#include "formats/Dti.h"
#include "formats/Mto.h"
#include "game/Angles.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/FollowCamera.h"
#include "game/kurt/Inventory.h"
#include "game/kurt/Kurt.h"
#include "game/kurt/Scope.h"
#include "game/scripts/AirStrike.h"
#include "shared/GameData.h"

#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Formats;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using Mdk::Game::DegreesToRadians;
using Mdk::Game::Kurt::CameraMath;
using Mdk::Game::Kurt::Inventory;
using Mdk::Game::Kurt::Scope;
using Mdk::Game::Scripts::BezierPath;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Sniper mode (kurt.gd _update_zoom, _update_clip, select_ammo; sound_mixer.gd _scope_gain;
// follow_camera.gd _update_sniper_view; godot-mdk docs/gameplay.md "Sniper mode").
namespace
{
    constexpr float Tick = 1.f / 30.f;
    constexpr float Step = 1.f / 60.f;
    constexpr int Bullets = 0;
    constexpr int Homing = 1;
    constexpr int Grenades = 2;
    constexpr int Mortar = 4;

    // Level 3's start pad (the Godot port's Kurt stands at z 192).
    const Vector3 Pad(-4.f, 0.f, 195.f);
    constexpr int Level = 3;

    Inventory WithAmmo(std::initializer_list<const char*> pickups)
    {
        Inventory inventory;
        for (const char* pickup : pickups)
        {
            int health = Inventory::MaxHealth;
            inventory.Collect(pickup, health);
        }

        return inventory;
    }

    // Runs the clip for seconds; returns the shots fired.
    int RunClip(Scope& scope, Inventory& inventory, Scope::Trigger trigger, float seconds, std::function<bool(int)> fire = nullptr)
    {
        if (!fire)
        {
            fire = [](int) { return true; };
        }

        int shots = 0;
        for (float t = 0.f; t < seconds - 1e-4f; t += Step)
        {
            if (Any(scope.UpdateClip(inventory, trigger, fire, Step) & Scope::Clip::Fired))
            {
                shots++;
            }
        }

        return shots;
    }

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    // Kurt with the space and mixer he holds by reference (Kurt destroyed first).
    struct Rig
    {
        std::shared_ptr<ArenaSpace> Space;
        std::shared_ptr<SoundMixer> Mixer;
        std::shared_ptr<KurtPlayer> Kurt;

        KurtPlayer* operator->() const { return Kurt.get(); }
    };

    Rig StandingKurt()
    {
        const auto& data = *GameData::Data();
        std::string level = std::to_string(Level);
        auto dti = Dti::Load(data.PathOf("TRAVERSE/LEVEL" + level + "/LEVEL" + level + ".DTI"));
        auto mto = Mto::Load(data.PathOf("TRAVERSE/LEVEL" + level + "/LEVEL" + level + "O.MTO"));
        auto space = std::make_shared<ArenaSpace>();
        space->Add(mto->GetArena(dti->Arenas[dti->StartArena].Name));

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](KurtPlayer::State) { return 5; });
        kurt->Feet = Pad;
        for (float t = 0.f; t < 1.f; t += Step)
        {
            Input idle;
            kurt->Update(idle, Step);
        }

        CHECK(kurt->OnFloor);
        return {space, mixer, kurt};
    }
}

TEST_CASE("SniperTests.ZoomAcceleratesThenStopsAtTheLimit")
{
    Scope scope;

    // One tick zooming in: the speed is 0.01, the zoom divided by 1.01.
    scope.UpdateZoom(-1.f, Tick);
    CHECK_PRECISION(1.f / 1.01f, scope.Zoom, 4);

    for (int i = 0; i < 60; i++)
    {
        scope.UpdateZoom(-1.f, Tick);
    }

    CHECK_PRECISION(Scope::ZoomMin, scope.Zoom, 4);
    CHECK(Scope::Zooming::Still == scope.UpdateZoom(-1.f, Tick));
    CHECK(Scope::Zooming::Moving == scope.UpdateZoom(1.f, Tick));
}

TEST_CASE("SniperTests.ZoomStopsAtALockedTargetsLimit")
{
    Scope scope;
    scope.ZoomLimit = Scope::LockLimit(10.f, 300.f);
    CHECK_PRECISION(0.0125f, scope.ZoomLimit, 5);
    for (int i = 0; i < 200; i++)
    {
        scope.UpdateZoom(-1.f, Tick);
    }

    CHECK_PRECISION(scope.ZoomLimit, scope.Zoom, 5);

    // A near target doesn't let it go past 0.25.
    CHECK(Scope::ZoomMin == Scope::LockLimit(20.f, 10.f));
}

TEST_CASE("SniperTests.LosingALockKeepsTheZoom")
{
    // Zoomed past 0.25 on a locked target; the lock is lost (the limit is 0.25 again) while
    // zooming in: the zoom stays, it doesn't jump back out.
    Scope scope;
    scope.ZoomLimit = Scope::LockLimit(10.f, 300.f);
    for (int i = 0; i < 40; i++)
    {
        scope.UpdateZoom(-1.f, Tick);
    }

    float deep = scope.Zoom;
    CHECK(deep < Scope::ZoomMin);

    scope.ZoomLimit = Scope::ZoomMin;
    scope.UpdateZoom(-1.f, Tick);
    CHECK_PRECISION(deep, scope.Zoom, 5);

    // Zooming out still works (once the zoom-in speed is spent).
    for (int i = 0; i < 30; i++)
    {
        scope.UpdateZoom(1.f, Tick);
    }

    CHECK(scope.Zoom > deep);
}

TEST_CASE("SniperTests.WheelNotchZoomsLikeAHeldKey")
{
    Scope wheel;
    Scope keys;
    wheel.Wheel(1.f);
    for (int i = 0; i < 6; i++)
    {
        wheel.UpdateZoom(0.f, Tick);
        keys.UpdateZoom(-1.f, Tick);
    }

    CHECK_PRECISION(keys.Zoom, wheel.Zoom, 5);
    CHECK(wheel.Zoom < 1.f);
}

TEST_CASE("SniperTests.LookIsSlowerZoomedInAndPitchIsLimited")
{
    Scope wide;
    Scope narrow;
    narrow.Zoom = Scope::ZoomMin;
    float turnWide = 0.f;
    float turnNarrow = 0.f;
    for (int i = 0; i < 30; i++)
    {
        turnWide += wide.Look(1.f, 1.f, Vector2::Zero(), Scope::Turbo::Off, Tick);
        turnNarrow += narrow.Look(1.f, 1.f, Vector2::Zero(), Scope::Turbo::Off, Tick);
    }

    // Turning right lowers the yaw; the zoom scales it.
    CHECK(turnWide < 0.f);
    CHECK_PRECISION(turnWide * Scope::ZoomMin, turnNarrow, 3);
    CHECK(wide.Pitch > 0.f);

    wide.Look(0.f, 0.f, Vector2(0.f, 1000.f), Scope::Turbo::Off, Tick);
    CHECK(Scope::PitchLimit == wide.Pitch);
}

TEST_CASE("SniperTests.ClipLoadsInAQuarterSecondPerRoundAndFiresAQuarterSecondApart")
{
    Scope scope;
    Inventory inventory;

    // Entering: the timer is set to 3 and three rounds load; ready after 0.75 s.
    CHECK(0 == RunClip(scope, inventory, Scope::Trigger::Held, 0.7f));
    CHECK(Scope::ClipSize == scope.ClipRounds);
    CHECK(scope.ClipTime > 0.f);

    // Held fire: a shot every quarter second (the timer reloads the clip meanwhile).
    int shots = RunClip(scope, inventory, Scope::Trigger::Held, 1.05f);
    CHECK(4 == shots);
}

TEST_CASE("SniperTests.FullSlotsFireNothing")
{
    Scope scope;
    Inventory inventory;
    CHECK(0 == RunClip(scope, inventory, Scope::Trigger::Held, 2.f, [](int) { return false; }));
    CHECK(Scope::ClipSize == scope.ClipRounds);
}

TEST_CASE("SniperTests.ClipLoadsOnlyTheRoundsInStockAndFallsBackToBullets")
{
    Scope scope;
    Inventory inventory = WithAmmo({"SW_LGREN"});
    CHECK(Mortar == inventory.SelectedAmmo);
    int stock = inventory.Ammo()[Mortar - 1];

    RunClip(scope, inventory, Scope::Trigger::Released, 1.f);
    CHECK(Scope::ClipSize == scope.ClipRounds);
    int mortars = 0;
    RunClip(scope, inventory, Scope::Trigger::Held, 10.f, [&mortars](int type) { return type != Mortar || ++mortars > 0; });
    CHECK(stock == mortars);
    CHECK(0 == inventory.Ammo()[Mortar - 1]);

    // Empty, the clip reloads normal bullets.
    CHECK(Bullets == inventory.SelectedAmmo);
}

TEST_CASE("SniperTests.AmmoSelectionSkipsEmptyTypesAndReloads")
{
    Scope scope;
    scope.ClipRounds = Scope::ClipSize;
    Inventory inventory = WithAmmo({"SW_HOME", "SW_LGREN"});
    CHECK(Mortar == inventory.SelectedAmmo);

    scope.SelectAmmo(inventory, 1);
    CHECK(Bullets == inventory.SelectedAmmo);
    CHECK(0 == scope.ClipRounds);
    CHECK(scope.ClipTime > 0.f);

    scope.SelectAmmo(inventory, 1);
    CHECK(Homing == inventory.SelectedAmmo);
    scope.SelectAmmo(inventory, 1);
    CHECK(Mortar == inventory.SelectedAmmo);
    scope.SelectAmmo(inventory, -1);
    CHECK(Homing == inventory.SelectedAmmo);
    CHECK(Grenades != inventory.NextAmmo(1));
}

TEST_CASE("SniperTests.ScopeHearsTheCrosshairNotBehind")
{
    // On the line of sight, near: full; at 600 units: 300 / 600 x 1.3.
    CHECK(1.f == SoundMixer::ScopeGain(Vector3(0.f, 0.f, 50.f), 1.f));
    CHECK_PRECISION(0.65f, SoundMixer::ScopeGain(Vector3(0.f, 0.f, 600.f), 1.f), 4);
    // Zoomed in, the far sound is louder.
    CHECK(1.f == SoundMixer::ScopeGain(Vector3(0.f, 0.f, 600.f), Scope::ZoomMin));
    // Behind, or far off the line of sight: silent.
    CHECK(0.f == SoundMixer::ScopeGain(Vector3(0.f, 0.f, -10.f), 1.f));
    CHECK(0.f == SoundMixer::ScopeGain(Vector3(100.f, 0.f, 50.f), 1.f));
}

TEST_CASE("SniperTests.ScopeCentreIsOnTheLineOfSight")
{
    constexpr float Near = 0.5f;
    constexpr float Far = 1000.f;
    constexpr float Aspect = 4.f / 3.f;
    constexpr float Zoom = 0.5f;

    // A point straight ahead lands on y 279 of the 480-high screen: NDC y = -(279 - 240) / 240.
    Matrix4x4 projection = CameraMath::ScopeProjection(Zoom, Aspect, Near, Far);
    Vector4 ahead = Vector4::Transform(Vector4(0.f, 0.f, -100.f, 1.f), projection);
    CHECK_PRECISION(-39.f / 240.f, ahead.Y / ahead.W, 4);
    CHECK_PRECISION(0.f, ahead.X / ahead.W, 4);

    // A point 1 unit up at depth 384 / zoom shows 1 pixel higher.
    float depth = Scope::Focal / Zoom;
    Vector4 up = Vector4::Transform(Vector4(0.f, 1.f, -depth, 1.f), projection);
    CHECK_PRECISION((-39.f + 1.f) / 240.f, up.Y / up.W, 4);

    // The same on the 640x480 screen (the target lock's test).
    Vector3 eye(0.f, 0.f, 10.f);
    std::optional<Vector2> screen = Scope::ToScreen(eye, Vector3::UnitY(), Zoom, eye + Vector3(1.f, depth, 1.f));
    REQUIRE(screen.has_value());
    CHECK(Scope::Centre() + Vector2(1.f, -1.f) == *screen);
    CHECK_FALSE(Scope::ToScreen(eye, Vector3::UnitY(), Zoom, eye - Vector3::UnitY()).has_value());
    CHECK(Scope::Locks(Scope::Centre() + Vector2(30.f, 30.f), Scope::Centre() + Vector2(40.f, 40.f)));
    CHECK_FALSE(Scope::Locks(Scope::Centre() + Vector2(33.f, 0.f), Scope::Centre() + Vector2(40.f, 1.f)));
}

TEST_CASE("SniperTests.EyeMovesForwardLookingDown")
{
    Scope scope;
    CHECK(Vector3(0.f, 0.f, 4.f) == scope.Eye(Vector3::Zero(), Vector3::UnitX()));
    scope.Pitch = 60.f;
    CHECK_PRECISION(2.5f, scope.Eye(Vector3::Zero(), Vector3::UnitX()).X, 4);
    CHECK_PRECISION(-MathF::Sin(DegreesToRadians(60.f)), scope.Forward(0.f).Z, 4);
}

TEST_CASE("SniperTests.StrikePathGoesThroughItsPoints")
{
    std::vector<Vector3> points{Vector3(0.f, 0.f, 100.f), Vector3(50.f, 0.f, 60.f), Vector3(100.f, 0.f, 40.f), Vector3(150.f, 0.f, 60.f), Vector3(200.f, 0.f, 100.f)};
    BezierPath path(points);
    CHECK(path.Length() > 200.f);
    CHECK(points[0] == path.Sample(0.f));
    CHECK(points.back() == path.Sample(path.Length()));
    float over = path.ClosestOffset(points[2]);
    CHECK_PRECISION(points[2].X, path.Sample(over).X, 1);
    CHECK_PRECISION(points[2].Z, path.Sample(over).Z, 1);
}

DATA_TEST_CASE("SniperTests.KurtSnipesOnlyOnTheFloorAndAKnockDownEndsIt")
{
    Rig kurt = StandingKurt();
    Input input;
    input.Hold(Key::Sniper, Input::State::Down);
    kurt->Update(input, Step);
    CHECK(kurt->Sniping);

    // He only sidesteps.
    input.Hold(Key::Sniper, Input::State::Up);
    input.Hold(Key::Forward, Input::State::Down);
    Vector3 feet = kurt->Feet;
    for (int i = 0; i < 30; i++)
    {
        kurt->Update(input, Step);
    }

    CHECK_PRECISION(feet.X, kurt->Feet.X, 3);
    CHECK_PRECISION(feet.Y, kurt->Feet.Y, 3);
    CHECK(kurt->Scope->Pitch < 0.f);

    kurt->KnockDown(Vector2::Zero());
    CHECK_FALSE(kurt->Sniping);
}

DATA_TEST_CASE("SniperTests.SniperKeyLeavesSniperMode")
{
    Rig kurt = StandingKurt();
    kurt->EnterSniper();
    CHECK(kurt->Sniping);
    Input input;
    input.Hold(Key::Sniper, Input::State::Down);
    kurt->Update(input, Step);
    CHECK_FALSE(kurt->Sniping);
}

DATA_TEST_CASE("SniperTests.WheelZoomsInsteadOfPickingAmmo")
{
    Rig kurt = StandingKurt();
    int health = Inventory::MaxHealth;
    kurt->Inventory->Collect("SW_HOME", health);

    // The sniper key, then a wheel notch before the next step: it already zooms.
    Input input;
    input.Hold(Key::Sniper, Input::State::Down);
    kurt->Update(input, Step);
    CHECK(kurt->Sniping);
    int ammo = kurt->Inventory->SelectedAmmo;
    float zoom = kurt->Scope->Zoom;

    input.AddWheel(1.f);
    kurt->Update(input, Step);
    CHECK(ammo == kurt->Inventory->SelectedAmmo);
    CHECK(zoom != kurt->Scope->Zoom);
}

DATA_TEST_CASE("SniperTests.ReboundWheelDoesntZoomRightAfterTheSniperKey")
{
    Rig kurt = StandingKurt();
    Input input;
    CHECK(input.Bind(Key::Jump, "Wheel up"));
    input.Hold(Key::Sniper, Input::State::Down);
    kurt->Update(input, Step);
    CHECK(kurt->Sniping);
    float zoom = kurt->Scope->Zoom;

    input.AddWheel(1.f);
    CHECK(0.f == input.ZoomWheel());
    CHECK(input.IsDown(Key::Jump));
    kurt->Update(input, Step);
    CHECK(zoom == kurt->Scope->Zoom);
}
