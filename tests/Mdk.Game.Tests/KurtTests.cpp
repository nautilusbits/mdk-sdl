#include <doctest.h>

#include "Asserts.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Formats/Dti.h"
#include "Mdk.Formats/Mto.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Shared/GameData.h"

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using Mdk::Game::Kurt::Difficulty;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;
using State = KurtPlayer::State;

// Kurt's hits (hurt_kurt 0x46a604), knock-downs and death, standing on level 3's start pad.
namespace
{
    constexpr int Level = 3;
    constexpr float Step = 1.f / 60.f;
    // Frames of every animation: a knock-down then takes 10 ticks.
    constexpr int Frames = 5;
    // The flashes fade by 4 a tick (0x41d460).
    constexpr float FlashFadePerTick = 4.f;
    // Above the pad (the Godot port's Kurt stands at z 192).
    const Vector3 Pad(-4.f, 0.f, 195.f);
    constexpr int SilentSamples = 1000;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ArenaSpace> CreateSpace()
    {
        auto& data = *GameData::Data();
        auto number = std::to_string(Level);
        auto dti = Formats::Dti::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".DTI"));
        auto mto = Formats::Mto::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + "O.MTO"));
        auto space = std::make_shared<ArenaSpace>();
        space->Add(mto->GetArena(dti->Arenas[dti->StartArena].Name));
        return space;
    }

    // Level 3's start arena, read on first use (C#'s static field).
    const std::shared_ptr<ArenaSpace>& Space()
    {
        static auto space = CreateSpace();
        return space;
    }

    std::shared_ptr<SoundMixer> Mixer(std::optional<SoundMixer::Entry> entry = std::nullopt)
    {
        return std::make_shared<SoundMixer>(Device(), [entry](const std::string&) { return entry; });
    }

    // A sound looping nothing at full volume.
    SoundMixer::Entry Silence()
    {
        auto sound = std::make_shared<Sound>(std::vector<float>(SilentSamples), 1, 1, Looping::Once);
        return {sound, SoundMixer::FullVolume};
    }

    // Kurt with the space and mixer he holds by reference (Kurt destroyed first).
    struct Rig
    {
        std::shared_ptr<ArenaSpace> Space;
        std::shared_ptr<SoundMixer> Mixer;
        std::shared_ptr<KurtPlayer> Kurt;

        KurtPlayer* operator->() const { return Kurt.get(); }
        KurtPlayer& operator*() const { return *Kurt; }
    };

    Rig Make(std::shared_ptr<ArenaSpace> space, std::shared_ptr<SoundMixer> mixer, Vector3 feet)
    {
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](State) { return Frames; });
        kurt->Feet = feet;
        return {space, mixer, kurt};
    }

    void Run(KurtPlayer& kurt, Input& input, float seconds)
    {
        for (float t = 0.f; t < seconds; t += Step)
        {
            kurt.Update(input, Step);
        }
    }

    void Run(KurtPlayer& kurt, float seconds)
    {
        Input idle;
        Run(kurt, idle, seconds);
    }

    void Tick(KurtPlayer& kurt)
    {
        Input idle;
        kurt.Update(idle, Step);
    }

    // Kurt standing on the pad.
    Rig Standing(Difficulty difficulty = Difficulty::Normal)
    {
        auto rig = Make(Space(), Mixer(), Pad);
        rig->Inventory->Difficulty = difficulty;
        Run(*rig, 1.f);
        CHECK(rig->OnFloor);
        return rig;
    }

    // A fan's updraft of 10 u/s from z 0 to 20 (fans.gd type 6), Kurt in the air above no arena.
    Rig InFan(Vector3 feet, std::function<float(float, float)> updraft)
    {
        auto rig = Make(std::make_shared<ArenaSpace>(), Mixer(), feet);
        rig->Updraft = std::move(updraft);
        return rig;
    }

    // Kurt on the pad, a tap of jump: up in the air.
    Rig Jumped()
    {
        auto rig = Standing();
        Input jump;
        jump.Hold(Key::Jump, Input::State::Down);
        Run(*rig, jump, 2 * Step);
        CHECK(rig->Current == State::Jump);
        return rig;
    }

    // How Kurt leaves the open chute without closing it.
    enum class ChuteExit { Teleport, Ride, LevelEnd };

    // Kurt under the open chute with CHUTEON flapping, above `feet`.
    Rig UnderChute(std::shared_ptr<ArenaSpace> space, Vector3 feet, Input& jump)
    {
        auto rig = Make(space, Mixer(Silence()), feet);
        rig->VerticalSpeed = -20.f;
        jump.Hold(Key::Jump, Input::State::Down);
        Run(*rig, jump, 0.5f);
        CHECK(rig.Mixer->IsPlaying("CHUTEON"));
        return rig;
    }
}

