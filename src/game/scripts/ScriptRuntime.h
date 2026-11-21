#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <json.hpp>

#include "core/Dictionary.h"
#include "core/DotNet.h"
#include "core/Numerics.h"
#include "formats/Dti.h"
#include "formats/Model.h"
#include "formats/scripts/ScriptDecoder.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/Solids.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/Debris.h"
#include "game/scripts/GameStats.h"
#include "game/scripts/PairHash.h"
#include "game/scripts/RayHit.h"
#include "game/scripts/StrikeScene.h"

namespace Mdk::Formats
{
    class Arena;
    class Bni;
    class Cmi;
    class ModelAnimation;
    class Texture;
}

namespace Mdk::Game::Collision
{
    class ArenaSpace;
    class Bsp;
}

namespace Mdk::Game::Hud
{
    class Messages;
}

namespace Mdk::Game::Kurt
{
    class Kurt;
}

namespace Mdk::Game::Level
{
    class LevelData;
    class TriangleGroups;
}

namespace Mdk::Game::Scripts
{
    class AirStrike;
    class AnimatedTextures;
    class EndLevel;
    class Effects;
    class Fans;
    class Items;
    class ObjectBehaviors;
    class ObjectMotion;
    class Rides;
    class ScriptVm;
    class Snapshot;
    class SniperRounds;

    // How hard Kurt's hits are: as the original's, or all an enemy's health (the console's onehit).
    enum class Lethality { Normal, OneHit };

    // Runs the level's scripts: arena scripts (for the arena Kurt is in), the objects they
    // spawn and the aliens placed by the DTI, at the original's 30 ticks per second. A port of
    // godot-mdk's script_runtime.gd (see its docs/scripts.md and docs/engine.md).
    //
    //   tick ─► Kurt's arena? ─► triggers, arenas ─► arena scripts ─► subsystems
    //        └─► objects of the live arenas: door ─► script (ScriptVm) ─► motion (ObjectMotion)
    class ScriptRuntime
    {
        using MdkObject = Game::Objects::MdkObject;
        using Operand = Formats::Scripts::Operand;
        using OperandList = Formats::Scripts::OperandList;

        // ScriptRuntime.cs
    public:
        static constexpr float Tick = 1.0f / 30.0f;
        static constexpr float TicksPerSecond = 30.0f;
        // Flags of objects spawned by spawn_flagged and of projectiles (fire).
        static constexpr int SpawnFlaggedFlags = 0x2008A6;
        static constexpr int ProjectileFlags = 0x80820;
        // Health from which objects are indestructible.
        static constexpr int Indestructible = 65000;

    private:
        // Chain gun (0x41a304): damage per tick, reach beyond the target's size, and the reach
        // of shots that hit nothing.
        static constexpr int ChainGunDamage = 1;
        static constexpr int SuperChainGunFactor = 6;
        static constexpr float ChainGunReach = 140.0f;
        static constexpr float ChainGunWallReach = 150.0f;
        static constexpr float SuperChainGunPush = 20.0f;
        // Objects with more hit points than this don't show the health bar.
        static constexpr int MaxBarHealth = 900;
        // Ticks before the minecrawler reaches the level's town, per difficulty (45, 30 and 20 minutes).
        static constexpr std::array<int, 3> TownTicksByDifficulty = {81000, 54000, 36000};
        // Index in the order of play of the last level, which has no town.
        static constexpr int LastLevelIndex = 5;

    public:
        // Global flags: the minecrawler went for the second town (set by the game), the first
        // or the second town was flattened (tested by the debriefing).
        static constexpr int FlagSecondTown = 1 << 31;
        static constexpr int FlagTownFlattened = 1 << 30;
        static constexpr int FlagSecondTownFlattened = 1 << 29;

    private:
        static constexpr float TownShake = 5.0f;
        static constexpr float TownMessageSeconds = 5.0f;

    public:
        // Cutscene states (0x573c60, set by special_event): 0 = none.
        static constexpr int CutsceneDog = 0x34;
        static constexpr int CutsceneAll = 0x3d;
        static constexpr int CutsceneStrike = 0x47;
        static constexpr int CutsceneEnd = 0x51;
        static constexpr int CutsceneAllFlagged = 0x5b;
        static constexpr int CutsceneBoss = 0x5d;

    private:
        // The only objects that run in cutscenes below CutsceneAll (0x47868c).
        static constexpr std::array<std::string_view, 6> CutsceneTypes = {"XM5_FLAP", "XBN", "BOLT", "BIGBOLT", "SW_SBONE", "SW_SEAL"};
        // Events up to this end the level; higher ones are cutscenes.
        static constexpr int LastEndEvent = 50;
        // Flags of the objects hidden in cutscenes from CutsceneAll (pickups, Kurt's items).
        static constexpr int CutsceneHiddenFlags = 0x201000;
        static constexpr int CameraBlendTicks = 1800;

        // The holy cow (0x46d718): dropped from 100 units above a target within 600, blowing up
        // 0.5 s after landing; flags 0x40000806 (gravity, collisions, Kurt passes through, the cow).
        static constexpr float CowHeight = 100.0f;
        static constexpr float CowRange = 600.0f;
        static constexpr float CowFuse = 0.5f;
        static constexpr int CowFlags = MdkObject::FlagGravity | MdkObject::FlagCollides | MdkObject::FlagNotSolid2 | MdkObject::FlagCow;

        // DTI records: trigger boxes (show an arena, load one ahead, 0x41bf1c), aliens, static
        // objects (pickups, 0x43bd38), cover spots, waypoints, wind zones, connections.
        static constexpr uint TriggerShow = 1;
        static constexpr uint DtiAlien = 2;
        static constexpr uint TriggerLoad = 3;
        static constexpr uint DtiStatic = 4;
        static constexpr uint DtiCover = 5;
        static constexpr uint DtiWaypoint = 8;
        static constexpr uint DtiWind = 9;
        // Connections (DTI type 6, 0x41c550): the doorway reaches this far below its floor
        // (0x4945c8), a hatch's plane is this far below its z (0x4945d0).
        static constexpr float DoorwayDrop = 5.0f;
        static constexpr float HatchDrop = 0.5f;
        static constexpr int DtiStaticFlags = MdkObject::FlagPickup | MdkObject::FlagNotSolid2 | MdkObject::FlagNoBanking | MdkObject::FlagNotTarget;
        static constexpr std::string_view NoArena = "NONE";

