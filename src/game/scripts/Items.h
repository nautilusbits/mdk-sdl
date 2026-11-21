#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Dictionary.h"
#include "core/Numerics.h"
#include "formats/Model.h"
#include "game/kurt/Inventory.h"

namespace Mdk::Formats
{
    class Bni;
    class ModelAnimation;
}

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;
    class Twister;

    // Kurt's thrown items (0x46ce78 throws, 0x43deac updates) and blasts (0x463a94). A port of
    // godot-mdk's items.gd (see its docs/engine.md "Kurt's items").
    //
    //   use ──► thrown (pickup model, 25 u/s forward, 15 up) ──lands, hits, times out──► active:
    //     grenade: blast │ decoy: walks, aliens aim at it │ bomb: spins, then blast │ tornado: twisters
    //     mortar: thumps │ key: the nuke │ seal, bone: pickups again
    class Items
    {
    public:
        using Item = Kurt::Inventory::Item;

        // Whether a blast's kills count for the statistics.
        enum class Kills { Counted, Ignored };

    private:
        // C#'s (int Best, float Distance, Vector3 Point, int Event) tuple.
        struct WeakPartHit
        {
            int Best = 0;
            float Distance = 0.0f;
            Vector3 Point;
            int Event = 0;
        };

        // C#'s (int Damage, float Distance) tuple.
        struct BlastShare
        {
            int Damage = 0;
            float Distance = 0.0f;
        };

    public:
        Items(ScriptRuntime& runtime, std::shared_ptr<Formats::Bni> sprites) : _runtime(&runtime), _sprites(std::move(sprites)) {}

        // Kurt's thrown items and effects (0x1000), and active ones (0x4000).
        static constexpr int FlagThrown = 0x1000;
        static constexpr int FlagActive = 0x4000;

    private:
        // A thrown item: Kurt's effect, not a target, not solid for Kurt, gravity and collisions.
        static constexpr int ThrownFlags = 0x818a6;
        // Contact flag of an item that hit an object (0x43eb48).
        static constexpr int ContactHitObject = 0x10;

    public:
        // The XE's bombs (rides.gd's bomber): a grenade that noses down by 45°/s to -90°.
        static constexpr int KindBomb = 0x81;

    private:
        static constexpr float BombPitchRate = 45.0f;
        static constexpr float BombPitchMin = -90.0f;

        // Pickup models of the item types.
        static constexpr std::array<std::string_view, 10> Models = {"", "SW_DUMMY", "SW_INTER", "SW_TWIST", "SW_THUMP", "SW_HBOMB", "SW_GATT", "SW_KEY", "SW_SEAL", "SW_SBONE"};
        // Thrown items fly this fast (grenades 3 times faster), 15 u/s upwards; 4 units above
        // Kurt's feet; scale 0.1 growing by 3 per second.
        static constexpr float ThrowSpeed = 25.0f;
        static constexpr float ThrowUpSpeed = 15.0f;
        static constexpr float GrenadeSpeedFactor = 3.0f;
        static constexpr float ThrowHeight = 4.0f;
        static constexpr float ThrowScale = 0.1f;
        static constexpr float GrowRate = 3.0f;
        // Flight ticks: 150, seals and bones 750.
        static constexpr int FlightTicks = 150;
        static constexpr int LongFlightTicks = 750;
        static constexpr float SealRollRate = 30.0f;

        // Hit types (obj+0x21d, if_hit_weapon).
        static constexpr int HitMortar = -3;
        static constexpr int HitGrenade = -7;
        static constexpr int HitNuke = -8;
        static constexpr int HitBomb = -9;
        // Blast targets: Kurt, objects, triangle groups.
        static constexpr int TargetKurt = 1;
        static constexpr int TargetObjects = 2;
        static constexpr int TargetGroups = 4;
        static constexpr int TargetWorld = TargetObjects | TargetGroups;

        // Grenades: 150 within 40 (75 on Kurt), explosion ×2.
        static constexpr int GrenadeDamage = 150;
        static constexpr int GrenadeKurtDamage = 75;
        static constexpr float GrenadeRadius = 40.0f;
        static constexpr float GrenadeExplosion = 2.0f;
        // The decoy walks forward at 5 u/s for 450 ticks.
        static constexpr int DecoyTicks = 450;
        static constexpr float DecoySpeed = 5.0f;
        // The bomb spins at 235°/s for 600 ticks, then blasts 450 within 80 (67 on Kurt), explosion ×3.
        static constexpr int BombTicks = 600;
        static constexpr float BombSpin = 235.0f;
        static constexpr int BombDamage = 450;
        static constexpr int BombKurtDamage = 67;
        static constexpr float BombRadius = 80.0f;
        static constexpr float BigExplosion = 3.0f;
        // The tornado spins at 360°/s for 60 ticks, a twister at once and every 15 ticks from 45 left.
        static constexpr int TornadoTicks = 60;
        static constexpr int TornadoFirstTwister = 45;
        static constexpr int TornadoTwisterEvery = 15;
        static constexpr float TornadoSpin = 360.0f;
        // The nuke: 900 ticks, from frame 1; white from frame 70; doors within 50 blown open;
        // blast of 200 within 60 (19 on Kurt).
        static constexpr int NukeTicks = 900;
        static constexpr int NukeWhiteFrame = 70;
        static constexpr float NukeShake = 3.0f;
        static constexpr float NukeDoorRangeSquared = 2500.0f;
        static constexpr int NukeDoorOpen = 0x80;
        static constexpr int DoorStateKeep = 0x1F;
        static constexpr int NukeDamage = 200;
        static constexpr int NukeKurtDamage = 19;
        static constexpr float NukeRadius = 60.0f;
        // The mortar thumps on these frames of SW_THUMP (0x491e4c): 4 damage to the arena's
        // aliens (+5 u/s up when standing), the screen shakes; from the 4th Kurt is knocked down.
        static constexpr std::array<int, 15> MortarThumps = {28, 54, 64, 71, 77, 82, 87, 92, 97, 102, 107, 112, 117, 122, 127};
        static constexpr std::array<std::string_view, 2> MortarSpared = {"XE", "XF"};
        static constexpr int MortarActiveTicks = 30;
        static constexpr int MortarDamage = 4;
        static constexpr float MortarLift = 5.0f;
        static constexpr float MortarShake = 5.0f;
        static constexpr int MortarKnockThump = 4;
        static constexpr int MortarIgnored = 0x1030;
        // Seals and bones become pickups again after 150 ticks (750 - 600); seals then shrink.
        static constexpr int SealPickupTicks = 601;
        static constexpr float SealShrink = 0.9f;
        static constexpr float SealGone = 0.1f;
        // Script flags of seals and bones: something holds it, it has landed.
        static constexpr int SealHeld = 1;
        static constexpr int SealLanded = 2;
        // Blasts lift their centre 1 unit off the floor; Kurt's damage is at most 15, at double the distance.
        static constexpr float BlastLift = 1.0f;
        static constexpr int BlastKurtMax = 15;
        static constexpr float BlastKurtDistance = 2.0f;
        // The decoy's and bomb's animations, the nuke's model and the seal's landing.
        static constexpr std::string_view DecoyThrown = "SW_DUM_I";
        static constexpr std::string_view DecoyWalk = "SW_DUM_M";
        static constexpr std::string_view BombAnimation = "SW_INTER";
        static constexpr std::string_view MortarAnimation = "SW_THUMP";
        static constexpr std::string_view Nuke = "SW_NUKE";
        static constexpr std::string_view SealLanding = "XMT_LAND";

    public:
        // The World's Most Interesting Bomb (0x573c24), and the decoy (0x573c20), while out.
        std::shared_ptr<Objects::MdkObject> Bomb;
        std::shared_ptr<Objects::MdkObject> Decoy;

    private:
        Dictionary<std::string, std::shared_ptr<Formats::ModelAnimation>> _animations;
        std::vector<std::shared_ptr<Twister>> _twisters;

    public:
        const std::vector<std::shared_ptr<Twister>>& Twisters() const { return _twisters; }

        // An item that goes before it's done (its arena put away).
        void Forget(Objects::MdkObject& obj);

        // The items' animations the game plays (a level's load bakes them).
        std::vector<std::shared_ptr<Formats::ModelAnimation>> Animations();

        // SW_H150's run and idle (ObjectBehaviors).
        static constexpr std::string_view HamsterRun = "H150_R";
        static constexpr std::string_view HamsterIdle = "H150_I";

        // A model animation of the items (TRAVSPRT.BNI: SW_INTER, SW_DUM_I, H150_R...).
        std::shared_ptr<Formats::ModelAnimation> GetAnimation(const std::string& name);

        // Whether Kurt may use an item: one thrown item at a time (decoys, seals and bones
        // excepted), and none while the bomb is out ("use" then sets it off).
        bool CanUse(Item item);

    private:
        static bool IsDecoyKind(Item item);

    public:
        // "Use" while the bomb is out: it goes off at the end of its animation (0x43f258).
        void TriggerBomb();

        // Throws the selected item (0x46ce78): its pickup model leaves Kurt 4 units up, along his yaw.
        void UseItem();

        // A thrown item after it moved (0x43deac): it flies until it lands, hits or times out.
        void UpdateThrown(Objects::MdkObject& obj);

    private:
        // Grenades stop on any alien, the others only on objects with flag 0x1000000 (0x43eb48).
        bool HitsObject(Objects::MdkObject& obj);

        // An item landed (or hit something, or its time ran out): it stops and does its thing.
        void Activate(Objects::MdkObject& obj);

        // A grenade's or the bomb's blast: on the world, then on Kurt, an explosion and its sound.
        void Explode(Objects::MdkObject& obj, int damage, int kurtDamage, float radius, int hitType, Objects::MdkObject* source, float scale);

        // The "key" becomes the nuke (SW_NUKE): a white flash, its animation, then the blast.
        void StartNuke(Objects::MdkObject& obj);

        // Active items (0x43e860 decoy, 0x43f18c bomb, 0x43e690 mortar, 0x43efcc nuke, 0x43e980
        // seal, 0x43ea84 bone, 0x43ee98 tornado).
        void UpdateActive(Objects::MdkObject& obj);

        void UpdateTornado(Objects::MdkObject& obj);

        void UpdateBomb(Objects::MdkObject& obj);

    public:
        // Moves the twisters of Kurt's arena (one tick).
        void UpdateTwisters();

    private:
        // The mortar (0x43e690) pounds the ground on the frames of MortarThumps: the
        // screen shakes and every alien of the arena (flying ones spared) loses 4 health, thrown up if it
        // stands. From the fourth thump on, Kurt is knocked down if he stands on a floor. It blows up at the end.
        void UpdateMortar(Objects::MdkObject& obj);

        // The nuke (0x43efcc) shakes the screen while its animation plays (turning white towards
        // the end), then blows open the doors nearby and blasts everything within 60 units.
        void UpdateNuke(Objects::MdkObject& obj);

        // A thrown seal or bone (0x43e980, 0x43ea84) plays XMT_LAND once it lands. Unless something
        // has got hold of it (script flag 1), Kurt can take it back after 5 seconds; a seal shrinks away after 25.
        void UpdateSeal(Objects::MdkObject& obj);

    public:
        // Whether a blast's kills count for the statistics.

        // A blast (0x463a94) of damage within radius:
        // targets 1 Kurt, 2 objects, 4 triangle groups. Damage falls off with the
        // distance to a target's box (less half its size), and walls stop it.
        void Blast(Vector3 center, int damage, float radius, int targets, int hitType, Objects::MdkObject* source, Kills kills = Kills::Counted);

    private:
        void BlastObjects(Vector3 center, int damage, float radius, int hitType, Objects::MdkObject* source, Kills kills);

        // A blast on an object's weak parts: the best damage, its distance and point, and the
        // event of a part it destroyed (part + 1, or 0).
        WeakPartHit BlastWeakParts(Objects::MdkObject& obj, Vector3 center, int damage, float radius, Kills kills);

        // Kurt is hurt at double the distance (walls stop it), at most 15; blasts knock him down
        // twice as easily.
        void BlastKurt(Vector3 center, int damage, float radius);

        // A blast on the triangle groups that react to hits: each gets one hit, on the first of
        // its triangles within the radius that the blast reaches, of the damage less the falloff.
        void BlastGroups(Vector3 center, int damage, float radius, int hitType, Kills kills);

        // A blast's damage on a weak part: Kurt's blasts (counted kills) follow onehit.
        int BlastHit(Objects::MdkObject& obj, int damage, int health, Kills kills);

        // Damage of a blast on a box (0x463958) and its distance: to the box's centre, less half
        // the box's size; 0 beyond the radius or behind a wall.
        BlastShare BlastDamage(const Formats::Box& box, Vector3 center, int damage, float radius);

        ScriptRuntime* _runtime;
        std::shared_ptr<Formats::Bni> _sprites;
    };
}