DATA_TEST_CASE("KurtTests.DamageIsScaledByDifficulty")
{
    struct Row { Difficulty Difficulty; int Damage; int Taken; };
    const Row rows[] = {
        {Difficulty::Easy, 9, 6},
        {Difficulty::Easy, 1, 1},
        {Difficulty::Normal, 9, 9},
        {Difficulty::Hard, 9, 18},
    };

    for (const auto& row : rows)
    {
        auto kurt = Standing(row.Difficulty);
        kurt->Hurt(row.Damage);
        CHECK(kurt->Health == KurtPlayer::MaxHealth - row.Taken);
    }
}

DATA_TEST_CASE("KurtTests.InvulnerableKurtTakesNoDamage")
{
    auto kurt = Standing();
    kurt->Invulnerable = 1.f;
    kurt->Hurt(10);
    CHECK(kurt->Health == KurtPlayer::MaxHealth);
    CHECK(kurt->KnockDamage == 0.f);
}

DATA_TEST_CASE("KurtTests.KnockDamageAddsUpAndDrains")
{
    auto kurt = Standing();
    kurt->Hurt(2);
    kurt->Hurt(2);
    CHECK(kurt->KnockDamage == 4.f);

    // It drains by 2 per second.
    Run(*kurt, 0.5f);
    CHECK_NEAR(3.f, kurt->KnockDamage, 0.1f);
    CHECK(kurt->Current == State::Still);
}

DATA_TEST_CASE("KurtTests.FiveDamageKnocksKurtDown")
{
    auto kurt = Standing();
    kurt->Hurt(3);
    kurt->Hurt(2);
    Tick(*kurt);
    CHECK(kurt->Current == State::Knocked);
    CHECK(kurt->Invulnerable > 0.f);

    // Knocked down he takes no damage, then he gets up.
    kurt->Hurt(10);
    CHECK(kurt->Health == KurtPlayer::MaxHealth - 5);
    Run(*kurt, 1.f);
    CHECK(kurt->Current == State::Still);
}

DATA_TEST_CASE("KurtTests.KnockedDownKurtStopsFiring")
{
    auto kurt = Standing();
    Input input;
    input.Hold(Key::Fire, Input::State::Down);
    kurt->Update(input, Step);
    CHECK(kurt->Firing);
    CHECK(kurt->Current == State::Shot);

    kurt->KnockDown(Vector2::Zero());
    kurt->Update(input, Step);
    CHECK_FALSE(kurt->Firing);
}

DATA_TEST_CASE("KurtTests.KurtDiesAndTheSkullFadesIn")
{
    auto kurt = Standing();
    bool died = false;
    kurt->Died = [&died] { died = true; };
    kurt->Hurt(KurtPlayer::MaxHealth);
    Tick(*kurt);
    CHECK(kurt->Current == State::Dead);

    // The skull fades in by 2 per tick up to 255: about 4 seconds.
    Run(*kurt, 5.f);
    CHECK(died);
}

DATA_TEST_CASE("KurtTests.KurtRisesInAnUpdraft")
{
    constexpr float Lift = 10.f;
    constexpr float Acceleration = 64.f;
    KurtPlayer* self = nullptr;
    auto kurt = InFan(Vector3(0.f, 0.f, 5.f), [&self](float vz, float dt)
    {
        return self->Feet.Z < 20.f ? MathF::Min(vz + (Lift + Acceleration) * dt, Lift) : std::numeric_limits<float>::quiet_NaN();
    });
    self = kurt.Kurt.get();

    Run(*kurt, 1.f);

    CHECK(kurt->InUpdraft);
    CHECK(kurt->ChuteOpen);
    CHECK(kurt->Feet.Z > 5.f);
}

DATA_TEST_CASE("KurtTests.KurtLeavesAnUpdraftAtMost40")
{
    auto kurt = InFan(Vector3(0.f, 0.f, 100.f), [](float, float) { return std::numeric_limits<float>::quiet_NaN(); });
    kurt->VerticalSpeed = 60.f;

    Tick(*kurt);

    CHECK_FALSE(kurt->InUpdraft);
    CHECK(kurt->VerticalSpeed <= KurtPlayer::UpdraftExitSpeed);
}

DATA_TEST_CASE("KurtTests.TheChuteOpensFromAJumpsTop")
{
    // Jump pressed again at the top of a jump opens the chute there (0x4646a4), not only once Kurt
    // falls faster than 16 u/s, which left him hanging in the jump's last frame about 0.4 s.
    auto kurt = Jumped();
    while (kurt->VerticalSpeed > 0.f)
    {
        Tick(*kurt);
    }

    Input jump;
    jump.Hold(Key::Jump, Input::State::Down);
    Run(*kurt, jump, 3 * Step);
    CHECK(kurt->ChuteOpen);
    CHECK(kurt->Current == State::Chute);
}