    public:
        // Sparks (0x41e8f4): on objects (green, blue without gore: 0x41e919), on indestructible
        // objects and walls (grey, half as fast), on groups that react to the hit (orange), and fire.
        enum class Spark { Flesh, Hard, Group, Fire };

    private:
        // Palette colours (base, range) of each kind.
        struct SparkColour
        {
            int Base;
            int Range;
        };

        static constexpr std::array<SparkColour, 4> SparkColours = {{{3, 3}, {0x25, -16}, {10, 3}, {0x30, 0x10}}};
        static constexpr SparkColour SparkFleshNoGore = {13, 3};
        static constexpr float SparkSize = 0.5f;
        static constexpr float SparkSlow = 0.5f;
        static constexpr int FireSparks = 16;
        static constexpr std::array<std::string_view, 3> Ricochets = {"RICO1", "RICO2", "RICO3"};
        // Object explosions: the white flash (1000 / distance, up to 150), the slime drops
        // (scale 5-6.6), the pitch towards the camera 5 units above it (3 for other explosions) unless
        // it's within 5 units across and 8 up or down.
        static constexpr float ExplosionFlash = 1000.0f;
        static constexpr float ExplosionFlashMax = 150.0f;
        static constexpr int GoreDrops = 32;
        static constexpr float GoreScale = 5.0f;
        static constexpr float ExplosionLift = 5.0f;
        static constexpr float DefaultLift = 3.0f;
        static constexpr float ExplosionNear = 5.0f;
        static constexpr float ExplosionNearHeight = 8.0f;
        static constexpr int ExplosionFrames = 26;
        static constexpr float ExplosionSizeFactor = 1.5f;

    public:
        static constexpr std::string_view ExplodeSound = "EXPLODE";

    private:
        static constexpr float TeleportFlash = 255.0f;

    public:
        // Kinds of hits on triangle groups (0x40d560), matched against their hit flags and masks.
        static constexpr int HitShot = 1;
        static constexpr int HitChainGun = 2;
        static constexpr int HitBlast = 3;
        static constexpr int HitOtherBlast = 4;
        // Kurt running into a group (damp_collide_move 0x46634e): kind 8, hit type -11, no damage.
        static constexpr int HitKurt = 8;
        static constexpr int HitTypeKurt = -11;

    private:
        // Group hit flags (group_set_hit_flags): stops the hit, always runs, destructible.
        static constexpr int GroupStopsHit = 0x20;
        static constexpr int GroupAlwaysHit = 0x40;

    public:
        static constexpr int GroupDestructible = 0x80;
        static constexpr int GroupCount = 16;
        // group_set_state operations of destructible groups: hidden, then shown when hit.
        static constexpr int GroupHide = 2;

    private:
        static constexpr int GroupShow = 3;

        // Animations stored in the CMI file are named after their offset (CMI_1a2b).
        static constexpr std::string_view CmiAnimationPrefix = "CMI_";
        static constexpr int AnimationNameLength = 8;
        // The arena camera pitch when the DTI has none (degrees).
        static constexpr float DefaultCameraPitch = 4.0f;
        // Kurt's box (MDK): half width and height, from his feet.
        static constexpr float KurtHalfWidth = 0.6f;
        static constexpr float KurtHeight = 5.0f;
        // Kurt's eyes and objects' middles, above their origins, for lines of sight.
        static constexpr float EyeHeight = 5.0f;
        static constexpr float KurtChest = 4.0f;
        static constexpr float TargetHeight = 2.0f;
        static constexpr float NoArenaFloor = -1000.0f;

    public:
        // Per arena script state (the arena's embedded object).
        class ArenaState
        {
        public:
            ArenaState(std::string name, int script);

            std::string Name;
            std::shared_ptr<MdkObject> Controller;
            // The object group hit scripts run in (the original's scratch object 0x57fc40).
            std::shared_ptr<MdkObject> HitScripts;
            std::array<float, 4> Variables{};
            int Flags = 0;
            bool Started = false;
            // Per triangle group (1-16): hit behaviour (group_set_hit_flags), hit kinds that run
            // the group's hit script and that script (group_on_hit), and counters (arena+0xcc).
            std::array<int, GroupCount> GroupHitFlags{};
            std::array<int, GroupCount> GroupHitMasks{};
            std::array<int, GroupCount> GroupHitScripts{};
            std::array<int, GroupCount> GroupCounters{};
        };

        // A ray's hit: point, plane normal, arena and triangle group.
        using RayHit = Scripts::RayHit;

        std::shared_ptr<Game::Level::LevelData> Level;
        std::shared_ptr<Formats::Cmi> Cmi;
        std::shared_ptr<Game::Kurt::Kurt> Kurt;
        std::shared_ptr<Audio::SoundMixer> Mixer;
        std::shared_ptr<Scripts::ScriptVm> Vm;
        std::shared_ptr<Scripts::ObjectMotion> Motion;
        std::shared_ptr<Scripts::ObjectBehaviors> Behaviors;
        std::shared_ptr<Scripts::Effects> Effects;
        std::shared_ptr<Scripts::Debris> Debris;
        // Sniper rounds, and the object locked in the scope (0x573a8c).
        std::shared_ptr<Scripts::SniperRounds> SniperRounds;
        std::shared_ptr<MdkObject> SniperTarget;
        std::shared_ptr<Scripts::AirStrike> AirStrike;
        // The full-screen strike playing, if any (the game waits meanwhile).
        std::shared_ptr<Scripts::StrikeScene> Strike;
        std::shared_ptr<Scripts::Rides> Rides;
        std::shared_ptr<Scripts::Items> Items;
        std::shared_ptr<Scripts::Fans> Fans;
        std::shared_ptr<Scripts::GameStats> Stats = std::make_shared<Scripts::GameStats>();
        // The end of the level's tornado, once it started.
        std::shared_ptr<Scripts::EndLevel> EndLevel;
        // On-screen messages (hud_message), set by the game.
        std::shared_ptr<Hud::Messages> Messages;
        // rand(); seeded by the soak test (its runs repeat), otherwise random.
        std::shared_ptr<Random> Rng;

        // The point and direction of the last shatter_group (0x4d5374, 0x4d5358).
        Vector3 ShatterPoint;
        Vector3 ShatterDirection = Vector3::UnitZ();
        std::array<float, 4> GlobalVariables{};
        int GlobalFlags = 0;
        // Kurt's position and the target of alien scripts (Kurt, or a decoy).
        Vector3 KurtPosition;
        Vector3 TargetPosition;
        // Yaw of the target and of Kurt (degrees).
        float TargetYaw = 0.0f;
        float KurtYaw = 0.0f;
        // Kurt's velocity (u/s), measured over the last tick.
        Vector3 KurtVelocity;
        // The camera, which explosions turn to (fed by the game).
        Vector3 Eye;
        // The object aliens aim at instead of Kurt (0x491e48, set_target_mode 1).
        std::shared_ptr<MdkObject> AlienTarget;
        // Ticks the alarm keeps sounding (0x573aec), set by objects with movement command 15.
        int AlarmTicks = 0;
        // How the sky is drawn (0x574304, opcode 202): 0 normally, 1 black, -1 not drawn (SkyModes).
        int SkyMode = 0;
        // Gore (if_option, 0x5742dc): 1 on, 0 off.
        int Option = 1;
        // The strongest screen shake asked this tick (raise_573aa8).
        float Shake = 0.0f;
        // A screen shake was asked (the follow camera shakes).
        std::function<void(float)> ShakeRaised;
        // Frames of arena textures set by arena_texture_frame (opcode 133).
        std::shared_ptr<Scripts::AnimatedTextures> AnimatedTextures;

        // The health bar (0x573c74 seconds left, 0x41e3c8): its object (0x573c78), or values;
        // drawn by Hud/HudView.
        float BarTime = 0.0f;
        std::shared_ptr<MdkObject> BarObject;
        int BarHealth = 0;
        int BarMax = 0;

        int Cutscene = 0;
        std::shared_ptr<MdkObject> CutsceneTarget;
        // The cutscene camera (0x599920...0x599940): shot, yaw (90° - heading), pitch, distance
        // to the target, stored position and the blend timer in ticks; the game looks from
        // CameraPoint (CutsceneCamera).
        int CameraMode = 0;
        float CameraYaw = 0.0f;
        float CameraPitch = 0.0f;
        float CameraDistance = 0.0f;
        Vector3 CameraPosition;
        int CameraTimer = 0;
        Vector3 CameraPoint;

        // The level is over (0x573b60).
        bool LevelOver = false;
        enum class GameOver { No, Yes };

        enum class StrikeState { Started, Ended };

        // A level ended (special_event up to 50), or the whole game (event 81).
        std::function<void(GameOver)> LevelEnded;
        std::function<void()> GameFinished;
        // The full-screen strike started or ended: the game is paused but not the music.
        std::function<void(StrikeState)> StrikeSceneChanged;

        // Ticks left before the minecrawler flattens the town (0x574270); none on the last level.
        int TownTicks = 0;
        // The time left for the town (0x574270 over 0x574274), 1 without one.
        float TownTimeLeft() const;

    private:
        // The town's whole time; saved, since saves have no difficulty.
        int _townTotal = 0;

    public:
        std::string CurrentArena;
        // The second arena (0x573a68): behind an open door, the one Kurt just left, or one loaded
        // ahead; when active (0x573a6c) its objects and script run too.
        std::string SecondArena;
        bool SecondActive = false;
        // The arenas drawn (Kurt's and the active second) last tick.
        const std::vector<std::string>& DrawnArenas() const { return _drawnArenas; }

        // The arenas Kurt collides with: the drawn ones, not the second on the snowboard (0x465e34).
        const std::vector<std::string>& SolidArenas();

    private:
        // On the board, Kurt's arena alone (kept: no list per step).
        std::vector<std::string> _solidFirst;

    public:
        std::vector<std::shared_ptr<MdkObject>> Objects;
        // camera_track (opcode 203): the camera pitch the scripts want (0x573918), for this
        // many more ticks (0x5739b0).
        float CameraTrackPitch = 0.0f;
        int CameraTrackTicks = 0;
        // A script made an arena reachable (a teleport into it).
        std::function<void(const std::string&)> ArenaEntered;

    private:
        std::shared_ptr<Collision::ArenaSpace> _space;
        std::shared_ptr<Formats::Bni> _sprites;
        std::shared_ptr<Game::Level::TriangleGroups> _groups;
        Dictionary<std::string, std::shared_ptr<Collision::Bsp>> _bsps;
        Dictionary<std::string, float> _floors;
        // The arena at the other end of each connection: (arena, record id) → arena.
        Dictionary<std::pair<std::string, int>, std::string, PairHash> _connections;
        Dictionary<std::string, std::shared_ptr<ArenaState>> _arenas;
        Dictionary<int, std::shared_ptr<Formats::ModelAnimation>> _animations;
        std::vector<std::shared_ptr<MdkObject>> _boxes;
        // The voices following each object: they stop when it's removed.
        Dictionary<MdkObject*, std::vector<int>> _following;
        // The voice lists of removed objects, reused.
        std::stack<std::vector<int>> _spareVoiceLists;
        std::vector<std::string> _loadedArenas;
        std::vector<std::string> _drawnArenas;
        // The camera's arena is looked for from this high above Kurt's feet (0x4940f0).
        static constexpr float CameraLineHeight = 3.0f;
        // The one arena (not corridor) loaded (_g_current_arena); its corridors are loaded too.
        std::string _residentArena;
        int _nextInstance = 1000;
        Vector3 _previousKurtPosition;
        // A teleport into an arena set the previous position before the first tick.
        bool _teleported = false;
        float _time = 0.0f;
        int _tickCount = 0;

    public:
        ScriptRuntime(std::shared_ptr<Game::Level::LevelData> level, std::shared_ptr<Formats::Cmi> cmi, std::shared_ptr<Formats::Bni> sprites,
            std::shared_ptr<Collision::ArenaSpace> space, std::shared_ptr<Game::Level::TriangleGroups> groups,
            std::shared_ptr<Audio::SoundMixer> mixer, std::shared_ptr<Game::Kurt::Kurt> kurt, std::optional<int> seed = std::nullopt);

        // Ticks run so far.
        int TickCount() const { return _tickCount; }

        // Runs the whole ticks that delta seconds complete.
        void Update(float delta);

        // How far a frame ahead seconds after the last step is from the
        // last tick to the next (0-1).
        float TickBlend(float ahead);

        std::shared_ptr<ArenaState> GetArenaState(const std::string& arena);

        // Group of the floor triangle below Kurt (0x573c10).
        int GetKurtFloorGroup();

        // The target of an object's script (script_run): Kurt, or the decoy while it walks, or
        // the aliens' target (opcode 251), unless the object always targets Kurt.
        void SelectTarget(MdkObject& obj);