DATA_TEST_CASE("KurtTests.AJumpsFramesFollowItsArcThenItFalls")
{
    // After 10 timed frames, each frame waits for the vertical speed (0x492030): the last at -20 u/s,
    // then the fall. Played by time, the jump ran out while Kurt still rose.
    auto kurt = Jumped();
    float frame = 0.f;
    while (kurt->Current == State::Jump)
    {
        CHECK(kurt->AnimationFrame >= frame);
        frame = kurt->AnimationFrame;
        if (frame > 10.f)
        {
            CHECK_MESSAGE(kurt->VerticalSpeed <= -3.f, "frame " << frame << " at " << kurt->VerticalSpeed << " u/s");
        }

        Tick(*kurt);
    }

    CHECK(kurt->Current == State::Fall);
    CHECK(kurt->VerticalSpeed <= -20.f);
    CHECK(frame == 15.f);
}

DATA_TEST_CASE("KurtTests.ChuteFlapsWhileOpenAndClosesWithItsSound")
{
    auto kurt = Make(std::make_shared<ArenaSpace>(), Mixer(Silence()), Vector3(0.f, 0.f, 1000.f));
    kurt->VerticalSpeed = -20.f;
    auto& mixer = *kurt.Mixer;
    Input jump;
    jump.Hold(Key::Jump, Input::State::Down);

    // Open: CHUTEON once the opening frames are shown, looping.
    Run(*kurt, jump, 0.5f);
    CHECK(kurt->ChuteOpen);
    CHECK(mixer.IsPlaying("CHUTEON"));
    CHECK_FALSE(mixer.IsPlaying("CHUTEIN"));

    // Closed: CHUTEON stops, CHUTEIN plays.
    Run(*kurt, 2 * Step);
    CHECK_FALSE(kurt->ChuteOpen);
    CHECK_FALSE(mixer.IsPlaying("CHUTEON"));
    CHECK(mixer.IsPlaying("CHUTEIN"));
}

DATA_TEST_CASE("KurtTests.ChuteClosedInTheAirShowsItsClosingFrameThenFalls")
{
    auto kurt = Make(std::make_shared<ArenaSpace>(), Mixer(), Vector3(0.f, 0.f, 1000.f));
    kurt->VerticalSpeed = -20.f;
    Input jump;
    jump.Hold(Key::Jump, Input::State::Down);
    Run(*kurt, jump, 0.5f);
    CHECK(kurt->Current == State::Chute);

    // One tick of K_CHUTE's closing frame, then the fall.
    Tick(*kurt);
    CHECK(kurt->Current == State::Fall);
    CHECK(kurt->ChuteClosing);

    Tick(*kurt);
    CHECK_FALSE(kurt->ChuteClosing);
}

DATA_TEST_CASE("KurtTests.ChuteLoopStopsWhenKurtDies")
{
    // Bug: dead under the chute, CHUTEON played until the level ended.
    Input jump;
    auto kurt = UnderChute(Space(), Pad + Vector3(0.f, 0.f, 10.f), jump);
    kurt->SetHealth(0);
    Run(*kurt, jump, 3.f);

    CHECK(kurt->Current == State::Dead);
    CHECK_FALSE(kurt.Mixer->IsPlaying("CHUTEON"));
}

DATA_TEST_CASE("KurtTests.ChuteLoopStopsSilentlyOnOtherExits")
{
    for (auto exit : {ChuteExit::Teleport, ChuteExit::Ride, ChuteExit::LevelEnd})
    {
        CAPTURE(static_cast<int>(exit));
        Input jump;
        auto kurt = UnderChute(std::make_shared<ArenaSpace>(), Vector3(0.f, 0.f, 1000.f), jump);
        switch (exit)
        {
            case ChuteExit::Teleport:
                kurt->Teleport(Vector3(0.f, 0.f, 500.f), 0.f);
                break;
            case ChuteExit::Ride:
                kurt->Ride = [](Input&, float) {};
                break;
            case ChuteExit::LevelEnd:
                kurt->Frozen = true;
                break;
        }

        Tick(*kurt);
        CHECK_FALSE(kurt.Mixer->IsPlaying("CHUTEON"));
        CHECK_FALSE(kurt.Mixer->IsPlaying("CHUTEIN"));
    }
}

// The flashes fade by 4 a tick in cutscenes too (0x478704): LEVEL5's last nuke frees
// Bones, whose 23 s cutscene stayed white.
DATA_TEST_CASE("KurtTests.FlashesFadeWhileFrozen")
{
    constexpr float WhiteMax = 255.f;
    auto kurt = Standing();
    kurt->Hurt(1);
    kurt->WhiteFlash = WhiteMax;
    kurt->Frozen = true;
    Run(*kurt, 1.f);
    CHECK(kurt->HurtFlash == 0.f);
    CHECK_NEAR(WhiteMax - FlashFadePerTick * KurtPlayer::Ticks, kurt->WhiteFlash, FlashFadePerTick);
}