        // Whether a sound (by name) is playing anywhere (if_sound_playing).
        bool IsSoundPlaying(const std::string& name);

    private:
        // An arena Kurt may only reach by a teleport is shown and made solid (level.gd enter_arena).
        void EnterArena(const std::string& arena);

    public:
        // Moves Kurt (teleport_player): within the arena with a white flash when
        // arena is empty, otherwise into that arena.
        void TeleportKurt(const std::string& arena, Vector3 position, float yaw);

        // Shuts every door (the soak tour, before it teleports): a door left open freezes with
        // its arena, and later closes onto an arena no longer drawn; walking away shuts it first.
        void ShutDoors();

    private:
        // The second arena after a teleport (0x41bce4): none into an arena; into a corridor
        // not loaded, the last arena (DTI order) leading to it (CDANT_1 → DANT_2), loaded ahead; into
        // a loaded one (of the loaded arena, or Kurt's pair), unchanged. Kurt's show then activates it.
        void TeleportSecond(const std::string& arena, const std::string& from);

        // Corridors connected to the loaded arena are loaded with it (0x419ac0); so are the
        // arena Kurt was in and the second.
        bool IsCorridorLoaded(const std::string& corridor, const std::string& from);

        // Whether two lists hold the same names in the same order.
        static bool Same(const std::vector<std::string>& a, const std::vector<std::string>& b);

        // The triangle groups of his arena that Kurt ran into this tick get a hit (0x46634e;
        // how the snowboard breaks the ice walls).
        void KurtTouchesGroups();

    public:
        // Shows an arena (BSPShow 0x41a11c, opcode 100): it becomes the active second arena
        // (unless it's Kurt's), and the first time its DTI aliens appear. "" or NONE clears it.
        void ShowArena(const std::string& arena);

        // Loads an arena ahead (arena_set_neighbour 0x41a2d0, opcode 223): the second arena, not active.
        void PreloadArena(const std::string& arena);

    private:
        // Loads an arena (arena_load 0x419d00): only one arena (not corridor) is loaded at a time.
        void LoadArena(const std::string& arena);

        // An arena being loaded takes the doors leading to it from its neighbours (arena_load
        // 0x419d00), except from Kurt's arena and the active second one: e.g. loading DANT_2 from
        // CDANT_2 moves the door CDANT_1 → DANT_2 into DANT_2, where Kurt will meet it.
        void PullDoors(const std::string& arena);

        // Only Kurt's arena and the active second one are drawn, with their objects (0x41e344).
        // An arena in neither slot any more is put away (0x419cb0: its objects' loop sounds stop, Kurt's
        // thrown items there go), and started again when it comes back (arena_activate 0x43f8e0).
        void UpdateArenas();

    public:
        // The drawn arena the camera is in (0x490db4, camera_update): Kurt's, or the second one
        // when the line from above Kurt's feet to the eye leaves Kurt's through a
        // doorway into it. It's drawn over the other (0x41df35).
        std::string CameraArena(Vector3 eye);

        // Whether an arena's objects run: Kurt's, and the second one when it's active.
        bool IsLiveArena(const std::string& arena);

    private:
        // The trigger boxes of Kurt's arena (DTI records 1 and 3, 0x41bf1c): walking into one
        // shows (1) or loads ahead (3) the arena of its id; an id of -1 clears the second arena.
        void CheckTriggers();

        // Whether a move crosses a box's edge (XY).
        static bool Crosses(Vector2 min, Vector2 max, Vector2 from, Vector2 to);

        static bool SegmentsCross(Vector2 a, Vector2 b, Vector2 c, Vector2 d);

        // Which way a move goes through a connection to leave the arena (the DTI record's
        // angle field, an integer): across its x, y or z plane, or across the XY line from its first
        // corner to the other, ending on its left or right (unused by the levels).
        enum class Doorway { MinusX, PlusX, MinusY, PlusY, Left, Right, MinusZ, PlusZ };

    public:
        // An object with FlagChangesArena goes into the arena whose
        // connection its last move crossed (0x45e810, 0x43ca00): the board follows Kurt, so its
        // script keeps running (LEVEL4's first run ends in MEAT_3).
        void FollowArenas(MdkObject& obj);

    private:
        // The arena a move enters when it crosses a connection of arena
        // (0x41c550, every tick), or "". E.g. LEVEL4 MEAT_7 1012 (+y at y = 14822) → CMEAT_7.
        std::string CrossedArena(const std::string& arena, Vector3 from, Vector3 to);

        // Whether a move goes out through a doorway: across its plane in its direction, the
        // move within (or across) the doorway on the other axes. A horizontal doorway's box reaches
        // DoorwayDrop below it; the diagonal ones only test where the move ends.
        static bool CrossesDoorway(const Formats::Dti::Record& record, Vector3 from, Vector3 to);

        // Whether a move from a to b on one axis is within [min, max] or crosses it.
        static bool Spans(float a, float b, float min, float max);

        std::vector<Formats::Dti::Record> ArenaRecords(const std::string& arena);

        // Each arena's DTI records, by name (found once).
        Dictionary<std::string, std::vector<Formats::Dti::Record>> _records;
        // The ticks' scratch lists (kept: no list per tick).
        std::vector<Formats::Dti::Record> _spotRecords;
        std::vector<int> _weights;
        std::vector<Vector3> _spots;
        std::unordered_set<int> _touchedGroups;
        std::vector<std::string> _loadedNext;
        std::vector<std::string> _drawnNext;
        static constexpr std::array<int, 6> NearerFirst = {0, 0, 0, 1, 1, 2};

        // Pairs the connection records (DTI type 6) of the arenas by their id (0x41c22c).
        void FindConnections();

        void RunTick();

        // The objects updated this tick: one moved into the other arena on the way runs once.
        std::unordered_set<MdkObject*> _ranObjects;

        // Updates the live objects of arena (0x43c7dc).
        // C#'s ListCopy<MdkObject> (a pooled copy of the list): a copied vector.
        void RunObjects(const std::vector<std::shared_ptr<MdkObject>>& objects, const std::string& arena);

        void RunControllers();

    public:
        // special_event (opcode 131, 0x4456d2): cutscenes (events above 50, 0x477cf4) or the end
        // of the level (50 and below).
        void SpecialEvent(MdkObject& obj, int value);

        // The shooting galleries' guns (if_gun_aim 219, 0x461024) of level 6: they only fire
        // along -y (270° ± 3°) and lead Kurt by the 1.67 s their shot takes (sideways only). Faster
        // sideways movement makes them likelier to fire at him; otherwise they aim at a raised target
        // (objects within 2 of y and 3 of z in their cone), then at Kurt anyway, and else face 270° and
        // fire 29% of the time. Sets the yaw; returns whether to fire.
        bool GunAim(MdkObject& obj, float y, float z);

        // place_x_near_player (220, 0x460f00): the level 6 pop-up targets rise at Kurt's x
        // (25%), where he will be 2.67 s later (50%) or at random (25%, or always when he's outside
        // x_min...x_max or past y_limit), at least 12 units from other objects on the same y.
        void PlaceNearKurt(MdkObject& obj, float xMin, float xMax, float yLimit);

        // camera_track (203, 0x4612e0): tilts the camera up towards a tall object while Kurt
        // faces it, less the more he turns away (none past 90°), at most 30° up. Mode 0 looks 70% of the
        // way up the object, mode 1 at height × scale above its origin.
        void CameraTrack(MdkObject& obj, int mode, float height);

    private:
        // The DTI camera pitch of Kurt's arena (arena+0x462).
        float ArenaCameraPitch();

        // The scope's target lock (during projection in the original, 0x43b65c): the nearest
        // object whose screen box overlaps a 64-pixel square around the crosshair (640x480 pixels), not
        // flagged 0x30. Homing rounds chase it, and the zoom goes down to 0.375 x its height / distance
        // (0x4678b0) instead of 0.25.
        void UpdateSniperTarget();

        // Kurt fires from the scope: a round of type, or Bones' air strike.
        // Returns false when nothing went (no free round slot, no strike).
        bool FireSniper(int type);

    public:
        // special_130 (0x45d140): a bullet hole on the texture of the part a sniper round hit
        // last (BulletHoles); TextureStamped tells the drawing.
        void StampBulletHole(MdkObject& obj);

        // A bullet hole changed a texture's pixels.
        std::function<void(Formats::Texture&)> TextureStamped;

    private:
        // A texture as an arena's objects find it (their archives, see ArchivesOf).
        std::shared_ptr<Formats::Texture> ArenaTexture(const std::string& arena, const std::string& name);

    public:
        // The first active object of a type (the cutscenes' targets).
        std::shared_ptr<MdkObject> FindObjectNamed(const std::string& type);

    private:
        // Starts a cutscene: Kurt stops firing (0x4779b0) and stands still, the camera looks at the target.
        void StartCutscene(int state, MdkObject& target);

        void EndCutscene();

        // A frame during a cutscene (0x478704): only some objects run and are drawn, and the
        // camera follows the cutscene's shot (0x477d94).
        void CutsceneTick();

        // The cutscene camera (0x477d94). Every shot but mode 11 looks at the target, 3 units
        // above its origin. Mode 0 starts from (423, 85) and switches to mode 1 once the target is 12
        // units away; modes 1 and 2 orbit behind the target (12 or 25 units), smoothly for 1800 ticks.
        void UpdateCutsceneCamera();

        // The end of a level (special_event 0-50): Kurt stops firing and the level is over
        // (0x573b60), with NUKE and TORNADO, and the arena breaks up around Kurt as he rises.
        void EndTheLevel();

        // Spawns the aliens placed by the DTI records of type 2 (ARENA$TYPE_n scripts), and
        // static objects (type 4, 0x43bd38: the pickups of level 8's GUNT_9).
        void SpawnDtiAliens(const std::string& arena);

    public:
        // Whether a spawned object gets the flags of spawn_flagged.
        enum class Spawning { Plain, Flagged };

    private:
        // Pickups without a model that spawn_flagged makes sprite boxes of, and their sizes
        // (0x451b88): LEVEL4's red bones on the board runs, EWJ of LEVEL4 and LEVEL8.
        static const Dictionary<std::string, Vector3> SpritePickups;

    public:
        // spawn_flagged (opcode 161): an object, or a sprite box for SW_EWJ and BONEFLC (spawn_box
        // 0x45cca8), with the flags 0x2008a6.
        void SpawnFlagged(MdkObject& parent, const std::string& type, Vector3 position, int script);

        // Spawns an object of a type in parent's arena; null for an unknown type.
        std::shared_ptr<MdkObject> Spawn(MdkObject& parent, const std::string& type, Vector3 position, float yaw, int instance, int script, Spawning spawning);

        // spawn_box (opcode 159): an object whose model is a box of size
        // (model_create_box 0x404188, 8 corners, 12 triangles) showing a texture as an animated sprite
        // (FIRE, PULSE). The triangles themselves aren't drawn.
        std::shared_ptr<MdkObject> SpawnBox(MdkObject& parent, Vector3 position, Vector3 size, const std::string& texture, int script);

    private:
        // C#'s (Vector3, string) tuple key: std has no Vector3 hash.
        struct BoxKeyHash
        {
            std::size_t operator()(const std::pair<Vector3, std::string>& key) const
            {
                auto h = std::hash<std::string>{}(key.second);
                for (auto f : {key.first.X, key.first.Y, key.first.Z})
                {
                    h = h * 31 + std::hash<float>{}(f);
                }

                return h;
            }
        };

        Dictionary<std::pair<Vector3, std::string>, std::shared_ptr<Formats::Model>, BoxKeyHash> _boxModels;

        // model_create_box (0x404188): 8 corners, 12 triangles, not drawn.
        static std::shared_ptr<Formats::Model> BoxModel(Vector3 size, const std::string& texture);

    public:
        // The objects made by spawn_box (dead ones too: skip them): their model's name is the
        // texture their sprite shows.
        const std::vector<std::shared_ptr<MdkObject>>& Boxes() const { return _boxes; }

        void Remove(MdkObject& obj);

        // Kills an object (object_kill 0x43d670): it switches to its death script if it has
        // one, otherwise it explodes. yaw is the direction the explosion faces.
        void Kill(MdkObject& obj, float yaw = 0.0f);

        // Plays the full-screen strike (0x4398f0) while the game waits.
        void PlayStrikeScene(Scripts::StrikeScene::Kind kind, Scripts::StrikeScene::Plane plane);

        // Blows an object up (0x43d224): its explosion sound (opcode 25, or EXPLODE), a white
        // flash by distance, its pieces flying off, and the global model 0 (EXPLODE, an animated
        // texture) scaled to the object's height and turned to the camera.
        void Explode(MdkObject& obj, float yaw);

    private:
        // The white flash of an explosion: 1000 / distance more, up to 150.
        void FlashAt(Vector3 point);

        // The pieces of an exploding object: the parts of its break-up model (model + "D") and
        // slime drops, or 16 fire sparks when it has none.
        void BreakUp(MdkObject& obj);

    public:
        // Spawns an explosion (0x43cb2c): the global model 0 (EXPLODE), whose animated texture
        // plays once, one frame per tick, pitched towards the camera lift units above
        // it unless the camera is close (within 5 units across and 8 up or down).
        std::shared_ptr<MdkObject> SpawnExplosion(const std::string& arena, Vector3 center, float scale, float yaw = 0.0f, float lift = DefaultLift);

        // An object fell far below its arena (0x43d884): it switches to its death script, put
        // back above the floor, or it's removed.
        void FallOut(MdkObject& obj);

        // Fires the chain gun for one tick (0x41a304): it hits the best target in front of Kurt
        // (see AimScore) or, without one, the arena up to 150 units away.
        void FireChainGun();

        // A hit on the arena triangle a ray found; see HitGroup.
        int HitGroupAt(const RayHit& hit, int amount, int kind, int weapon);

        // A hit on a triangle of an arena group (0x40d560). kind is what hit
        // it (HitShot, HitChainGun, HitBlast...), weapon the hit type its script sees
        // (if_hit_weapon). The group's hit flags (opcode 168) and hit script (opcode 99) decide what
        // happens. Returns 1 when the hit script ran, | 2 when the group stops such hits (0x20).
        int HitGroup(const std::string& arena, int group, int amount, int kind, int weapon);

        // The centres of a group's triangles, none while the group isn't solid (blasts).
        std::vector<Vector3> GroupCenters(const std::string& arena, int group);

        // group_set_state on a group of an arena.
        void SetGroupState(const std::string& arena, int group, int operation);

        // group_set_texture: the group's triangles take a material value.
        void SetGroupTexture(const std::string& arena, int group, int value);

        // Shakes the screen at least this much (0x467f7c).
        void RaiseShake(float amount);

        // The camera's line of sight from from to to against
        // the objects of Kurt's arena that block it (camera_clearance 0x417ee8: flag 0x1000000, not
        // 0x810): where it first meets one of their parts, or its end.
        Vector3 ClipView(Vector3 from, Vector3 to);

    private:
        // Aim test of the chain gun (0x41ab2c): a box is a target when it's within its size +
        // 140 units of the origin, inside a cone around Kurt's yaw that is wider for big and close boxes,
        // and visible. Returns its score (squared distance, height counting double; lower is better), or -1.
        float AimScore(const Formats::Box& bounds, Vector3 origin, float bestScore);

    public:
        // Whether part index is a weak part (0x462384): its name starts with
        // the prefix and has a digit right after it.
        static bool IsWeakPart(MdkObject& obj, int index);

    private:
        // The chain gun, or the super chain gun (6 times the damage, throws what it kills).
        enum class Gun { Normal, Super };

        void ChainGunHit(MdkObject& obj, int part, const Formats::Box& bounds, Vector3 origin, int damage, Gun gun);

        // The minecrawler reached the town: the screen shakes and a message says which town is
        // gone (OOT_L1, "There goes Laguna Beach!", or OOT_L1A for the second town).
        void FlattenTown();

    public:
        // Shows the health bar with these values for a second (the arena's own object, boss_bar).
        void ShowBarValues(int health, int maxHealth);

        // The bar's health and maximum, or zeros while it's hidden.
        // C#'s (int Health, int Max) tuple.
        struct Bar
        {
            int Health = 0;
            int Max = 0;
        };

        Bar GetBar();

    private:
        // The bar goes after its time, or when its object dies or can't show a bar.
        void UpdateBar();

    public:
        // Sparks where a shot hits (0x41e8f4); a ricochet sound (the object's, set by opcode 26,
        // or RICO1-RICO3) every 4 ticks, always for bursts and when the arena has no effects (0x4052d4).
        void SparkAt(Vector3 point, int count, const std::string& sound = "", Spark kind = Spark::Flesh);

        // A burst of fire sparks (the nuke's blast).
        void SparkFire(const std::string& arena, Vector3 point);

    private:
        // A fan's fire spark (0x414230): half a unit big, still, for the updraft to lift.
        void FanSpark(const std::string& arena, Vector3 point);

        // Where a platform that keeps Kurt is (flag 0x800000), or null.
        static std::optional<Vector3> KeeperAt(MdkObject* owner);

        // Kurt's updraft (damp_gravity 0x469efc): his arena's fans, else the second arena's.
        // E.g. LEVEL3's CHMO_6 shaft fan lifts him on through the floor doorway into HMO_8.
        float KurtUpdraft(Vector3 feet, float vz, float dt);

        // The frame count of an arena's (or the level's) texture, 0 without one.
        int TextureFrames(const std::string& arena, const std::string& name);

        // The nearest solid triangle of one arena along a segment (effects and pieces).
        std::optional<RayHit> RayIn(const std::string& arena, Vector3 from, Vector3 to);

        // The triangles of an arena's group with their current material, for shattering.
        std::optional<Scripts::Debris::Group> GroupFacets(const std::string& arena, int number);

    public:
        // Starts the object's looping sound (set_loop_sound), stopping the previous one; an
        // empty name just stops it.
        void SetLoopSound(MdkObject& obj, const std::string& name);

        // wind_zone (opcode 224, 0x4579e0): while Kurt is inside one of the arena's type-9 boxes,
        // the wind blows him along the yaw at the speed, sliding him on his back (damp_buttslide); in
        // the air without sliding it only pulls him down twice as fast, his chute shut. Disabled, it ends
        // the slide.
        enum class Wind { Off, On };

        void WindZone(const std::string& arena, Wind wind, float yaw, float speed);

        // Picks one of the arena's type-8 waypoints at random (pick_waypoint8 0x460a6c) as the
        // destination (movement command 221). Every waypoint weighs at least 1: 500 - distance (2D), plus
        // 200 - distance to Kurt when Kurt is closer than 200, plus 250 when the object is closer to Kurt
        // than to the waypoint and the waypoint is farther from Kurt than from the object, less half the
        // height difference. Mode 0 takes the waypoints 50-500 units away, mode 1 those whose id is
        // within 3 of the nearest one.
        void PickWaypoint8(MdkObject& obj, int mode);

        // A type-5 spot to advance to (find_advance_spot, like find_cover_spot without the
        // visibility tests and with 2D distances): 5-400 units away, no farther from the object than Kurt
        // is and closer to Kurt than the object. It becomes the destination (movement command 197).
        void FindAdvanceSpot(MdkObject& obj);

        // Looks for cover (find_cover_spot): a type-5 spot of the object's arena, 9-400 units
        // away, no farther than Kurt, closer to Kurt than the object, hidden from Kurt but visible to the
        // object. One of the 3 nearest is chosen (nearer ones more likely) and the object goes there
        // (movement command 43); otherwise it stops.
        void FindCoverSpot(MdkObject& obj);

    private:
        // The records of a type, in a scratch list (valid until the next call).
        const std::vector<Formats::Dti::Record>& OfType(const std::vector<Formats::Dti::Record>& records, uint type);

    public:
        // Plays a sound at a point, independently of any object.
        void PlaySoundAt(const std::string& name, Vector3 point);

    private:
        // The SW_EWJ easter egg (0x46d718): a holy cow (SW_HCOW) drops from 100 units above the
        // enemy Kurt faces (see CowTarget), or above Kurt himself.
        void DropCow();

        // The cow's target: the object of Kurt's arena within 600 units with the lowest score
        // (its distance, + 400 beyond 30° of Kurt's yaw, + 1000 beyond 50°) that has open sky 100 units
        // above it and no cow on it already.
        std::shared_ptr<MdkObject> CowTarget();

    public:
        // Kurt takes the pickups he runs through (damp_collect_pickups 0x46c448): the segment he
        // moved along this tick crosses a pickup's bounds, grown by 1 unit (and 5 downwards).
        void CollectPickups();

        // Spawns a connector (a door) between the object's arena and another (spawn_connector).
        // A connector of that type already linking both arenas is moved into this arena instead, unless
        // Kurt can see it.
        void SpawnConnector(MdkObject& obj, const std::string& type, Vector3 position, float yaw, int instance, const std::string& otherArena, int script);

        // Fires a projectile (fire): the global model starts at a reference point of the object
        // (origin [0, index]) or at the centre of one of its parts ([1, name]), flying along the object's
        // yaw and pitch, and runs the script.
        std::shared_ptr<MdkObject> Fire(MdkObject& obj, const OperandList& origin, const std::string& bullet, int script);

        void HurtKurt(int damage);

        // Whether Kurt looks at an object (0x460730): it's min-max range away, within a cone
        // around his yaw that narrows from 90° next to him to cone at the max range,
        // and in sight.
        bool KurtLooksAt(MdkObject& obj, float minRange, float maxRange, float cone);

        // The object Kurt stands on (0x573b84), if any.
        std::shared_ptr<MdkObject> GetKurtPlatform();

    private:
        // What Kurt collides with (damp_collide_move): the visible parts of the objects of his
        // arena that are alive, walls or floors (Footing), but the one he rides
        // and doors' LOCK parts.
        std::span<const Collision::Solids::Solid> SolidsWithin(const Formats::Box& region);

        std::vector<Collision::Solids::Solid> _solidsWithin;

    public:
        // Contact damage (touch_damage 0x45cf60): targets 1 hurts Kurt once per visible part
        // touching him, 2 hurts the other objects touching its box (hit event -3, hit type -4). With
        // flags 1 the object dies when it hits. Returns whether it hit.
        bool TouchDamage(MdkObject& obj, int targets, int damage, int flags);

        // push_kurt: knocks Kurt down (unless he's already down), pushed along the object's frame
        // (mode 0: [0, a, b, up]) or away from it ([mode, a, up]), and up added to his vertical speed.
        void PushKurt(MdkObject& obj, const OperandList& args);

        // Kurt's bounding box.
        Formats::Box GetKurtBox();

        // Lowest point (Z) of an arena's geometry.
        float GetArenaFloor(const std::string& arena);

    private:
        // The arenas rays meet: Kurt's and the second, or every arena before he has one
        // (arrays kept: no list per ray).
        const std::vector<std::string>& RayArenas();

        std::vector<std::string> _bspNames;
        std::vector<std::string> _rayPair = std::vector<std::string>(2);

    public:
        // The nearest arena triangle a segment crosses, or null. Only Kurt's arena and the second
        // one (loaded, active or not) stop rays (0x421680); the others are passed through.
        std::optional<RayHit> Raycast(Vector3 from, Vector3 to);

        // Whether a segment crosses an up-facing face (|nz| ≥ 0.5) of one arena (0x421708):
        // the scripts' floor probe, which other objects (platforms) don't stop.
        bool CrossesFloor(const std::string& arena, Vector3 from, Vector3 to);

        // A box of half extents half swept without sliding against Kurt's
        // arena, then the second one (bsp_sweep_box, the mortar round in 0x462708). Faces stop it only
        // from their front: a wall seen from its back is passed (LEVEL6 OLYM_3's opening). The point is
        // the box centre at the contact, the normal the face's plane.
        std::optional<RayHit> Sweep(Vector3 from, Vector3 to, Vector3 half);

    private:
        static Vector3 TriangleNormal(Formats::Arena& arena, int triangle);

        // The group of an arena triangle (the top byte of its flags).
        static int TriangleGroup(Formats::Arena& arena, int triangle);

    public:
        // The BSP of an arena, for objects' moves.
        std::shared_ptr<Collision::Bsp> BspOf(const std::string& arena);

        // Destination near the target (move_near_target): the target offset by forward and side
        // × 1% of the distance, in the frame of the direction from the object to the target.
        Vector3 NearTargetDestination(MdkObject& obj, float forward, float side);

        // Whether the sender may command the receiver (command_objects, if_count_objects): the
        // receiver has no other leader of a higher priority, and obeys the sender's priority.
        static bool MayCommand(MdkObject& sender, MdkObject& receiver);

        // Sends a command to objects (command_objects 0x440384). Commands: 1 join a formation
        // around the sender, 7 goto target, 0xFC gosub target, 43 go to destination. Selectors: 2 all of
        // a type, 3 all, 4 the first of a type, 5 of a type with instance id, 6 of a type within param
        // with a line of sight, 7 of a type following the sender, 8 all following the sender, 9 the
        // linked object, 10 of a type with Y >= param.
        void CommandObjects(MdkObject& sender, int command, int target, Vector3 destination, int selector, const std::string& type, float param, int id);

    private:
        // Applies a command to one object (0x4405d0). Returns true when it joined a formation.
        bool CommandObject(MdkObject& sender, MdkObject& receiver, int command, int target, Vector3 destination, int formationIndex);

    public:
        // World bounds of an object in its current pose (obj+0x198), or of a model-space box,
        // turned by its yaw.
        Formats::Box GetWorldBounds(MdkObject& obj, std::optional<Formats::Box> bounds = std::nullopt);

        // Objects of the arena of an object, alive, other than it (for commands and counts).
        std::vector<std::shared_ptr<MdkObject>> GetArenaObjects(MdkObject& obj);

        // The animation of a script operand: one stored in the CMI file, or (when its first u32
        // is 0) the arena animation named after it.
        std::shared_ptr<Formats::ModelAnimation> GetAnimation(MdkObject& obj, int offset);

    private:
        std::shared_ptr<Formats::Scripts::ScriptDecoder> _decoder;
        // anim_once, anim_loop (the animation) and door_set_anims (two): their animation operands.
        static const Dictionary<int, int> AnimationOperands;

    public:
        // The models of the level's CMI (any arena's objects may have them).
        std::vector<std::shared_ptr<Formats::Model>> LevelModels();

        // Bakes the poses and boxes of every model with every animation that moves it: the
        // arenas' animations and those the scripts play (a level's load: nothing is baked while playing).
        void PreparePoses();

        // An animation of an arena's models, by name (arena_find_animation 0x440adc).
        std::shared_ptr<Formats::ModelAnimation> FindArenaAnimation(const std::string& arena, const std::string& name);

        // A global model of the CMI, or one of the arena's.
        std::shared_ptr<Formats::Model> FindModel(const std::string& arena, const std::string& type);

    private:
        // A texture of an arena, or of the level.
        std::shared_ptr<Formats::Texture> FindTexture(const std::string& arena, const std::string& name);

    public:
        // can_see_kurt: Kurt within range, inside a cone around the object's yaw, with a clear
        // line of sight. The cone's formula isn't known; it's used as a half angle in degrees.
        bool CanSeeKurt(MdkObject& obj, float range, float cone);

    private:
        // play_sound (0x442402): flags & 3: 0 play, 1 restart, 2 play unless it's playing, 3
        // stop (3D) or nothing (2D). 0x80: without position; 0x10: following the object at an offset;
        // 0x20: at a reference point; 0x40: at a point; none: where the object is (following it with
        // flag 4, which also makes it the object's tracked sound).
        // play_sound's start modes 0-2.
        static constexpr std::array<Audio::SoundMixer::Start, 3> SoundStarts = {Audio::SoundMixer::Start::New, Audio::SoundMixer::Start::Restart, Audio::SoundMixer::Start::Once};

    public:
        void PlaySound(MdkObject& obj, const std::string& name, int flags, const Operand& position);

    private:
        // A sound following an object, at an offset in its frame (kept to stop it with the object).
        int PlayOn(const std::string& name, MdkObject& obj, Audio::SoundMixer::Start start, Vector3 offset);

        // ScriptRuntime.Beta.cs
        // Ticks "an object that sounded the alarm is gone" lasts (the demo's 0xe21de).
        static constexpr int AlarmEndedDuration = 10;
        // Parts this much farther from the line of fire than the nearest may be hit too.
        static constexpr float BetaPartSpread = 1.5f;
        // The movement command of an object sounding the alarm.
        static constexpr int AlarmCommand = 15;

    public:
        // Ticks left of "an object that sounded the alarm is gone" (opcode 28 of the demo).
        int AlarmEndedTicks = 0;

    private:
        // The level is one of the 1996 demo's.
        bool IsBeta() const;

        // Kurt's arena in the demo's levels: the smallest arena box around him, when his arena
        // connects to it.
        std::string BetaArena();

        bool Connects(const std::string& from, const std::string& to);

        // An object killed: one sounding the alarm starts the demo's alarm-ended ticks.
        void NoteAlarmEnded(MdkObject& obj);

        // The part of an object the chain gun hits in the demo's levels, or -1. Its scripts ask
        // which part was hit for every object (XW3's guns, XB2's eyes and nose, the grunts'
        // heads). How the demo picks it wasn't read: one of the shown parts nearest to the line Kurt fires
        // along, at random within BetaPartSpread, so that parts above each other all get hit.
        int BetaHitPart(MdkObject& obj, Vector3 origin);

        // ScriptRuntime.OneHit.cs
        // Enemies whose scripts count hits through their health (puzzles): LEVEL8's forklift
        // (driver off below 1000, then pushed by each hit) and XEARTH (sinks after 720 counted hits).
        static const std::unordered_set<std::string> ScriptedTargets;

    public:
        Scripts::Lethality Lethality = Scripts::Lethality::Normal;

        // The damage of Kurt's hit on an object.
        int KurtDamage(MdkObject& obj, int damage);

        // The damage of Kurt's hit on an object or its part with health left:
        // with onehit, at least all of an enemy's (a hit of 1 on a grunt of 20 takes 20).
        int KurtDamage(MdkObject& obj, int damage, int health);

    private:
        static bool IsOneHitTarget(MdkObject& obj);

        // ScriptRuntime.Snapshot.cs
        static constexpr std::string_view ControllerKey = "arena:";
        static constexpr std::string_view HitScriptsKey = "hits:";

    public:
        // Whether a full save may be made now (0x42b520(0)): no cutscene, the level not over,
        // no strike out; the port also needs Kurt alive and not riding.
        bool CanSnapshot();

        // Kurt and the level for a full save, as JSON ({"kurt": ..., "level": ...}).
        std::string Capture();

        // Kurt and the level of a full save (Capture), before the first tick.
        void Load(const std::string& json);

        // The level's state for a full save.
        nlohmann::json Snapshot();

        // Puts the level back as Snapshot left it, before the first tick (objects
        // are created again, their scripts carry on where they were).
        void Restore(const nlohmann::json& data);

    private:
        void RestoreFields(const nlohmann::json& data, Scripts::Snapshot& packer);

        nlohmann::json PackObject(Scripts::Snapshot& packer, MdkObject& obj);

        // An object of a full save, without its variables yet (null when its model is gone).
        std::shared_ptr<MdkObject> Recreate(const nlohmann::json& entry);

        // The arenas' script objects, by key, for references in full saves.
        Dictionary<std::string, std::shared_ptr<MdkObject>> FixedObjects();

        // An animation by its name: one stored in the CMI (CMI_offset), one of the arena's
        // models, or one of the items.
        std::shared_ptr<Formats::ModelAnimation> FindAnimationNamed(MdkObject* obj, const std::string& name);
    };
}
