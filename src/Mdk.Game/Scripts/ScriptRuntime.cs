using System.Numerics;
using Mdk.Formats;
using Mdk.Formats.Scripts;
using Mdk.Game.Audio;
using Mdk.Game.Collision;
using Mdk.Game.Level;
using Mdk.Game.Objects;
using static Mdk.Game.Scripts.ScriptMath;

namespace Mdk.Game.Scripts;

/// <summary>Runs the level's scripts: arena scripts (for the arena Kurt is in), the objects they
/// spawn and the aliens placed by the DTI, at the original's 30 ticks per second. A port of
/// godot-mdk's <c>script_runtime.gd</c> (see its docs/scripts.md and docs/engine.md).
/// <code>
///   tick ─► Kurt's arena? ─► triggers, arenas ─► arena scripts ─► subsystems
///        └─► objects of the live arenas: door ─► script (ScriptVm) ─► motion (ObjectMotion)
/// </code></summary>
public sealed partial class ScriptRuntime
{
    public const float Tick = 1f / 30f;
    public const float TicksPerSecond = 30f;
    /// <summary>Flags of objects spawned by spawn_flagged and of projectiles (fire).</summary>
    public const int SpawnFlaggedFlags = 0x2008A6;
    public const int ProjectileFlags = 0x80820;
    /// <summary>Health from which objects are indestructible.</summary>
    public const int Indestructible = 65000;
    /// <summary>Chain gun (0x41a304): damage per tick, reach beyond the target's size, and the reach
    /// of shots that hit nothing.</summary>
    private const int ChainGunDamage = 1;
    private const int SuperChainGunFactor = 6;
    private const float ChainGunReach = 140f;
    private const float ChainGunWallReach = 150f;
    private const float SuperChainGunPush = 20f;
    /// <summary>Objects with more hit points than this don't show the health bar.</summary>
    private const int MaxBarHealth = 900;
    /// <summary>Ticks before the minecrawler reaches the level's town, per difficulty (45, 30 and 20 minutes).</summary>
    private static readonly int[] TownTicksByDifficulty = [81000, 54000, 36000];
    /// <summary>Index in the order of play of the last level, which has no town.</summary>
    private const int LastLevelIndex = 5;
    /// <summary>Global flags: the minecrawler went for the second town (set by the game), the first
    /// or the second town was flattened (tested by the debriefing).</summary>
    public const int FlagSecondTown = 1 << 31;
    public const int FlagTownFlattened = 1 << 30;
    public const int FlagSecondTownFlattened = 1 << 29;
    private const float TownShake = 5f;
    private const float TownMessageSeconds = 5f;

    // Cutscene states (0x573c60, set by special_event): 0 = none.
    public const int CutsceneDog = 0x34;
    public const int CutsceneAll = 0x3d;
    public const int CutsceneStrike = 0x47;
    public const int CutsceneEnd = 0x51;
    public const int CutsceneAllFlagged = 0x5b;
    public const int CutsceneBoss = 0x5d;
    /// <summary>The only objects that run in cutscenes below <see cref="CutsceneAll"/> (0x47868c).</summary>
    private static readonly string[] CutsceneTypes = ["XM5_FLAP", "XBN", "BOLT", "BIGBOLT", "SW_SBONE", "SW_SEAL"];
    /// <summary>Events up to this end the level; higher ones are cutscenes.</summary>
    private const int LastEndEvent = 50;
    /// <summary>Flags of the objects hidden in cutscenes from <see cref="CutsceneAll"/> (pickups, Kurt's items).</summary>
    private const int CutsceneHiddenFlags = 0x201000;
    private const int CameraBlendTicks = 1800;

    /// <summary>The holy cow (0x46d718): dropped from 100 units above a target within 600, blowing up
    /// 0.5 s after landing; flags 0x40000806 (gravity, collisions, Kurt passes through, the cow).</summary>
    private const float CowHeight = 100f;
    private const float CowRange = 600f;
    private const float CowFuse = 0.5f;
    private const int CowFlags = MdkObject.FlagGravity | MdkObject.FlagCollides | MdkObject.FlagNotSolid2 | MdkObject.FlagCow;

    /// <summary>DTI records: trigger boxes (show an arena, load one ahead, 0x41bf1c), aliens, static
    /// objects (pickups, 0x43bd38), cover spots, waypoints, wind zones, connections.</summary>
    private const uint TriggerShow = 1;
    private const uint DtiAlien = 2;
    private const uint TriggerLoad = 3;
    private const uint DtiStatic = 4;
    private const uint DtiCover = 5;
    private const uint DtiWaypoint = 8;
    private const uint DtiWind = 9;
    /// <summary>Connections (DTI type 6, 0x41c550): the doorway reaches this far below its floor
    /// (0x4945c8), a hatch's plane is this far below its z (0x4945d0).</summary>
    private const float DoorwayDrop = 5f;
    private const float HatchDrop = 0.5f;
    private const int DtiStaticFlags = MdkObject.FlagPickup | MdkObject.FlagNotSolid2 | MdkObject.FlagNoBanking | MdkObject.FlagNotTarget;
    private const string NoArena = "NONE";

    /// <summary>Sparks (0x41e8f4): on objects (green, blue without gore: 0x41e919), on indestructible
    /// objects and walls (grey, half as fast), on groups that react to the hit (orange), and fire.</summary>
    public enum Spark { Flesh, Hard, Group, Fire }

    /// <summary>Palette colours (base, range) of each kind.</summary>
    private static readonly (int Base, int Range)[] SparkColours = [(3, 3), (0x25, -16), (10, 3), (0x30, 0x10)];
    private static readonly (int Base, int Range) SparkFleshNoGore = (13, 3);
    private const float SparkSize = 0.5f;
    private const float SparkSlow = 0.5f;
    private const int FireSparks = 16;
    private static readonly string[] Ricochets = ["RICO1", "RICO2", "RICO3"];
    /// <summary>Object explosions: the white flash (1000 / distance, up to 150), the slime drops
    /// (scale 5-6.6), the pitch towards the camera 5 units above it (3 for other explosions) unless
    /// it's within 5 units across and 8 up or down.</summary>
    private const float ExplosionFlash = 1000f;
    private const float ExplosionFlashMax = 150f;
    private const int GoreDrops = 32;
    private const float GoreScale = 5f;
    private const float ExplosionLift = 5f;
    private const float DefaultLift = 3f;
    private const float ExplosionNear = 5f;
    private const float ExplosionNearHeight = 8f;
    private const int ExplosionFrames = 26;
    private const float ExplosionSizeFactor = 1.5f;
    public const string ExplodeSound = "EXPLODE";
    private const float TeleportFlash = 255f;

    /// <summary>Kinds of hits on triangle groups (0x40d560), matched against their hit flags and masks.</summary>
    public const int HitShot = 1;
    public const int HitChainGun = 2;
    public const int HitBlast = 3;
    public const int HitOtherBlast = 4;
    /// <summary>Kurt running into a group (damp_collide_move 0x46634e): kind 8, hit type -11, no damage.</summary>
    public const int HitKurt = 8;
    public const int HitTypeKurt = -11;
    /// <summary>Group hit flags (group_set_hit_flags): stops the hit, always runs, destructible.</summary>
    private const int GroupStopsHit = 0x20;
    private const int GroupAlwaysHit = 0x40;
    public const int GroupDestructible = 0x80;
    public const int GroupCount = 16;
    /// <summary>group_set_state operations of destructible groups: hidden, then shown when hit.</summary>
    public const int GroupHide = 2;
    private const int GroupShow = 3;

    /// <summary>Animations stored in the CMI file are named after their offset (CMI_1a2b).</summary>
    private const string CmiAnimationPrefix = "CMI_";
    private const int AnimationNameLength = 8;
    /// <summary>The arena camera pitch when the DTI has none (degrees).</summary>
    private const float DefaultCameraPitch = 4f;
    /// <summary>Kurt's box (MDK): half width and height, from his feet.</summary>
    private const float KurtHalfWidth = 0.6f;
    private const float KurtHeight = 5f;
    /// <summary>Kurt's eyes and objects' middles, above their origins, for lines of sight.</summary>
    private const float EyeHeight = 5f;
    private const float KurtChest = 4f;
    private const float TargetHeight = 2f;
    private const float NoArenaFloor = -1000f;

    /// <summary>Per arena script state (the arena's embedded object).</summary>
    public sealed class ArenaState(string name, int script)
    {
        public readonly string Name = name;
        public readonly MdkObject Controller = new() { Arena = name, Restart = script };
        /// <summary>The object group hit scripts run in (the original's scratch object 0x57fc40).</summary>
        public readonly MdkObject HitScripts = new() { Arena = name };
        public readonly float[] Variables = new float[4];
        public int Flags;
        public bool Started;
        /// <summary>Per triangle group (1-16): hit behaviour (group_set_hit_flags), hit kinds that run
        /// the group's hit script and that script (group_on_hit), and counters (arena+0xcc).</summary>
        public readonly int[] GroupHitFlags = new int[GroupCount];
        public readonly int[] GroupHitMasks = new int[GroupCount];
        public readonly int[] GroupHitScripts = new int[GroupCount];
        public readonly int[] GroupCounters = new int[GroupCount];
    }

    /// <summary>A ray's hit: point, plane normal, arena and triangle group.</summary>
    public readonly record struct RayHit(Vector3 Point, Vector3 Normal, string Arena, int Group);

    public readonly LevelData Level;
    public readonly Cmi Cmi;
    public readonly Mdk.Game.Kurt.Kurt Kurt;
    public readonly SoundMixer Mixer;
    public readonly ScriptVm Vm;
    public readonly ObjectMotion Motion;
    public readonly ObjectBehaviors Behaviors;
    public readonly Effects Effects;
    public readonly Debris Debris;
    /// <summary>Sniper rounds, and the object locked in the scope (0x573a8c).</summary>
    public readonly SniperRounds SniperRounds;
    public MdkObject? SniperTarget;
    public readonly AirStrike AirStrike;
    /// <summary>The full-screen strike playing, if any (the game waits meanwhile).</summary>
    public StrikeScene? Strike { get; private set; }
    public readonly Rides Rides;
    public readonly Items Items;
    public readonly Fans Fans;
    public readonly GameStats Stats = new();
    /// <summary>The end of the level's tornado, once it started.</summary>
    public EndLevel? EndLevel;
    /// <summary>On-screen messages (hud_message), set by the game.</summary>
    public Hud.Messages? Messages;
    /// <summary>rand(); seeded by the soak test (its runs repeat), otherwise random.</summary>
    public readonly Random Rng;

    /// <summary>The point and direction of the last shatter_group (0x4d5374, 0x4d5358).</summary>
    public Vector3 ShatterPoint;
    public Vector3 ShatterDirection = Vector3.UnitZ;
    public readonly float[] GlobalVariables = new float[4];
    public int GlobalFlags;
    /// <summary>Kurt's position and the target of alien scripts (Kurt, or a decoy).</summary>
    public Vector3 KurtPosition;
    public Vector3 TargetPosition;
    /// <summary>Yaw of the target and of Kurt (degrees).</summary>
    public float TargetYaw;
    public float KurtYaw;
    /// <summary>Kurt's velocity (u/s), measured over the last tick.</summary>
    public Vector3 KurtVelocity;
    /// <summary>The camera, which explosions turn to (fed by the game).</summary>
    public Vector3 Eye;
    /// <summary>The object aliens aim at instead of Kurt (0x491e48, set_target_mode 1).</summary>
    public MdkObject? AlienTarget;
    /// <summary>Ticks the alarm keeps sounding (0x573aec), set by objects with movement command 15.</summary>
    public int AlarmTicks;
    /// <summary>How the sky is drawn (0x574304, opcode 202): 0 normally, 1 black, -1 not drawn (<see cref="Level.SkyModes"/>).</summary>
    public int SkyMode;
    /// <summary>Gore (if_option, 0x5742dc): 1 on, 0 off.</summary>
    public int Option = 1;
    /// <summary>The strongest screen shake asked this tick (raise_573aa8).</summary>
    public float Shake;
    /// <summary>A screen shake was asked (the follow camera shakes).</summary>
    public event Action<float>? ShakeRaised;
    /// <summary>Frames of arena textures set by arena_texture_frame (opcode 133).</summary>
    public readonly AnimatedTextures AnimatedTextures;

    /// <summary>The health bar (0x573c74 seconds left, 0x41e3c8): its object (0x573c78), or values;
    /// drawn by Hud/HudView.</summary>
    public float BarTime;
    public MdkObject? BarObject;
    public int BarHealth;
    public int BarMax;

    public int Cutscene;
    public MdkObject? CutsceneTarget;
    /// <summary>The cutscene camera (0x599920...0x599940): shot, yaw (90° - heading), pitch, distance
    /// to the target, stored position and the blend timer in ticks; the game looks from
    /// <see cref="CameraPoint"/> (<see cref="CutsceneCamera"/>).</summary>
    public int CameraMode;
    public float CameraYaw;
    public float CameraPitch;
    public float CameraDistance;
    public Vector3 CameraPosition;
    public int CameraTimer;
    public Vector3 CameraPoint;

    /// <summary>The level is over (0x573b60).</summary>
    public bool LevelOver;
    /// <summary>A level ended (special_event up to 50), or the whole game (event 81).</summary>
    public event Action<GameOver>? LevelEnded;
    public event Action? GameFinished;
    /// <summary>The full-screen strike started or ended: the game is paused but not the music.</summary>
    public event Action<StrikeState>? StrikeSceneChanged;

    public enum GameOver { No, Yes }

    public enum StrikeState { Started, Ended }

    /// <summary>Ticks left before the minecrawler flattens the town (0x574270); none on the last level.</summary>
    public int TownTicks;
    /// <summary>The time left for the town (0x574270 over 0x574274), 1 without one.</summary>
    public float TownTimeLeft => _townTotal == 0 ? 1f : Math.Clamp((float)TownTicks / _townTotal, 0f, 1f);
    /// <summary>The town's whole time; saved, since saves have no difficulty.</summary>
    private int _townTotal;
    public string CurrentArena = "";
    /// <summary>The second arena (0x573a68): behind an open door, the one Kurt just left, or one loaded
    /// ahead; when active (0x573a6c) its objects and script run too.</summary>
    public string SecondArena = "";
    public bool SecondActive;
    /// <summary>The arenas drawn (Kurt's and the active second) last tick.</summary>
    public IReadOnlyList<string> DrawnArenas => _drawnArenas;

    /// <summary>The arenas Kurt collides with: the drawn ones, not the second on the snowboard (0x465e34).</summary>
    public IReadOnlyList<string> SolidArenas
    {
        get
        {
            if (!Rides.OnBoard())
            {
                return _drawnArenas;
            }

            _solidFirst.Clear();
            if (_drawnArenas.Count > 0)
            {
                _solidFirst.Add(_drawnArenas[0]);
            }

            return _solidFirst;
        }
    }

    /// <summary>On the board, Kurt's arena alone (kept: no list per step).</summary>
    private readonly List<string> _solidFirst = [];

    public readonly List<MdkObject> Objects = [];
    /// <summary>camera_track (opcode 203): the camera pitch the scripts want (0x573918), for this
    /// many more ticks (0x5739b0).</summary>
    public float CameraTrackPitch;
    public int CameraTrackTicks;
    /// <summary>A script made an arena reachable (a teleport into it).</summary>
    public event Action<string>? ArenaEntered;

    private readonly ArenaSpace _space;
    private readonly Bni _sprites;
    private readonly TriangleGroups _groups;
    private readonly Dictionary<string, Bsp> _bsps = [];
    private readonly Dictionary<string, float> _floors = [];
    /// <summary>The arena at the other end of each connection: (arena, record id) → arena.</summary>
    private readonly Dictionary<(string Arena, int Id), string> _connections = [];
    private readonly Dictionary<string, ArenaState> _arenas = [];
    private readonly Dictionary<int, ModelAnimation> _animations = [];
    private readonly List<MdkObject> _boxes = [];
    /// <summary>The voices following each object: they stop when it's removed.</summary>
    private readonly Dictionary<MdkObject, List<int>> _following = [];
    /// <summary>The voice lists of removed objects, reused.</summary>
    private readonly Stack<List<int>> _spareVoiceLists = new();
    private readonly List<string> _loadedArenas = [];
    private readonly List<string> _drawnArenas = [];
    /// <summary>The camera's arena is looked for from this high above Kurt's feet (0x4940f0).</summary>
    private const float CameraLineHeight = 3f;
    /// <summary>The one arena (not corridor) loaded (_g_current_arena); its corridors are loaded too.</summary>
    private string _residentArena = "";
    private int _nextInstance = 1000;
    private Vector3 _previousKurtPosition;
    /// <summary>A teleport into an arena set the previous position before the first tick.</summary>
    private bool _teleported;
    private float _time;
    private int _tickCount;

    public ScriptRuntime(LevelData level, Cmi cmi, Bni sprites, ArenaSpace space, TriangleGroups groups, SoundMixer mixer,
        Mdk.Game.Kurt.Kurt kurt, int? seed = null)
    {
        Rng = seed is { } value ? new Random(value) : new Random();
        Kurt = kurt;
        Rides = new Rides(this);
        _sprites = sprites;
        Items = new Items(this, sprites);
        SniperRounds = new SniperRounds(this);
        AirStrike = new AirStrike(this);
        kurt.SniperFire = FireSniper;
        kurt.ItemUsed += Items.UseItem;
        kurt.BombTriggered += Items.TriggerBomb;
        kurt.CanUseItem = Items.CanUse;
        kurt.SolidsWithin = SolidsWithin;
        Effects = new Effects(Rng) { FrameCount = TextureFrames, Ray = RayIn };
        AnimatedTextures = new AnimatedTextures(TextureFrames);
        Fans = new Fans(level.Dti.Arenas, Rng) { Spark = FanSpark, IsLive = IsLiveArena };
        Debris = new Debris(Rng)
        {
            Ray = RayIn,
            Trail = Effects.SpawnTrail,
            Updraft = (arena, point, vz, dt) => Fans.Query(arena, point, vz, Fans.MaskEffects, dt),
            GroupFacets = GroupFacets,
        };
        kurt.Updraft = (vz, dt) => KurtUpdraft(kurt.Feet, vz, dt);
        kurt.Keeper = KeeperAt;
        Level = level;
        Cmi = cmi;
        Mixer = mixer;
        _space = space;
        _groups = groups;
        var decoder = ScriptDecoder.For(cmi);
        decoder.Preload(cmi.EntryPoints());
        _decoder = decoder;
        Vm = new ScriptVm(this, decoder);
        Motion = new ObjectMotion(this);
        Behaviors = new ObjectBehaviors(this);
        foreach (var arena in level.Arenas)
        {
            _bsps[arena.Name] = new Bsp(arena);
            _floors[arena.Name] = arena.Vertices.Length == 0 ? NoArenaFloor : arena.Vertices.Min(v => v.Z);
        }

        FindConnections();

        // No town to save in the last level, nor in the 1996 demo's.
        if (GameStats.IndexOf(level.Number) != LastLevelIndex && !IsBeta)
        {
            TownTicks = _townTotal = TownTicksByDifficulty[(int)Kurt.Inventory.Difficulty];
        }
    }

    /// <summary>Ticks run so far.</summary>
    public int TickCount => _tickCount;

    /// <summary>Runs the whole ticks that <paramref name="delta"/> seconds complete.</summary>
    public void Update(float delta)
    {
        _time += delta;
        while (_time >= Tick)
        {
            _time -= Tick;
            foreach (var obj in Objects)
            {
                obj.Remember();
            }

            RunTick();
            _tickCount++;
        }

        Rides.Follow();
    }

    /// <summary>How far a frame <paramref name="ahead"/> seconds after the last step is from the
    /// last tick to the next (0-1).</summary>
    public float TickBlend(float ahead) => Math.Clamp((_time + ahead) / Tick, 0f, 1f);

    public ArenaState GetArenaState(string arena)
    {
        if (!_arenas.TryGetValue(arena, out var state))
        {
            state = _arenas[arena] = new ArenaState(arena, Cmi.ArenaScripts.GetValueOrDefault(arena));
        }

        return state;
    }

    /// <summary>Group of the floor triangle below Kurt (0x573c10).</summary>
    public int GetKurtFloorGroup()
    {
        var hit = Raycast(KurtPosition + Vector3.UnitZ, KurtPosition - new Vector3(0f, 0f, 10f));
        return hit?.Group ?? 0;
    }

    /// <summary>The target of an object's script (script_run): Kurt, or the decoy while it walks, or
    /// the aliens' target (opcode 251), unless the object always targets Kurt.</summary>
    public void SelectTarget(MdkObject obj)
    {
        const int AlwaysKurt = 2;
        TargetPosition = KurtPosition;
        TargetYaw = KurtYaw;
        if (obj.TargetMode == AlwaysKurt)
        {
            return;
        }

        MdkObject? other = null;
        if (Items.Decoy is { Dead: false })
        {
            other = Items.Decoy;
        }
        else if (AlienTarget is { Dead: false })
        {
            other = AlienTarget;
        }

        if (other != null)
        {
            TargetPosition = other.Position;
            TargetYaw = other.Yaw;
        }
    }

    /// <summary>Whether a sound (by name) is playing anywhere (if_sound_playing).</summary>
    public bool IsSoundPlaying(string name) => Mixer.IsPlaying(name.ToUpperInvariant());

    /// <summary>An arena Kurt may only reach by a teleport is shown and made solid (level.gd enter_arena).</summary>
    private void EnterArena(string arena)
    {
        ArenaEntered?.Invoke(arena);
        if (Level.ArenaNamed(arena) is { } entered)
        {
            _space.Add(entered);
        }
    }

    /// <summary>Moves Kurt (teleport_player): within the arena with a white flash when
    /// <paramref name="arena"/> is empty, otherwise into that arena.</summary>
    public void TeleportKurt(string arena, Vector3 position, float yaw)
    {
        if (arena.Length != 0)
        {
            EnterArena(arena);

            var from = CurrentArena;
            CurrentArena = arena;
            TeleportSecond(arena, from);
            ShowArena(arena);

            // No move crosses a connection (0x41bce4 sets 0x5739cc too).
            _previousKurtPosition = position;
            _teleported = true;
        }

        Kurt.Teleport(position, yaw);
        if (arena.Length == 0)
        {
            Kurt.WhiteFlash = MathF.Max(Kurt.WhiteFlash, TeleportFlash);
        }
    }

    /// <summary>Shuts every door (the soak tour, before it teleports): a door left open freezes with
    /// its arena, and later closes onto an arena no longer drawn; walking away shuts it first.</summary>
    public void ShutDoors()
    {
        foreach (var door in Objects.Where(o => !o.Dead && (o.Flags & MdkObject.FlagDoor) != 0))
        {
            Behaviors.Shut(door);
        }
    }

    /// <summary>The second arena after a teleport (0x41bce4): none into an arena; into a corridor
    /// not loaded, the last arena (DTI order) leading to it (CDANT_1 → DANT_2), loaded ahead; into
    /// a loaded one (of the loaded arena, or Kurt's pair), unchanged. Kurt's show then activates it.</summary>
    private void TeleportSecond(string arena, string from)
    {
        if (Level.Mto.Has(arena))
        {
            SecondArena = "";
            SecondActive = false;
            return;
        }

        if (IsCorridorLoaded(arena, from))
        {
            return;
        }

        var neighbour = Level.Dti.Arenas.LastOrDefault(a => Level.Mto.Has(a.Name) && a.Records.Any(r =>
            r.Type == LevelData.Connection && _connections.GetValueOrDefault((a.Name, r.Id)) == arena));
        SecondArena = neighbour?.Name ?? "";
        SecondActive = false;
        if (neighbour != null)
        {
            LoadArena(neighbour.Name);
        }
    }

    /// <summary>Corridors connected to the loaded arena are loaded with it (0x419ac0); so are the
    /// arena Kurt was in and the second.</summary>
    private bool IsCorridorLoaded(string corridor, string from)
    {
        if (corridor == SecondArena || corridor == from)
        {
            return true;
        }

        return ArenaRecords(_residentArena).Any(r =>
            r.Type == LevelData.Connection && _connections.GetValueOrDefault((_residentArena, r.Id)) == corridor);
    }

    /// <summary>Whether two lists hold the same names in the same order.</summary>
    private static bool Same(List<string> a, List<string> b)
    {
        if (a.Count != b.Count)
        {
            return false;
        }

        for (var i = 0; i < a.Count; i++)
        {
            if (a[i] != b[i])
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>The triangle groups of his arena that Kurt ran into this tick get a hit (0x46634e;
    /// how the snowboard breaks the ice walls).</summary>
    private void KurtTouchesGroups()
    {
        var touched = _touchedGroups;
        touched.Clear();
        foreach (var (arena, triangle) in Kurt.TakeContacts())
        {
            var group = TriangleGroup(arena, triangle);
            if (arena.Name != CurrentArena || group == 0 || !touched.Add(group))
            {
                continue;
            }

            HitGroup(CurrentArena, group, 0, HitKurt, HitTypeKurt);
        }
    }

    /// <summary>Shows an arena (BSPShow 0x41a11c, opcode 100): it becomes the active second arena
    /// (unless it's Kurt's), and the first time its DTI aliens appear. "" or NONE clears it.</summary>
    public void ShowArena(string arena)
    {
        if (arena.Length == 0 || arena == NoArena)
        {
            SecondArena = "";
            SecondActive = false;
            return;
        }

        // Loaded (arena_load 0x419d00): Kurt's arena, or a new second one.
        if (arena == CurrentArena)
        {
            LoadArena(arena);
        }
        else if (arena != SecondArena)
        {
            SecondArena = arena;
            LoadArena(arena);
        }

        SecondActive = true;
        var state = GetArenaState(arena);
        if (!state.Started)
        {
            state.Started = true;
            SpawnDtiAliens(arena);
        }
    }

    /// <summary>Loads an arena ahead (arena_set_neighbour 0x41a2d0, opcode 223): the second arena, not active.</summary>
    public void PreloadArena(string arena)
    {
        if (arena.Length == 0 || arena == NoArena || arena == SecondArena)
        {
            return;
        }

        SecondArena = arena;
        LoadArena(arena);
        SecondActive = false;
    }

    /// <summary>Loads an arena (arena_load 0x419d00): only one arena (not corridor) is loaded at a time.</summary>
    private void LoadArena(string arena)
    {
        if (Level.Mto.Has(arena))
        {
            _residentArena = arena;
        }

        PullDoors(arena);
    }

    /// <summary>An arena being loaded takes the doors leading to it from its neighbours (arena_load
    /// 0x419d00), except from Kurt's arena and the active second one: e.g. loading DANT_2 from
    /// CDANT_2 moves the door CDANT_1 → DANT_2 into DANT_2, where Kurt will meet it.</summary>
    private void PullDoors(string arena)
    {
        foreach (var record in ArenaRecords(arena))
        {
            if (record.Type != LevelData.Connection || !_connections.TryGetValue((arena, record.Id), out var other)
                || other == CurrentArena || (SecondActive && other == SecondArena))
            {
                continue;
            }

            foreach (var door in Objects.Where(o => !o.Dead && (o.Flags & MdkObject.FlagDoor) != 0 && o.Arena == other && o.Connects == arena))
            {
                ObjectBehaviors.MoveDoor(door);
            }
        }
    }

    /// <summary>Only Kurt's arena and the active second one are drawn, with their objects (0x41e344).
    /// An arena in neither slot any more is put away (0x419cb0: its objects' loop sounds stop, Kurt's
    /// thrown items there go), and started again when it comes back (arena_activate 0x43f8e0).</summary>
    private void UpdateArenas()
    {
        var loaded = _loadedNext;
        loaded.Clear();
        loaded.Add(CurrentArena);
        if (SecondArena.Length != 0)
        {
            loaded.Add(SecondArena);
        }

        var drawn = _drawnNext;
        drawn.Clear();
        drawn.Add(CurrentArena);
        if (SecondActive && SecondArena.Length != 0)
        {
            drawn.Add(SecondArena);
        }

        if (!Same(loaded, _loadedArenas))
        {
            using var copy = ListCopy<MdkObject>.Of(Objects);
            foreach (var obj in copy)
            {
                if (_loadedArenas.Contains(obj.Arena) && !loaded.Contains(obj.Arena))
                {
                    // Kurt's thrown items and effects there go (0x43f800).
                    if ((obj.Flags & (Items.FlagThrown | Items.FlagActive)) != 0)
                    {
                        Items.Forget(obj);
                        Remove(obj);
                        continue;
                    }

                    Mixer.StopVoice(obj.LoopSound);
                    obj.LoopSound = 0;
                }
                else if (loaded.Contains(obj.Arena) && !_loadedArenas.Contains(obj.Arena) && obj.LoopSoundName.Length != 0)
                {
                    obj.LoopSound = PlayOn(obj.LoopSoundName, obj, SoundMixer.Start.New, Vector3.Zero);
                }
            }

            _loadedArenas.Clear();
            _loadedArenas.AddRange(loaded);
        }

        _drawnArenas.Clear();
        _drawnArenas.AddRange(drawn);
        foreach (var obj in Objects)
        {
            obj.Visible = drawn.Contains(obj.Arena) && !Rides.Hides(obj);
        }
    }

    /// <summary>The drawn arena the camera is in (0x490db4, camera_update): Kurt's, or the second one
    /// when the line from above Kurt's feet to the <paramref name="eye"/> leaves Kurt's through a
    /// doorway into it. It's drawn over the other (0x41df35).</summary>
    public string CameraArena(Vector3 eye)
    {
        if (_drawnArenas.Count < 2)
        {
            return CurrentArena;
        }

        var from = KurtPosition + Vector3.UnitZ * CameraLineHeight;
        return CrossedArena(CurrentArena, from, eye) == SecondArena ? SecondArena : CurrentArena;
    }

    /// <summary>Whether an arena's objects run: Kurt's, and the second one when it's active.</summary>
    public bool IsLiveArena(string arena) => arena == CurrentArena || (SecondActive && arena == SecondArena);

    /// <summary>The trigger boxes of Kurt's arena (DTI records 1 and 3, 0x41bf1c): walking into one
    /// shows (1) or loads ahead (3) the arena of its id; an id of -1 clears the second arena.</summary>
    private void CheckTriggers()
    {
        var from = new Vector2(_previousKurtPosition.X, _previousKurtPosition.Y);
        var to = new Vector2(KurtPosition.X, KurtPosition.Y);
        foreach (var record in ArenaRecords(CurrentArena))
        {
            if (record.Type != TriggerShow && record.Type != TriggerLoad)
            {
                continue;
            }

            var min = new Vector2(MathF.Min(record.Position.X, record.BoxEnd.X), MathF.Min(record.Position.Y, record.BoxEnd.Y));
            var max = new Vector2(MathF.Max(record.Position.X, record.BoxEnd.X), MathF.Max(record.Position.Y, record.BoxEnd.Y));
            var inside = to.X >= min.X && to.Y >= min.Y && to.X < max.X && to.Y < max.Y;
            if (!inside && !Crosses(min, max, from, to))
            {
                continue;
            }

            var arena = record.Id >= 0 && record.Id < Level.Dti.Arenas.Count ? Level.Dti.Arenas[record.Id].Name : "";
            if (record.Type == TriggerShow)
            {
                ShowArena(arena);
            }
            else if (arena.Length == 0)
            {
                ShowArena("");
            }
            else
            {
                PreloadArena(arena);
            }
        }
    }

    /// <summary>Whether a move crosses a box's edge (XY).</summary>
    private static bool Crosses(Vector2 min, Vector2 max, Vector2 from, Vector2 to)
    {
        Vector2[] corners = [min, new(max.X, min.Y), max, new(min.X, max.Y)];
        for (var i = 0; i < corners.Length; i++)
        {
            if (SegmentsCross(from, to, corners[i], corners[(i + 1) % corners.Length]))
            {
                return true;
            }
        }

        return false;
    }

    private static bool SegmentsCross(Vector2 a, Vector2 b, Vector2 c, Vector2 d)
    {
        var r = b - a;
        var s = d - c;
        var denominator = r.X * s.Y - r.Y * s.X;
        if (denominator == 0f)
        {
            return false;
        }

        var t = ((c.X - a.X) * s.Y - (c.Y - a.Y) * s.X) / denominator;
        var u = ((c.X - a.X) * r.Y - (c.Y - a.Y) * r.X) / denominator;
        return t >= 0f && t <= 1f && u >= 0f && u <= 1f;
    }

    /// <summary>Which way a move goes through a connection to leave the arena (the DTI record's
    /// angle field, an integer): across its x, y or z plane, or across the XY line from its first
    /// corner to the other, ending on its left or right (unused by the levels).</summary>
    private enum Doorway { MinusX, PlusX, MinusY, PlusY, Left, Right, MinusZ, PlusZ }

    /// <summary>An object with <see cref="MdkObject.FlagChangesArena"/> goes into the arena whose
    /// connection its last move crossed (0x45e810, 0x43ca00): the board follows Kurt, so its
    /// script keeps running (LEVEL4's first run ends in MEAT_3).</summary>
    public void FollowArenas(MdkObject obj)
    {
        var arena = CrossedArena(obj.Arena, obj.PreviousPosition, obj.Position);
        if (arena.Length != 0)
        {
            obj.Arena = arena;
        }
    }

    /// <summary>The arena a move enters when it crosses a connection of <paramref name="arena"/>
    /// (0x41c550, every tick), or "". E.g. LEVEL4 MEAT_7 1012 (+y at y = 14822) → CMEAT_7.</summary>
    private string CrossedArena(string arena, Vector3 from, Vector3 to)
    {
        foreach (var record in ArenaRecords(arena))
        {
            if (record.Type == LevelData.Connection && CrossesDoorway(record, from, to)
                && _connections.TryGetValue((arena, record.Id), out var other))
            {
                return other;
            }
        }

        return "";
    }

    /// <summary>Whether a move goes out through a doorway: across its plane in its direction, the
    /// move within (or across) the doorway on the other axes. A horizontal doorway's box reaches
    /// <see cref="DoorwayDrop"/> below it; the diagonal ones only test where the move ends.</summary>
    private static bool CrossesDoorway(Dti.Record record, Vector3 from, Vector3 to)
    {
        var min = record.Position;
        var max = record.BoxEnd;
        var bottom = min.Z - DoorwayDrop;
        var plane = min.Z - HatchDrop;
        var withinX = Spans(from.X, to.X, min.X, max.X);
        var withinY = Spans(from.Y, to.Y, min.Y, max.Y);
        var withinZ = Spans(from.Z, to.Z, bottom, max.Z);
        var side = (to.X - min.X) * (max.Y - min.Y) - (max.X - min.X) * (to.Y - min.Y);
        return (Doorway)BitConverter.SingleToInt32Bits(record.Angle) switch
        {
            Doorway.MinusX => withinY && withinZ && to.X < min.X && from.X >= min.X,
            Doorway.PlusX => withinY && withinZ && to.X > min.X && from.X <= min.X,
            Doorway.MinusY => withinX && withinZ && to.Y < min.Y && from.Y >= min.Y,
            Doorway.PlusY => withinX && withinZ && to.Y > min.Y && from.Y <= min.Y,
            Doorway.MinusZ => withinX && withinY && to.Z < plane && from.Z >= plane,
            Doorway.PlusZ => withinX && withinY && to.Z > plane && from.Z <= plane,
            Doorway.Right => withinX && withinY && withinZ && side > 0f,
            _ => withinX && withinY && withinZ && side < 0f,
        };
    }

    /// <summary>Whether a move from a to b on one axis is within [min, max] or crosses it.</summary>
    private static bool Spans(float a, float b, float min, float max) => MathF.Max(a, b) >= min && MathF.Min(a, b) <= max;

    private List<Dti.Record> ArenaRecords(string arena)
    {
        if (!_records.TryGetValue(arena, out var records))
        {
            records = _records[arena] = Level.EntryNamed(arena)?.Records ?? [];
        }

        return records;
    }

    /// <summary>Each arena's DTI records, by name (found once).</summary>
    private readonly Dictionary<string, List<Dti.Record>> _records = [];
    /// <summary>The ticks' scratch lists (kept: no list per tick).</summary>
    private readonly List<Dti.Record> _spotRecords = [];
    private readonly List<int> _weights = [];
    private readonly List<Vector3> _spots = [];
    private readonly HashSet<int> _touchedGroups = [];
    private readonly List<string> _loadedNext = [];
    private readonly List<string> _drawnNext = [];
    private static readonly int[] NearerFirst = [0, 0, 0, 1, 1, 2];

    /// <summary>Pairs the connection records (DTI type 6) of the arenas by their id (0x41c22c).</summary>
    private void FindConnections()
    {
        var byId = new Dictionary<int, List<string>>();
        foreach (var entry in Level.Dti.Arenas)
        {
            foreach (var record in entry.Records.Where(r => r.Type == LevelData.Connection))
            {
                if (!byId.TryGetValue(record.Id, out var names))
                {
                    byId[record.Id] = names = [];
                }

                names.Add(entry.Name);
            }
        }

        foreach (var (id, names) in byId)
        {
            foreach (var a in names)
            {
                foreach (var b in names.Where(b => a != b))
                {
                    _connections[(a, id)] = b;
                }
            }
        }
    }

    private void RunTick()
    {
        KurtPosition = Kurt.Feet;
        TargetPosition = KurtPosition;
        TargetYaw = Wrap360(Kurt.Yaw);
        KurtYaw = TargetYaw;
        if (Cutscene != 0)
        {
            CutsceneTick();
            return;
        }

        AlarmTicks = Math.Max(AlarmTicks - 1, 0);
        AlarmEndedTicks = Math.Max(AlarmEndedTicks - 1, 0);
        CameraTrackTicks = Math.Max(CameraTrackTicks - 1, 0);
        Shake = 0f;
        UpdateBar();
        if (TownTicks > 0)
        {
            TownTicks--;
            if (TownTicks == 0)
            {
                FlattenTown();
            }
        }

        // The first tick (also after loading a game) has no move, unless a teleport set where it starts.
        if (_tickCount == 0 && !_teleported)
        {
            _previousKurtPosition = KurtPosition;
        }

        // Kurt changes arena only through a connection of his (0x41c550); a teleport puts him anywhere.
        // The 1996 demo's connections have no direction: there the arenas' boxes decide.
        var arena = IsBeta ? BetaArena()
            : CurrentArena.Length == 0 ? _space.ArenaAt(KurtPosition) ?? "" : CrossedArena(CurrentArena, _previousKurtPosition, KurtPosition);
        if (arena.Length != 0 && arena != CurrentArena)
        {
            // Crossing into another arena: the one left stays as the active second arena.
            if (CurrentArena.Length != 0)
            {
                SecondArena = CurrentArena;
                Kurt.EnterArena();
            }

            SecondActive = true;
            CurrentArena = arena;
            ShowArena(arena);
        }

        CheckTriggers();
        UpdateArenas();
        KurtTouchesGroups();
        Rides.Update();

        // Kurt moves and takes pickups, then fires, then the objects run (game_frame).
        if (_tickCount > 0)
        {
            CollectPickups();
        }

        KurtVelocity = (KurtPosition - _previousKurtPosition) * TicksPerSecond;
        _previousKurtPosition = KurtPosition;
        if (Kurt.Firing)
        {
            FireChainGun();
        }

        Items.UpdateTwisters();
        Effects.Update(1f);
        Fans.Update();
        Debris.Update(1f);
        SniperRounds.Update(1f);
        AirStrike.Update(1f);
        EndLevel?.Update(1f);
        UpdateSniperTarget();

        // Only the objects of Kurt's arena and of the active second arena are updated (0x43c7dc).
        using var objects = ListCopy<MdkObject>.Of(Objects);
        foreach (var obj in objects)
        {
            if (obj.Dead || !IsLiveArena(obj.Arena))
            {
                continue;
            }

            if ((obj.Flags & MdkObject.FlagDoor) != 0)
            {
                Behaviors.UpdateDoor(obj);
            }

            var (position, yaw) = (obj.Position, obj.Yaw);
            Vm.Run(obj);
            if (!obj.Dead)
            {
                Motion.Update(obj);
            }

            // Kurt moves and turns with the platform he stands on.
            if (obj == Kurt.Platform && !obj.Dead && Kurt.OnFloor)
            {
                Kurt.Carry(position, obj.Position, obj.Yaw - yaw);
            }
        }

        // Then the arenas' own scripts (game_frame: 0x43c7dc, then 0x440bc8). E.g. MEAT_5's
        // arena_show NONE drops CMEAT_4 only after the ridden board there followed Kurt in.
        RunControllers();
    }

    private void RunControllers()
    {
        if (CurrentArena.Length != 0)
        {
            Vm.Run(GetArenaState(CurrentArena).Controller);
        }

        if (SecondActive && SecondArena.Length != 0)
        {
            Vm.Run(GetArenaState(SecondArena).Controller);
        }
    }

    /// <summary>special_event (opcode 131, 0x4456d2): cutscenes (events above 50, 0x477cf4) or the end
    /// of the level (50 and below).</summary>
    public void SpecialEvent(MdkObject obj, int value)
    {
        if (value <= LastEndEvent)
        {
            EndTheLevel();
            return;
        }

        switch (value)
        {
            case 51:
                // Kurt strikes (0x4779e0): X_STRIKD full screen (0x4398f0), then he's put at the
                // object, which moves 4 units along y.
                PlayStrikeScene(StrikeScene.Kind.Kurt, StrikeScene.Plane.WithPilot);
                Kurt.Teleport(obj.Position, obj.Yaw);
                obj.Position.Y += 4f;
                StartCutscene(CutsceneStrike, obj);
                CameraDistance = 10f;
                CameraPosition = obj.Position - new Vector3(10f, 4f, -8f);
                CameraPitch = 0f;
                CameraYaw = 90f - obj.Yaw;
                break;
            case 52:
                var dog = FindObjectNamed("XBN");
                if (dog != null)
                {
                    StartCutscene(CutsceneDog, dog);
                    CameraMode = 0;
                }

                break;
            case 53:
            case 92:
                EndCutscene();
                break;
            case 55:
                CameraMode = 2;
                break;
            case 61:
                var gunter = FindObjectNamed("XGUNTAM");
                if (gunter != null)
                {
                    StartCutscene(CutsceneAll, gunter);
                    CameraMode = 11;
                    var heading = FromAngle(gunter.Yaw) * 45f;
                    CameraPosition = gunter.Position + new Vector3(heading.X, heading.Y, 3f);
                    CameraPitch = -20f;
                    CameraYaw = 270f - gunter.Yaw;
                    CameraDistance = 45f;
                }

                break;
            case 81:
                // The end of the game: MISC/FLIC/MDKEND.FLC and MDKBZK.MVE (game state 8).
                StartCutscene(CutsceneEnd, obj);
                GameFinished?.Invoke();
                break;
            case 91:
                StartCutscene(CutsceneAllFlagged, obj);
                CameraMode = 12;
                CameraPosition = new Vector3(-121f, 3347f, -350f);
                break;
            case 93:
                StartCutscene(CutsceneBoss, obj);
                CameraMode = 12;
                CameraPosition = new Vector3(1158f, 5006f, 315f);
                break;
        }
    }

    /// <summary>The shooting galleries' guns (if_gun_aim 219, 0x461024) of level 6: they only fire
    /// along -y (270° ± 3°) and lead Kurt by the 1.67 s their shot takes (sideways only). Faster
    /// sideways movement makes them likelier to fire at him; otherwise they aim at a raised target
    /// (objects within 2 of y and 3 of z in their cone), then at Kurt anyway, and else face 270° and
    /// fire 29% of the time. Sets the yaw; returns whether to fire.</summary>
    public bool GunAim(MdkObject obj, float y, float z)
    {
        const float Lead = 1.67333f;
        const float Down = 270f;
        const float Cone = 3f;
        const int MaxTargets = 4;
        var lead = new Vector3(KurtPosition.X + KurtVelocity.X * Lead, KurtPosition.Y, 0f);
        var angle = obj.YawTo(lead) + Rng.Next(500) * 0.001f - 0.25f;
        var aimed = MathF.Abs(angle - Down) <= Cone;
        if (aimed && Rng.Next(70) < Round(MathF.Abs(KurtVelocity.X)) + 10)
        {
            obj.Yaw = angle;
            return true;
        }

        var angles = new List<float>();
        foreach (var other in Objects)
        {
            if (other.Dead || other.Arena != obj.Arena || MathF.Abs(other.Position.Y - y) > 2f || MathF.Abs(other.Position.Z - z) > 3f)
            {
                continue;
            }

            var otherAngle = obj.YawTo(other.Position);
            if (MathF.Abs(otherAngle - Down) > Cone)
            {
                continue;
            }

            angles.Add(otherAngle);
            if (angles.Count == MaxTargets)
            {
                break;
            }
        }

        if (angles.Count != 0)
        {
            obj.Yaw = angles[Rng.Next(angles.Count)];
            return true;
        }

        if (aimed)
        {
            obj.Yaw = angle;
            return true;
        }

        obj.Yaw = Down;
        return Rng.Next(100) > 70;
    }

    /// <summary>place_x_near_player (220, 0x460f00): the level 6 pop-up targets rise at Kurt's x
    /// (25%), where he will be 2.67 s later (50%) or at random (25%, or always when he's outside
    /// x_min...x_max or past y_limit), at least 12 units from other objects on the same y.</summary>
    public void PlaceNearKurt(MdkObject obj, float xMin, float xMax, float yLimit)
    {
        const float Lead = 2.67333f;
        const int Tries = 100;
        const float Spacing = 12f;
        var r = Rng.Next(100);
        float x;
        if (r < 25 || KurtPosition.X < xMin || KurtPosition.X > xMax || KurtPosition.Y > yLimit)
        {
            x = xMin + (xMax - xMin) * Rng.Next(10000) * 0.0001f;
        }
        else if (r < 50)
        {
            x = KurtPosition.X;
        }
        else
        {
            x = KurtPosition.X + KurtVelocity.X * Lead;
        }

        if (x < xMin)
        {
            x += xMax - xMin;
        }

        for (var i = 0; i < Tries; i++)
        {
            var moved = false;
            foreach (var other in Objects)
            {
                if (other == obj || other.Dead || other.Arena != obj.Arena || other.Position.Y != obj.Position.Y
                    || MathF.Abs(x - other.Position.X) >= Spacing - 0.5f)
                {
                    continue;
                }

                x = other.Position.X - Spacing;
                moved = true;
            }

            if (!moved)
            {
                break;
            }
        }

        obj.Position.X = x;
    }

    /// <summary>camera_track (203, 0x4612e0): tilts the camera up towards a tall object while Kurt
    /// faces it, less the more he turns away (none past 90°), at most 30° up. Mode 0 looks 70% of the
    /// way up the object, mode 1 at height × scale above its origin.</summary>
    public void CameraTrack(MdkObject obj, int mode, float height)
    {
        const float MaxUp = -30f;
        var off = Wrap360(KurtYaw - Heading(KurtPosition, obj.Position));
        if (off > HalfTurn)
        {
            off = FullTurn - off;
        }

        var top = obj.Position.Z + height * obj.Scale;
        if (mode == 0)
        {
            var boundsTop = obj.Position.Z + (obj.Model != null ? obj.Model.Bounds.Max.Z * obj.Scale : 0f);
            top = 0.3f * obj.Position.Z + 0.7f * boundsTop;
        }

        var rest = ArenaCameraPitch();
        var target = rest;
        if (off <= 90f && top >= KurtPosition.Z)
        {
            var distance = Distance2D(obj.Position, KurtPosition);
            target = Math.Clamp(-float.RadiansToDegrees(MathF.Atan2(top - KurtPosition.Z, distance)) * (120f - off) / 120f, MaxUp, rest);
        }

        CameraTrackPitch = target;
        CameraTrackTicks = 2;
    }

    /// <summary>The DTI camera pitch of Kurt's arena (arena+0x462).</summary>
    private float ArenaCameraPitch() =>
        Level.EntryNamed(CurrentArena)?.Pitch ?? DefaultCameraPitch;

    /// <summary>The scope's target lock (during projection in the original, 0x43b65c): the nearest
    /// object whose screen box overlaps a 64-pixel square around the crosshair (640x480 pixels), not
    /// flagged 0x30. Homing rounds chase it, and the zoom goes down to 0.375 x its height / distance
    /// (0x4678b0) instead of 0.25.</summary>
    private void UpdateSniperTarget()
    {
        const int Untargetable = MdkObject.FlagNotSolid | MdkObject.FlagNotTarget;
        const int Corners = 8;
        SniperTarget = null;
        Kurt.Scope.ZoomLimit = Mdk.Game.Kurt.Scope.ZoomMin;
        if (!Kurt.Sniping)
        {
            return;
        }

        var eye = Kurt.SniperEye;
        var forward = Kurt.SniperForward;
        var zoom = Kurt.Scope.Zoom;
        var bestDepth = float.MaxValue;
        foreach (var obj in Objects)
        {
            if (obj.Dead || obj.Arena != CurrentArena || obj.Model == null || (obj.Flags & Untargetable) != 0)
            {
                continue;
            }

            var bounds = GetWorldBounds(obj);
            if (Mdk.Game.Kurt.Scope.ToScreen(eye, forward, zoom, bounds.Center()) is not { } centre)
            {
                continue;
            }

            var (min, max) = (centre, centre);
            for (var i = 0; i < Corners; i++)
            {
                var corner = new Vector3((i & 1) != 0 ? bounds.Max.X : bounds.Min.X, (i & 2) != 0 ? bounds.Max.Y : bounds.Min.Y, (i & 4) != 0 ? bounds.Max.Z : bounds.Min.Z);
                if (Mdk.Game.Kurt.Scope.ToScreen(eye, forward, zoom, corner) is { } p)
                {
                    (min, max) = (Vector2.Min(min, p), Vector2.Max(max, p));
                }
            }

            var depth = Vector3.Distance(eye, bounds.Center());
            if (Mdk.Game.Kurt.Scope.Locks(min, max) && depth < bestDepth)
            {
                bestDepth = depth;
                SniperTarget = obj;
            }
        }

        if (SniperTarget != null)
        {
            var bounds = GetWorldBounds(SniperTarget);
            Kurt.Scope.ZoomLimit = Mdk.Game.Kurt.Scope.LockLimit(bounds.Size().Z, Vector3.Distance(KurtPosition, bounds.Center()));
        }
    }

    /// <summary>Kurt fires from the scope: a round of <paramref name="type"/>, or Bones' air strike.
    /// Returns false when nothing went (no free round slot, no strike).</summary>
    private bool FireSniper(int type)
    {
        var fired = type == Mdk.Game.Kurt.Scope.Strike
            ? AirStrike.Call(Kurt.SniperEye, Kurt.SniperForward)
            : SniperRounds.Fire(type, Kurt.SniperEye, Wrap360(Kurt.Yaw), Kurt.Scope.Pitch, SniperTarget);
        if (fired)
        {
            Stats.SniperShots++;
        }

        return fired;
    }

    /// <summary>special_130 (0x45d140): a bullet hole on the texture of the part a sniper round hit
    /// last (<see cref="BulletHoles"/>); <see cref="TextureStamped"/> tells the drawing.</summary>
    public void StampBulletHole(MdkObject obj)
    {
        if (obj.Model is not { } model || obj.ShotPart <= 0 || obj.ShotPart > model.PartList.Count)
        {
            return;
        }

        var part = model.PartList[obj.ShotPart - 1];
        var vertices = obj.Pose()[obj.ShotPart - 1];
        if (vertices.Length == 0)
        {
            return;
        }

        // The hit in the model's frame.
        var local = Vector3.Transform(obj.ShotPoint - obj.Position, Matrix4x4.CreateRotationZ(-float.DegreesToRadians(obj.Yaw))) / obj.Scale;
        Texture? TextureOf(int value) => value >= 0 && value < model.Materials.Count ? ArenaTexture(obj.Arena, model.Materials[value]) : null;
        if (BulletHoles.Nearest(part, vertices, local, v => TextureOf(v) != null) is not { } face)
        {
            return;
        }

        var texture = TextureOf(part.TriangleMaterials[face.Triangle])!;
        var w = face.Weights;
        var uv = part.TriangleUvs[face.Triangle * 3] * w.X + part.TriangleUvs[face.Triangle * 3 + 1] * w.Y + part.TriangleUvs[face.Triangle * 3 + 2] * w.Z;
        BulletHoles.Stamp(texture, uv, _sprites.GetImage(Option != 0 ? BulletHoles.Hole : BulletHoles.GorelessHole));
        TextureStamped?.Invoke(texture);
    }

    /// <summary>A bullet hole changed a texture's pixels.</summary>
    public event Action<Texture>? TextureStamped;

    /// <summary>A texture as an arena's objects find it (their archives, see <see cref="LevelData.ArchivesOf"/>).</summary>
    private Texture? ArenaTexture(string arena, string name)
    {
        var found = Level.ArenaNamed(arena);
        var archives = found != null ? Level.ArchivesOf(found) : [Level.LevelTextures];
        return archives.Select(a => a.Textures.GetValueOrDefault(name)).FirstOrDefault(t => t != null);
    }

    /// <summary>The first active object of a type (the cutscenes' targets).</summary>
    public MdkObject? FindObjectNamed(string type) => Objects.FirstOrDefault(o => !o.Dead && o.TypeName == type);

    /// <summary>Starts a cutscene: Kurt stops firing (0x4779b0) and stands still, the camera looks at the target.</summary>
    private void StartCutscene(int state, MdkObject target)
    {
        Cutscene = state;
        CutsceneTarget = target;
        Kurt.StopFiring();
        Kurt.Frozen = true;
    }

    private void EndCutscene()
    {
        Cutscene = 0;
        CutsceneTarget = null;
        Kurt.Frozen = false;
        Kurt.Visible = true;
        foreach (var obj in Objects)
        {
            obj.Visible = true;
        }
    }

    /// <summary>A frame during a cutscene (0x478704): only some objects run and are drawn, and the
    /// camera follows the cutscene's shot (0x477d94).</summary>
    private void CutsceneTick()
    {
        Kurt.Visible = Cutscene is CutsceneStrike or CutsceneEnd;
        using var copy = ListCopy<MdkObject>.Of(Objects);
        foreach (var obj in copy)
        {
            if (obj.Dead || obj.Arena != CurrentArena)
            {
                continue;
            }

            var flagged = (obj.Flags & CutsceneHiddenFlags) != 0 || obj.ThrownKind > 0;
            bool runs;
            bool shown;
            if (Cutscene == CutsceneEnd)
            {
                runs = false;
                shown = false;
            }
            else if (Cutscene < CutsceneAll)
            {
                runs = CutsceneTypes.Contains(obj.TypeName);
                shown = runs && obj.TypeName != "BOLT" && obj.TypeName != "BIGBOLT";
            }
            else
            {
                runs = Cutscene == CutsceneAllFlagged || !flagged;
                shown = !flagged;
            }

            obj.Visible = shown;
            if (!runs)
            {
                continue;
            }

            Vm.Run(obj);
            if (!obj.Dead)
            {
                Motion.Update(obj);
            }
        }

        if (Cutscene != 0 && CutsceneTarget is { Dead: false })
        {
            UpdateCutsceneCamera();
        }
    }

    /// <summary>The cutscene camera (0x477d94). Every shot but mode 11 looks at the target, 3 units
    /// above its origin. Mode 0 starts from (423, 85) and switches to mode 1 once the target is 12
    /// units away; modes 1 and 2 orbit behind the target (12 or 25 units), smoothly for 1800 ticks.</summary>
    private void UpdateCutsceneCamera()
    {
        var target = CutsceneTarget!.Position;
        var point = CameraPosition;
        var yaw = CameraYaw;
        var distance = CameraDistance;
        switch (CameraMode)
        {
            case 0:
                point = new Vector3(423f, 85f, MathF.Max(target.Z - 25f, -2260f));
                yaw = 90f - Heading(point, target);
                distance = Vector3.Distance(point, target);
                if (distance >= 12f)
                {
                    CameraMode = 1;
                }

                CameraTimer = CameraBlendTicks;
                break;
            case 1:
            case 2:
                distance = (CameraMode == 1 ? 12f : 25f) * 0.1f + CameraDistance * 0.9f;
                var around = FromAngle(CutsceneTarget.Yaw + 150f) * distance;
                point = new Vector3(target.X + around.X, target.Y + around.Y, -2258f);
                if (CameraMode == 2)
                {
                    point.Z = MathF.Min(CameraPosition.Z + ObjectMotion.Ticks, -2246f);
                }

                if (CameraTimer > 0)
                {
                    point = point * 0.2f + CameraPosition * 0.8f;
                }

                yaw = 120f - CutsceneTarget.Yaw;
                break;
            case 12:
                yaw = 90f - Heading(point, target);
                distance = Vector3.Distance(point, target);
                CameraTimer = CameraBlendTicks;
                break;
        }

        var pitch = CameraPitch;
        if (CameraMode != 11)
        {
            pitch = -float.RadiansToDegrees(MathF.Atan2(target.Z + 3f - point.Z, distance));
            if (pitch < -HalfTurn)
            {
                pitch += FullTurn;
            }
        }

        if (CameraMode is 1 or 2)
        {
            if (CameraTimer > 0)
            {
                pitch = pitch * 0.2f + CameraPitch * 0.8f;
                yaw = yaw * 0.2f + CameraYaw * 0.8f;
                CameraTimer--;
            }
            else
            {
                pitch = pitch * 0.7f + CameraPitch * 0.3f;
                yaw = yaw * 0.3f + CameraYaw * 0.7f;
            }
        }

        CameraDistance = distance;
        CameraPitch = pitch;
        CameraYaw = yaw;
        if (Cutscene != CutsceneBoss)
        {
            CameraPosition = point;
        }

        CameraPoint = point;
    }

    /// <summary>The end of a level (special_event 0-50): Kurt stops firing and the level is over
    /// (0x573b60), with NUKE and TORNADO, and the arena breaks up around Kurt as he rises.</summary>
    private void EndTheLevel()
    {
        if (LevelOver)
        {
            return;
        }

        LevelOver = true;
        Stats.TownFlags = GlobalFlags;
        Kurt.StopFiring();
        Mixer.Play("NUKE");
        Mixer.Play("TORNADO");
        EndLevel = new EndLevel(_sprites.GetAnimation(EndLevel.Takeoff).FrameCount);
        EndLevel.Finished += () => LevelEnded?.Invoke(GameOver.No);
        EndLevel.Start(this);
    }

    /// <summary>Spawns the aliens placed by the DTI records of type 2 (ARENA$TYPE_n scripts), and
    /// static objects (type 4, 0x43bd38: the pickups of level 8's GUNT_9).</summary>
    private void SpawnDtiAliens(string arena)
    {
        var controller = GetArenaState(arena).Controller;
        foreach (var record in ArenaRecords(arena))
        {
            if (record.Type == DtiAlien)
            {
                var key = $"{arena}${record.Name}_{record.Id}";
                Spawn(controller, record.Name, record.Position, record.Angle, record.Id, Cmi.AlienScripts.GetValueOrDefault(key), Spawning.Plain);
                continue;
            }

            if (record.Type != DtiStatic)
            {
                continue;
            }

            var script = Cmi.AlienScripts.GetValueOrDefault($"{arena}${record.Name}");
            var obj = Spawn(controller, record.Name, record.Position, record.Angle, -1, script, Spawning.Plain);
            if (obj != null)
            {
                obj.Flags |= DtiStaticFlags;
                obj.Health = 1;
            }
        }
    }

    /// <summary>Whether a spawned object gets the flags of spawn_flagged.</summary>
    public enum Spawning { Plain, Flagged }

    /// <summary>Spawns an object of a type in <paramref name="parent"/>'s arena; null for an unknown type.</summary>
    public MdkObject? Spawn(MdkObject parent, string type, Vector3 position, float yaw, int instance, int script, Spawning spawning)
    {
        var model = FindModel(parent.Arena, type);
        if (model == null)
        {
            return null;
        }

        var obj = new MdkObject
        {
            Arena = parent.Arena,
            TypeName = type,
            Model = model,
            InstanceId = instance >= 0 ? instance : _nextInstance,
            Position = position,
            SpawnPosition = position,
            PreviousPosition = position,
            Yaw = Wrap360(yaw),
        };
        if (instance < 0)
        {
            _nextInstance++;
        }

        if (spawning == Spawning.Flagged)
        {
            obj.Flags = SpawnFlaggedFlags;
        }

        Objects.Add(obj);
        Stats.CountEnemy(type, GameStats.Kill.Created);

        // The object type's init script runs once at creation.
        var initScript = Cmi.ObjectScripts.GetValueOrDefault($"{parent.Arena}${type}");
        if (initScript != 0)
        {
            obj.Restart = initScript;
            Vm.Run(obj);
        }

        obj.Restart = script;
        return obj;
    }

    /// <summary>spawn_box (opcode 159): an object whose model is a box of <paramref name="size"/>
    /// (model_create_box 0x404188, 8 corners, 12 triangles) showing a texture as an animated sprite
    /// (FIRE, PULSE). The triangles themselves aren't drawn.</summary>
    public MdkObject SpawnBox(MdkObject parent, Vector3 position, Vector3 size, string texture, int script)
    {
        // One model per size and texture: the models' poses and boxes are kept by model.
        if (!_boxModels.TryGetValue((size, texture), out var model))
        {
            model = _boxModels[(size, texture)] = BoxModel(size, texture);
        }

        var obj = new MdkObject
        {
            Arena = parent.Arena,
            TypeName = texture,
            Model = model,
            InstanceId = _nextInstance++,
            Position = position,
            SpawnPosition = position,
            PreviousPosition = position,
            Restart = script,
        };
        Objects.Add(obj);
        _boxes.RemoveAll(b => b.Dead);
        _boxes.Add(obj);
        return obj;
    }

    private readonly Dictionary<(Vector3, string), Model> _boxModels = [];

    /// <summary>model_create_box (0x404188): 8 corners, 12 triangles, not drawn.</summary>
    private static Model BoxModel(Vector3 size, string texture)
    {
        const int NoMaterial = -256;
        int[][] triangles =
        [
            [0, 1, 2], [1, 2, 3], [0, 4, 6], [0, 2, 6], [0, 1, 5], [0, 5, 4],
            [1, 5, 7], [1, 3, 7], [3, 2, 6], [3, 7, 6], [5, 4, 6], [5, 7, 6],
        ];
        var half = size * 0.5f;
        var bounds = new Box(-half, half);
        var part = new Model.Part
        {
            Vertices = Enumerable.Range(0, 8)
                .Select(i => new Vector3((i & 1) != 0 ? half.X : -half.X, (i & 2) != 0 ? half.Y : -half.Y, (i & 4) != 0 ? half.Z : -half.Z))
                .ToArray(),
            TriangleIndices = triangles.SelectMany(t => t).ToArray(),
            TriangleMaterials = Enumerable.Repeat(NoMaterial, triangles.Length).ToArray(),
            TriangleUvs = new Vector2[triangles.Length * 3],
            Bounds = bounds,
        };
        return new Model { Name = texture, PartList = [part], Bounds = bounds };
    }

    /// <summary>The objects made by spawn_box (dead ones too: skip them): their model's name is the
    /// texture their sprite shows.</summary>
    public List<MdkObject> Boxes => _boxes;

    public void Remove(MdkObject obj)
    {
        Rides.Lost(obj);
        if (obj.Attached is { Dead: false })
        {
            Remove(obj.Attached);
        }

        // The object's sounds go with it.
        Mixer.StopVoice(obj.LoopSound);
        Mixer.StopVoice(obj.TrackedVoice);
        if (_following.Remove(obj, out var voices))
        {
            foreach (var voice in voices)
            {
                Mixer.StopVoice(voice);
            }

            voices.Clear();
            _spareVoiceLists.Push(voices);
        }

        obj.LoopSound = 0;
        obj.TrackedVoice = 0;
        obj.Dead = true;
        Objects.Remove(obj);
    }

    /// <summary>Kills an object (object_kill 0x43d670): it switches to its death script if it has
    /// one, otherwise it explodes. <paramref name="yaw"/> is the direction the explosion faces.</summary>
    public void Kill(MdkObject obj, float yaw = 0f)
    {
        obj.Health = 0;
        NoteAlarmEnded(obj);
        if (obj.DeathScript == 0)
        {
            Explode(obj, yaw);
            return;
        }

        obj.MoveCommand = 0;
        obj.Flags |= MdkObject.FlagNotTarget;
        obj.Restart = obj.DeathScript;
        obj.WaitTime = 0f;
        obj.WaitResume = obj.DeathScript;
        obj.DeathScript = 0;
    }

    /// <summary>Plays the full-screen strike (0x4398f0) while the game waits.</summary>
    public void PlayStrikeScene(StrikeScene.Kind kind, StrikeScene.Plane plane)
    {
        var scene = new StrikeScene();
        Strike = scene;
        scene.Finished += () => StrikeSceneChanged?.Invoke(StrikeState.Ended);
        StrikeSceneChanged?.Invoke(StrikeState.Started);
        scene.Play(this, kind, plane);
    }

    /// <summary>Blows an object up (0x43d224): its explosion sound (opcode 25, or EXPLODE), a white
    /// flash by distance, its pieces flying off, and the global model 0 (EXPLODE, an animated
    /// texture) scaled to the object's height and turned to the camera.</summary>
    public void Explode(MdkObject obj, float yaw)
    {
        const float MinHeight = 0.1f;
        var bounds = GetWorldBounds(obj);
        var center = obj.Model != null ? bounds.Center() : obj.Position;
        PlaySoundAt(obj.Labels[0].Length != 0 ? obj.Labels[0] : ExplodeSound, center);
        FlashAt(center);
        BreakUp(obj);
        Remove(obj);
        var effect = SpawnExplosion(obj.Arena, center, 1f, yaw, ExplosionLift);
        if (effect?.Model != null)
        {
            effect.Scale = bounds.Size().Z / MathF.Max(effect.Model.Bounds.Size().Z, MinHeight) * ExplosionSizeFactor;
        }
    }

    /// <summary>The white flash of an explosion: 1000 / distance more, up to 150.</summary>
    private void FlashAt(Vector3 point)
    {
        var distance = Vector3.Distance(KurtPosition, point);
        if (distance <= 0f || Kurt.WhiteFlash >= ExplosionFlashMax)
        {
            return;
        }

        Kurt.WhiteFlash = MathF.Min(Kurt.WhiteFlash + MathF.Round(ExplosionFlash / distance), ExplosionFlashMax);
    }

    /// <summary>The pieces of an exploding object: the parts of its break-up model (model + "D") and
    /// slime drops, or 16 fire sparks when it has none.</summary>
    private void BreakUp(MdkObject obj)
    {
        const string BreakUpSuffix = "D";
        const float DropSpread = 1f / 16384f;
        var model = obj.Model != null ? FindModel(obj.Arena, obj.Model.Name + BreakUpSuffix) : null;
        if (model == null)
        {
            var (colour, range) = SparkColours[(int)Spark.Fire];
            Debris.Spark(obj.Arena, obj.Position, FireSparks, 1f, colour, range);
            return;
        }

        Debris.BreakUp(obj, model);

        // Slime drops thrown like the pieces, with gore (0x43d55e).
        if (Option == 0)
        {
            return;
        }

        var velocity = obj.Position - obj.PreviousPosition;
        for (var i = 0; i < GoreDrops; i++)
        {
            var thrown = new Vector3(Rng.Next(32768) - 0x4000, Rng.Next(32768) - 0x4000, Rng.Next(32768) - 0x800) * DropSpread;
            Effects.SpawnDrop(obj.Arena, obj.Position, velocity + thrown, GoreScale + Rng.Next(32768) * 5e-5f);
        }
    }

    /// <summary>Spawns an explosion (0x43cb2c): the global model 0 (EXPLODE), whose animated texture
    /// plays once, one frame per tick, pitched towards the camera <paramref name="lift"/> units above
    /// it unless the camera is close (within 5 units across and 8 up or down).</summary>
    public MdkObject? SpawnExplosion(string arena, Vector3 center, float scale, float yaw = 0f, float lift = DefaultLift)
    {
        var modelName = Cmi.ModelOffsets.Keys.FirstOrDefault() ?? "";
        var effect = Spawn(GetArenaState(arena).Controller, modelName, center, yaw, -1, 0, Spawning.Plain);
        if (effect?.Model == null)
        {
            return null;
        }

        effect.Flags |= MdkObject.FlagNotTarget | MdkObject.FlagNotSolid2;
        effect.EffectFrames = ExplosionFrames;
        var texture = effect.Model.Materials.Count != 0 ? FindTexture(arena, effect.Model.Materials[0]) : null;
        if (texture != null)
        {
            effect.EffectFrames = texture.FrameCount;
        }

        effect.Scale = scale;
        var horizontal = Distance2D(Eye, center);
        effect.Yaw = yaw == 0f ? Wrap360(Heading(center, Eye)) : yaw;
        var rise = Eye.Z + lift - center.Z;
        if (horizontal > ExplosionNear || MathF.Abs(rise) > ExplosionNearHeight)
        {
            effect.Pitch = float.RadiansToDegrees(MathF.Atan2(rise, horizontal));
        }

        effect.TextureFrame = 0;
        return effect;
    }

    /// <summary>An object fell far below its arena (0x43d884): it switches to its death script, put
    /// back above the floor, or it's removed.</summary>
    public void FallOut(MdkObject obj)
    {
        const float Restore = 150f;
        if (obj.DeathScript == 0)
        {
            Remove(obj);
            return;
        }

        Kill(obj);
        obj.Velocity.Z = 0f;
        obj.Position.Z = GetArenaFloor(obj.Arena) - Restore;
        obj.Flags &= ~MdkObject.FlagGravity;
    }

    /// <summary>Fires the chain gun for one tick (0x41a304): it hits the best target in front of Kurt
    /// (see <see cref="AimScore"/>) or, without one, the arena up to 150 units away.</summary>
    public void FireChainGun()
    {
        const int ShotsPerTick = 6;
        var origin = KurtPosition + new Vector3(0f, 0f, EyeHeight);

        // The super chain gun does 6 times the damage while its time lasts.
        var superGun = Kurt.Inventory.SuperChainGun > 0;
        var damage = ChainGunDamage * (superGun ? SuperChainGunFactor : 1);
        if (superGun)
        {
            Kurt.Inventory.TickSuperChainGun(1);
        }

        Stats.Shots += ShotsPerTick;
        MdkObject? best = null;
        var bestScore = -1f;
        var bestPart = -1;
        var bestBounds = default(Box);
        foreach (var obj in Objects)
        {
            if (obj.Dead || obj.Arena != CurrentArena || obj.Health == 0 || (obj.Flags & (MdkObject.FlagNotSolid | MdkObject.FlagNotTarget)) != 0)
            {
                continue;
            }

            // Weak parts are targets of their own.
            if ((obj.Flags & MdkObject.FlagWeakParts) != 0 && obj.Model != null)
            {
                var partBounds = obj.PartBounds();
                for (var i = 0; i < obj.Model.PartList.Count; i++)
                {
                    if ((obj.HiddenParts & (1 << i)) != 0 || !IsWeakPart(obj, i) || partBounds[i] is not { } part)
                    {
                        continue;
                    }

                    var partBox = GetWorldBounds(obj, part);
                    var partScore = AimScore(partBox, origin, bestScore);
                    if (partScore < 0f)
                    {
                        continue;
                    }

                    best = obj;
                    bestScore = partScore;
                    bestPart = i;
                    bestBounds = partBox;
                }

                if (best == obj)
                {
                    continue;
                }
            }

            var bounds = GetWorldBounds(obj);
            var score = AimScore(bounds, origin, bestScore);
            if (score >= 0f)
            {
                best = obj;
                bestScore = score;
                bestPart = -1;
                bestBounds = bounds;
            }
        }

        if (best != null)
        {
            ChainGunHit(best, bestPart, bestBounds, origin, damage, superGun ? Gun.Super : Gun.Normal);
            return;
        }

        var direction = FromAngle(TargetYaw) * ChainGunWallReach;
        var shot = new Vector3(direction.X, direction.Y, 0f);
        if (Raycast(origin, origin + shot) is not { } hit)
        {
            return;
        }

        var reacted = (HitGroupAt(hit, damage, HitChainGun, superGun ? -2 : -1) & 1) != 0;
        SparkAt(hit.Point - Vector3.Normalize(shot), 1, "", reacted ? Spark.Group : Spark.Hard);
    }

    /// <summary>A hit on the arena triangle a ray found; see <see cref="HitGroup"/>.</summary>
    public int HitGroupAt(RayHit hit, int amount, int kind, int weapon) => HitGroup(hit.Arena, hit.Group, amount, kind, weapon);

    /// <summary>A hit on a triangle of an arena group (0x40d560). <paramref name="kind"/> is what hit
    /// it (HitShot, HitChainGun, HitBlast...), <paramref name="weapon"/> the hit type its script sees
    /// (if_hit_weapon). The group's hit flags (opcode 168) and hit script (opcode 99) decide what
    /// happens. Returns 1 when the hit script ran, | 2 when the group stops such hits (0x20).</summary>
    public int HitGroup(string arena, int group, int amount, int kind, int weapon)
    {
        const int ScriptRan = 1;
        const int Stopped = 2;
        if (group < 1 || group > GroupCount)
        {
            return 0;
        }

        var state = GetArenaState(arena);
        var i = group - 1;
        var flags = state.GroupHitFlags[i];
        var always = false;
        var result = 0;
        if ((flags & kind) != 0)
        {
            if ((flags & GroupDestructible) != 0)
            {
                // A destructible group: its damaged version appears.
                _groups.SetState(arena, group, GroupShow);
            }

            if ((flags & GroupAlwaysHit) != 0)
            {
                always = true;
                amount = Math.Max(amount, 1);
            }

            if ((flags & GroupStopsHit) != 0)
            {
                result = Stopped;
            }
        }

        if (state.GroupHitScripts[i] == 0 || ((state.GroupHitMasks[i] & kind) == 0 && !always))
        {
            return result;
        }

        state.GroupCounters[i] += amount;

        // The script runs at once, from its start, in the arena's scratch object (0x45c9a0).
        var scratch = state.HitScripts;
        scratch.HitType = weapon;
        scratch.HitEvent = 0;
        scratch.WaitTime = 0f;
        scratch.GosubReturns.Clear();
        scratch.GosubRestarts.Clear();
        scratch.Restart = state.GroupHitScripts[i];
        Vm.Run(scratch);
        return result | ScriptRan;
    }

    /// <summary>The centres of a group's triangles, none while the group isn't solid (blasts).</summary>
    public IEnumerable<Vector3> GroupCenters(string arena, int group)
    {
        var data = Level.ArenaNamed(arena);
        var triangles = _groups.Get(arena, group);
        if (data == null || triangles == null || (triangles.State & TriangleGroups.State.NotSolid) != 0)
        {
            yield break;
        }

        foreach (var t in triangles.Triangles)
        {
            var i = t * 3;
            yield return (data.Vertices[data.TriangleIndices[i]] + data.Vertices[data.TriangleIndices[i + 1]] + data.Vertices[data.TriangleIndices[i + 2]]) / 3f;
        }
    }

    /// <summary>group_set_state on a group of an arena.</summary>
    public void SetGroupState(string arena, int group, int operation) => _groups.SetState(arena, group, operation);

    /// <summary>group_set_texture: the group's triangles take a material value.</summary>
    public void SetGroupTexture(string arena, int group, int value) => _groups.SetMaterial(arena, group, value);

    /// <summary>Shakes the screen at least this much (0x467f7c).</summary>
    public void RaiseShake(float amount)
    {
        Shake = MathF.Max(Shake, amount);
        ShakeRaised?.Invoke(amount);
    }

    /// <summary>The camera's line of sight from <paramref name="from"/> to <paramref name="to"/> against
    /// the objects of Kurt's arena that block it (camera_clearance 0x417ee8: flag 0x1000000, not
    /// 0x810): where it first meets one of their parts, or its end.</summary>
    public Vector3 ClipView(Vector3 from, Vector3 to)
    {
        const int Blocks = 0x1000000;
        const int Ignored = MdkObject.FlagNotSolid | MdkObject.FlagNotSolid2;
        var end = to;
        foreach (var obj in Objects)
        {
            if (obj.Dead || obj.Health == 0 || obj.Arena != CurrentArena || obj.Model == null || (obj.Flags & Blocks) == 0
                || (obj.Flags & Ignored) != 0)
            {
                continue;
            }

            foreach (var part in obj.PartBounds())
            {
                // A part the head is in doesn't count (0x45f588 returns 2).
                if (part is not { } bounds || GetWorldBounds(obj, bounds) is var box && box.Contains(from))
                {
                    continue;
                }

                if (box.SegmentEntry(from, end) is { } entry)
                {
                    end = entry;
                }
            }
        }

        return end;
    }

    /// <summary>Aim test of the chain gun (0x41ab2c): a box is a target when it's within its size +
    /// 140 units of the origin, inside a cone around Kurt's yaw that is wider for big and close boxes,
    /// and visible. Returns its score (squared distance, height counting double; lower is better), or -1.</summary>
    private float AimScore(Box bounds, Vector3 origin, float bestScore)
    {
        const float MinSize = 10f;
        var size = MathF.Max(bounds.Size().Length(), MinSize);
        var center = bounds.Center();
        var distance = Vector3.Distance(origin, center);
        if (distance > size + ChainGunReach)
        {
            return -1f;
        }

        var angle = Wrap360(Heading(origin, center) - TargetYaw);
        var cone = (size - 2f) * 90f / (size - 2f + distance);
        if (angle > cone && angle < FullTurn - cone)
        {
            return -1f;
        }

        var height = center.Z - KurtPosition.Z;
        var score = new Vector2(center.X - origin.X, center.Y - origin.Y).LengthSquared() + 4f * height * height;
        if (bestScore >= 0f && score > bestScore)
        {
            return -1f;
        }

        return Raycast(origin, center) != null ? -1f : score;
    }

    /// <summary>Whether part <paramref name="index"/> is a weak part (0x462384): its name starts with
    /// the prefix and has a digit right after it.</summary>
    public static bool IsWeakPart(MdkObject obj, int index)
    {
        var name = obj.Model!.PartList[index].Name;
        var length = obj.WeakPrefixLength;
        if (name.Length <= length || !char.IsAsciiDigit(name[length]))
        {
            return false;
        }

        var prefix = obj.WeakPrefix[..Math.Min(length, obj.WeakPrefix.Length)];
        return name.StartsWith(prefix, StringComparison.Ordinal);
    }

    /// <summary>The chain gun, or the super chain gun (6 times the damage, throws what it kills).</summary>
    private enum Gun { Normal, Super }

    private void ChainGunHit(MdkObject obj, int part, Box bounds, Vector3 origin, int damage, Gun gun)
    {
        var superGun = gun == Gun.Super;
        var center = bounds.Center();
        var direction = Heading(origin, center);
        Stats.ShotHits++;
        obj.HitEvent = -1;
        if (IsBeta && part < 0 && BetaHitPart(obj, origin) is var hit and >= 0)
        {
            obj.HitEvent = hit + 1;
        }

        if (part >= 0 && part < obj.PartHealth.Length)
        {
            obj.PartHealth[part] -= KurtDamage(obj, damage, obj.PartHealth[part]);
            if (obj.PartHealth[part] <= 0)
            {
                obj.PartHealth[part] = 0;
                obj.HitEvent = part + 1;
            }

            if (obj.PartMaxHealth[part] <= MaxBarHealth)
            {
                ShowBarValues(obj.PartHealth[part], obj.PartMaxHealth[part]);
            }
        }

        if (obj.Health < Indestructible)
        {
            obj.Health -= KurtDamage(obj, damage);
        }

        obj.HitType = superGun ? -2 : -1;
        obj.HitDirection = direction;
        if (obj.Health > 0)
        {
            if (part < 0 && obj.MaxHealth <= MaxBarHealth)
            {
                BarObject = obj;
                BarTime = 1f;
            }

            // Sparks on the side of the box facing Kurt.
            var toward = FromAngle(direction);
            var size = bounds.Size();
            var point = center - new Vector3(toward.X * size.X, toward.Y * size.Y, 0f) * 0.5f;
            SparkAt(point, 1, obj.Labels[1], obj.Indestructible && part < 0 ? Spark.Hard : Spark.Flesh);
            return;
        }

        obj.Health = 0;
        if (superGun)
        {
            // The super chain gun throws what it kills away.
            var push = FromAngle(direction) * SuperChainGunPush;
            obj.Velocity += new Vector3(push.X, push.Y, 0f);
        }

        Stats.CountEnemy(obj.TypeName, GameStats.Kill.Killed);
        Kill(obj, direction + HalfTurn);
    }

    /// <summary>The minecrawler reached the town: the screen shakes and a message says which town is
    /// gone (OOT_L1, "There goes Laguna Beach!", or OOT_L1A for the second town).</summary>
    private void FlattenTown()
    {
        RaiseShake(TownShake);
        var text = $"OOT_L{GameStats.IndexOf(Level.Number) + 1}";
        if ((GlobalFlags & FlagSecondTown) != 0)
        {
            text += "A";
            GlobalFlags |= FlagSecondTownFlattened;
        }
        else
        {
            GlobalFlags |= FlagTownFlattened;
        }

        Messages?.Push(text, Hud.Messages.FlagZoom | Hud.Messages.FlagFront, TownMessageSeconds);
    }

    /// <summary>Shows the health bar with these values for a second (the arena's own object, boss_bar).</summary>
    public void ShowBarValues(int health, int maxHealth)
    {
        BarObject = null;
        BarHealth = health;
        BarMax = maxHealth;
        BarTime = health > 0 ? 1f : 0f;
    }

    /// <summary>The bar's health and maximum, or zeros while it's hidden.</summary>
    public (int Health, int Max) GetBar()
    {
        if (BarTime <= 0f)
        {
            return (0, 0);
        }

        return BarObject != null ? (BarObject.Health, BarObject.MaxHealth) : (BarHealth, BarMax);
    }

    /// <summary>The bar goes after its time, or when its object dies or can't show a bar.</summary>
    private void UpdateBar()
    {
        if (BarTime <= 0f)
        {
            return;
        }

        BarTime -= Tick;
        if (BarObject is { Dead: true })
        {
            BarObject = null;
            BarTime = 0f;
            return;
        }

        var (health, max) = GetBar();
        if (max == 0 || max > MaxBarHealth || health < 1)
        {
            BarTime = 0f;
        }
    }

    /// <summary>Sparks where a shot hits (0x41e8f4); a ricochet sound (the object's, set by opcode 26,
    /// or RICO1-RICO3) every 4 ticks, always for bursts and when the arena has no effects (0x4052d4).</summary>
    public void SparkAt(Vector3 point, int count, string sound = "", Spark kind = Spark.Flesh)
    {
        const int RicochetEvery = 3;
        var quiet = !Effects.HasEffects(CurrentArena) && !Debris.HasPieces(CurrentArena);
        if (quiet || count > 1 || (_tickCount & RicochetEvery) == 0)
        {
            PlaySoundAt(sound.Length != 0 ? sound : Ricochets[Rng.Next(Ricochets.Length)], point);
        }

        var (colour, range) = kind != Spark.Flesh || Option != 0 ? SparkColours[(int)kind] : SparkFleshNoGore;
        Debris.Spark(CurrentArena, point, count, SparkSize, colour, range, kind == Spark.Hard ? SparkSlow : 1f);
    }

    /// <summary>A burst of fire sparks (the nuke's blast).</summary>
    public void SparkFire(string arena, Vector3 point)
    {
        var (colour, range) = SparkColours[(int)Spark.Fire];
        Debris.Spark(arena, point, FireSparks, 1f, colour, range);
    }

    /// <summary>A fan's fire spark (0x414230): half a unit big, still, for the updraft to lift.</summary>
    private void FanSpark(string arena, Vector3 point)
    {
        var (colour, range) = SparkColours[(int)Spark.Fire];
        Debris.Spark(arena, point, 1, SparkSize, colour, range, 1f, Debris.Launch.Still);
    }

    /// <summary>Where a platform that keeps Kurt is (flag 0x800000), or null.</summary>
    private static Vector3? KeeperAt(object owner) =>
        owner is MdkObject { Dead: false } obj && (obj.Flags & MdkObject.FlagStandable) != 0 ? obj.Position : null;

    /// <summary>Kurt's updraft (damp_gravity 0x469efc): his arena's fans, else the second arena's.
    /// E.g. LEVEL3's CHMO_6 shaft fan lifts him on through the floor doorway into HMO_8.</summary>
    private float KurtUpdraft(Vector3 feet, float vz, float dt)
    {
        var lifted = Fans.Query(CurrentArena, feet, vz, Fans.MaskKurt, dt);
        if (!float.IsNaN(lifted) || SecondArena == "")
        {
            return lifted;
        }

        return Fans.Query(SecondArena, feet, vz, Fans.MaskKurt, dt);
    }

    /// <summary>The frame count of an arena's (or the level's) texture, 0 without one.</summary>
    private int TextureFrames(string arena, string name) => FindTexture(arena, name)?.FrameCount ?? 0;

    /// <summary>The nearest solid triangle of one arena along a segment (effects and pieces).</summary>
    private RayHit? RayIn(string arena, Vector3 from, Vector3 to)
    {
        if (!_bsps.TryGetValue(arena, out var bsp))
        {
            return null;
        }

        var triangle = bsp.Segment(from, to, Bsp.SegmentMode.Any, out var point, Bsp.Clip.PassesThrough);
        return triangle == Bsp.None ? null : new RayHit(point, TriangleNormal(bsp.Arena, triangle), arena, TriangleGroup(bsp.Arena, triangle));
    }

    /// <summary>The triangles of an arena's group with their current material, for shattering.</summary>
    private Debris.Group? GroupFacets(string arena, int number)
    {
        var data = Level.ArenaNamed(arena);
        var group = _groups.Get(arena, number);
        if (data == null || group == null || number == 0)
        {
            return null;
        }

        var facets = new List<Debris.Facet>();
        foreach (var t in group.Triangles)
        {
            Vector3 Corner(int k) => data.Vertices[data.TriangleIndices[t * 3 + k]];
            Vector2 Uv(int k) => data.TriangleUvs[t * 3 + k];
            facets.Add(new Debris.Facet(Corner(0), Corner(1), Corner(2), Uv(0), Uv(1), Uv(2), group.Material ?? data.TriangleMaterials[t]));
        }

        return new Debris.Group(data.Materials, facets);
    }

    /// <summary>Starts the object's looping sound (set_loop_sound), stopping the previous one; an
    /// empty name just stops it.</summary>
    public void SetLoopSound(MdkObject obj, string name)
    {
        Mixer.StopVoice(obj.LoopSound);
        obj.LoopSound = 0;
        obj.LoopSoundName = name;
        if (name.Length == 0)
        {
            return;
        }

        // It only loops if the sound itself does (its SNI flag).
        obj.LoopSound = PlayOn(name, obj, SoundMixer.Start.New, Vector3.Zero);
    }

    /// <summary>wind_zone (opcode 224, 0x4579e0): while Kurt is inside one of the arena's type-9 boxes,
    /// the wind blows him along the yaw at the speed, sliding him on his back (damp_buttslide); in
    /// the air without sliding it only pulls him down twice as fast, his chute shut. Disabled, it ends
    /// the slide.</summary>
    public enum Wind { Off, On }

    public void WindZone(string arena, Wind wind, float yaw, float speed)
    {
        if (wind == Wind.Off)
        {
            Kurt.StopSlide();
            return;
        }

        foreach (var record in ArenaRecords(arena))
        {
            if (record.Type != DtiWind || !new Box(record.Position, record.BoxEnd).Contains(KurtPosition))
            {
                continue;
            }

            if (Kurt.Sliding || Kurt.OnFloor)
            {
                Kurt.StartSlide();
                Kurt.Yaw = yaw;
                Kurt.SlideAccel(FromAngle(yaw) * speed, Tick);
            }
            else
            {
                Kurt.Blow(Tick);
            }

            return;
        }
    }

    /// <summary>Picks one of the arena's type-8 waypoints at random (pick_waypoint8 0x460a6c) as the
    /// destination (movement command 221). Every waypoint weighs at least 1: 500 - distance (2D), plus
    /// 200 - distance to Kurt when Kurt is closer than 200, plus 250 when the object is closer to Kurt
    /// than to the waypoint and the waypoint is farther from Kurt than from the object, less half the
    /// height difference. Mode 0 takes the waypoints 50-500 units away, mode 1 those whose id is
    /// within 3 of the nearest one.</summary>
    public void PickWaypoint8(MdkObject obj, int mode)
    {
        const int PickCommand = 221;
        obj.MoveCommand = 0;
        var records = OfType(ArenaRecords(obj.Arena), DtiWaypoint);
        var nearestId = 0;
        if (mode == 1)
        {
            var nearest = 999999f;
            foreach (var record in records)
            {
                var distance = Vector3.Distance(obj.Position, record.Position);
                if (distance < nearest)
                {
                    nearest = distance;
                    nearestId = record.Id;
                }
            }
        }

        var weights = _weights;
        weights.Clear();
        var total = 0;
        foreach (var record in records)
        {
            var spot = record.Position;
            var distance = Distance2D(spot, obj.Position);
            var weight = 0;
            if ((mode == 1 && Math.Abs(record.Id - nearestId) <= 3) || (mode != 1 && distance >= 50f && distance <= 500f))
            {
                var toKurt = Distance2D(spot, KurtPosition);
                weight = Round(500f - distance);
                if (toKurt < 200f)
                {
                    weight = Round(weight + 200f - toKurt);
                }

                if (Distance2D(obj.Position, KurtPosition) < distance && toKurt > distance)
                {
                    weight += 250;
                }

                weight = Round(weight - MathF.Abs(spot.Z - obj.Position.Z) * 0.5f);
                weight = Math.Max(weight, 1);
            }

            weights.Add(weight);
            total += weight;
        }

        if (total == 0)
        {
            return;
        }

        var pick = Rng.Next(total);
        for (var i = 0; i < records.Count; i++)
        {
            pick -= weights[i];
            if (pick >= 0)
            {
                continue;
            }

            obj.MoveCommand = PickCommand;
            obj.MoveDestination = records[i].Position;
            obj.Waypoint = obj.MoveDestination;
            obj.ContactFlags &= ~MdkObject.ContactStuck;
            return;
        }
    }

    /// <summary>A type-5 spot to advance to (find_advance_spot, like find_cover_spot without the
    /// visibility tests and with 2D distances): 5-400 units away, no farther from the object than Kurt
    /// is and closer to Kurt than the object. It becomes the destination (movement command 197).</summary>
    public void FindAdvanceSpot(MdkObject obj)
    {
        const int AdvanceCommand = 197;
        obj.MoveCommand = 0;
        var toKurt = Distance2D(obj.Position, KurtPosition);
        var spots = _spots;
        spots.Clear();
        foreach (var record in OfType(ArenaRecords(obj.Arena), DtiCover))
        {
            var spot = record.Position;
            var distance = Distance2D(spot, obj.Position);
            if (distance < 5f || distance > 400f || distance > toKurt || Distance2D(spot, KurtPosition) >= toKurt)
            {
                continue;
            }

            spots.Add(spot);
        }

        if (spots.Count == 0)
        {
            return;
        }

        obj.MoveCommand = AdvanceCommand;
        obj.MoveDestination = spots[Rng.Next(spots.Count)];
        obj.Waypoint = obj.MoveDestination;
        obj.ContactFlags &= ~MdkObject.ContactStuck;
    }

    /// <summary>Looks for cover (find_cover_spot): a type-5 spot of the object's arena, 9-400 units
    /// away, no farther than Kurt, closer to Kurt than the object, hidden from Kurt but visible to the
    /// object. One of the 3 nearest is chosen (nearer ones more likely) and the object goes there
    /// (movement command 43); otherwise it stops.</summary>
    public void FindCoverSpot(MdkObject obj)
    {
        const int CoverCommand = 43;
        var nearerFirst = NearerFirst;
        var up = new Vector3(0f, 0f, EyeHeight);
        var toKurt = obj.DistanceTo(KurtPosition);
        var spots = _spots;
        spots.Clear();
        foreach (var record in OfType(ArenaRecords(obj.Arena), DtiCover))
        {
            var spot = record.Position;
            var distance = obj.DistanceTo(spot);
            if (distance < 9f || distance > 400f || distance > toKurt || Vector3.Distance(spot, KurtPosition) >= toKurt)
            {
                continue;
            }

            if (Raycast(KurtPosition + up, spot + up) == null || Raycast(obj.Position + up, spot + up) != null)
            {
                continue;
            }

            spots.Add(spot);
        }

        obj.MoveCommand = 0;
        if (spots.Count == 0)
        {
            return;
        }

        spots.Sort((a, b) => obj.DistanceTo(a).CompareTo(obj.DistanceTo(b)));
        var choice = spots.Count >= 3 ? nearerFirst[Rng.Next(nearerFirst.Length)] : Rng.Next(spots.Count);
        var destination = spots[Math.Min(choice, spots.Count - 1)];
        obj.MoveCommand = CoverCommand;
        obj.MoveDestination = destination;
        obj.Waypoint = destination;
        obj.Path = 0;
    }

    /// <summary>The records of a type, in a scratch list (valid until the next call).</summary>
    private List<Dti.Record> OfType(List<Dti.Record> records, uint type)
    {
        _spotRecords.Clear();
        foreach (var record in records)
        {
            if (record.Type == type)
            {
                _spotRecords.Add(record);
            }
        }

        return _spotRecords;
    }

    /// <summary>Plays a sound at a point, independently of any object.</summary>
    public void PlaySoundAt(string name, Vector3 point) => Mixer.PlayAt(name, point);

    /// <summary>The SW_EWJ easter egg (0x46d718): a holy cow (SW_HCOW) drops from 100 units above the
    /// enemy Kurt faces (see <see cref="CowTarget"/>), or above Kurt himself.</summary>
    private void DropCow()
    {
        Mixer.Play("COW", SoundMixer.Start.Restart);
        var target = CowTarget();
        var point = target?.Position ?? KurtPosition;
        var cow = Spawn(GetArenaState(CurrentArena).Controller, "SW_HCOW", point + new Vector3(0f, 0f, CowHeight), 0f, -1, 0, Spawning.Plain);
        if (cow == null)
        {
            return;
        }

        cow.Flags = CowFlags;
        cow.Health = Indestructible;
        cow.Velocity.Z = -cow.Gravity;
        cow.CowTarget = target;
        cow.ParameterTimer = CowFuse;
    }

    /// <summary>The cow's target: the object of Kurt's arena within 600 units with the lowest score
    /// (its distance, + 400 beyond 30° of Kurt's yaw, + 1000 beyond 50°) that has open sky 100 units
    /// above it and no cow on it already.</summary>
    private MdkObject? CowTarget()
    {
        MdkObject? best = null;
        var bestScore = float.PositiveInfinity;
        foreach (var obj in Objects)
        {
            if (obj.Dead || obj.Arena != CurrentArena || (obj.Flags & (MdkObject.FlagNotSolid | MdkObject.FlagNotTarget)) != 0
                || obj.Health >= Indestructible)
            {
                continue;
            }

            var distance = obj.DistanceTo(KurtPosition);
            if (distance > CowRange)
            {
                continue;
            }

            var angle = MathF.Abs(WrapAngle(KurtYaw - Heading(KurtPosition, obj.Position)));
            var score = distance + (angle < 30f ? 0f : angle < 50f ? 400f : 1000f);
            if (score > bestScore)
            {
                continue;
            }

            var above = obj.Position + new Vector3(0f, 0f, EyeHeight);
            if (Raycast(above, above + new Vector3(0f, 0f, CowHeight)) != null)
            {
                continue;
            }

            if (Objects.Any(other => (other.Flags & MdkObject.FlagCow) != 0 && other.CowTarget == obj))
            {
                continue;
            }

            best = obj;
            bestScore = score;
        }

        return best;
    }

    /// <summary>Kurt takes the pickups he runs through (damp_collect_pickups 0x46c448): the segment he
    /// moved along this tick crosses a pickup's bounds, grown by 1 unit (and 5 downwards).</summary>
    public void CollectPickups()
    {
        const float ShrinkTicks = 30f;
        using var objects = ListCopy<MdkObject>.Of(Objects);
        foreach (var obj in objects)
        {
            if (obj.Dead || obj.Arena != CurrentArena || (obj.Flags & MdkObject.FlagPickup) == 0
                || (obj.Flags & (MdkObject.FlagCollected | MdkObject.FlagNotSolid)) != 0)
            {
                continue;
            }

            var world = GetWorldBounds(obj);
            var bounds = new Box(world.Min - new Vector3(1f, 1f, 5f), world.Max + new Vector3(1f, 1f, 1f));
            if (!bounds.Contains(KurtPosition) && bounds.SegmentEntry(_previousKurtPosition, KurtPosition) == null)
            {
                continue;
            }

            var sound = Kurt.Collect(obj.TypeName);
            if (sound.Length == 0)
            {
                continue;
            }

            Mixer.Play(sound, SoundMixer.Start.Restart);
            if (obj.TypeName == "SW_EWJ")
            {
                DropCow();
            }

            obj.Flags |= MdkObject.FlagCollected | Items.FlagThrown;
            obj.ParameterTimer = ShrinkTicks;
            obj.Velocity = Vector3.Zero;
            obj.Flags &= ~MdkObject.FlagGravity;
            if (obj.Attached != null)
            {
                Remove(obj.Attached);
                obj.Attached = null;
            }
        }
    }

    /// <summary>Spawns a connector (a door) between the object's arena and another (spawn_connector).
    /// A connector of that type already linking both arenas is moved into this arena instead, unless
    /// Kurt can see it.</summary>
    public void SpawnConnector(MdkObject obj, string type, Vector3 position, float yaw, int instance, string otherArena, int script)
    {
        const int DoorFlags = 0x1108000;
        if (otherArena == NoArena)
        {
            return;
        }

        foreach (var other in Objects)
        {
            if (other.Dead || other.TypeName != type
                || !((other.Arena == obj.Arena && other.Connects == otherArena) || (other.Arena == otherArena && other.Connects == obj.Arena)))
            {
                continue;
            }

            // A door Kurt can see (his arena or the second one) stays where it is.
            if (other.Arena != CurrentArena && other.Arena != SecondArena)
            {
                other.Arena = obj.Arena;
                other.Connects = otherArena;
            }

            return;
        }

        var door = Spawn(obj, type, position, yaw, instance, script, Spawning.Plain);
        if (door == null)
        {
            return;
        }

        door.Flags |= DoorFlags;
        door.Connects = otherArena;
        Behaviors.SetupDoor(door);
    }

    /// <summary>Fires a projectile (fire): the global model starts at a reference point of the object
    /// (origin [0, index]) or at the centre of one of its parts ([1, name]), flying along the object's
    /// yaw and pitch, and runs the script.</summary>
    public MdkObject? Fire(MdkObject obj, object?[] origin, string bullet, int script)
    {
        const int ProjectileCommand = 61;
        var start = I(origin[0]) == 0 ? obj.ReferencePoint(I(origin[1])) : obj.PartCenter(obj.FindPart(origin[1] as string ?? ""));
        var shot = Spawn(obj, bullet, start, obj.Yaw, -1, 0, Spawning.Plain);
        if (shot == null)
        {
            return null;
        }

        shot.MoveCommand = ProjectileCommand;
        shot.Flags |= ProjectileFlags;
        shot.Pitch = obj.Pitch;
        shot.Restart = script;
        return shot;
    }

    public void HurtKurt(int damage)
    {
        // Riding the XD2, it takes the hits (0x46a498).
        if (!Rides.TakesHits())
        {
            Kurt.Hurt(damage);
            return;
        }

        var ridden = Rides.Ridden!;
        if (ridden.Health >= Indestructible)
        {
            return;
        }

        ridden.Health -= damage;
        if (ridden.Health < 1)
        {
            Kill(ridden);
        }
    }

    /// <summary>Whether Kurt looks at an object (0x460730): it's min-max range away, within a cone
    /// around his yaw that narrows from 90° next to him to <paramref name="cone"/> at the max range,
    /// and in sight.</summary>
    public bool KurtLooksAt(MdkObject obj, float minRange, float maxRange, float cone)
    {
        var distance = Vector3.Distance(KurtPosition, obj.Position);
        if (distance < minRange || distance > maxRange)
        {
            return false;
        }

        var angle = Wrap360(MathF.Abs(KurtYaw - Heading(KurtPosition, obj.Position)));
        var allowed = (cone - 90f) / maxRange * distance + 90f;
        if (angle > allowed && angle < FullTurn - allowed)
        {
            return false;
        }

        return Raycast(KurtPosition + new Vector3(0f, 0f, EyeHeight), obj.Position + new Vector3(0f, 0f, TargetHeight)) == null;
    }

    /// <summary>The object Kurt stands on (0x573b84), if any.</summary>
    public MdkObject? GetKurtPlatform() => Kurt.OnFloor ? Kurt.Platform as MdkObject : null;

    /// <summary>What Kurt collides with (damp_collide_move): the visible parts of the objects of his
    /// arena that are alive, walls or floors (<see cref="MdkObject.Footing"/>), but the one he rides
    /// and doors' LOCK parts.</summary>
    private IReadOnlyList<Solids.Solid> SolidsWithin(Box region)
    {
        // Kurt uses each answer at once: one list does.
        var solids = _solidsWithin;
        solids.Clear();
        foreach (var obj in Objects)
        {
            if (obj.Dead || obj.Arena != CurrentArena || obj.Health == 0 || obj.Model == null || obj.Footing is not { } footing
                || obj == Rides.Ridden || !GetWorldBounds(obj).Intersects(region))
            {
                continue;
            }

            var parts = obj.PartBounds();
            for (var i = 0; i < parts.Length; i++)
            {
                if (((obj.HiddenParts | obj.LockParts) & (1 << i)) != 0 || parts[i] is not { } part)
                {
                    continue;
                }

                solids.Add(new Solids.Solid(GetWorldBounds(obj, part), obj, footing));
            }
        }

        return solids;
    }

    private readonly List<Solids.Solid> _solidsWithin = [];

    /// <summary>Contact damage (touch_damage 0x45cf60): targets 1 hurts Kurt once per visible part
    /// touching him, 2 hurts the other objects touching its box (hit event -3, hit type -4). With
    /// flags 1 the object dies when it hits. Returns whether it hit.</summary>
    public bool TouchDamage(MdkObject obj, int targets, int damage, int flags)
    {
        const int HurtsKurt = 1;
        const int HurtsObjects = 2;
        const int DiesOnHit = 1;
        const int Untouchable = 0x820;
        var box = GetWorldBounds(obj);
        var hit = false;
        if ((targets & HurtsKurt) != 0)
        {
            var kurtBox = GetKurtBox();
            if (box.Intersects(kurtBox))
            {
                var partBounds = obj.PartBounds();
                for (var i = 0; i < partBounds.Length; i++)
                {
                    if ((obj.HiddenParts & (1 << i)) != 0 || partBounds[i] is not { } part || !GetWorldBounds(obj, part).Intersects(kurtBox))
                    {
                        continue;
                    }

                    Kurt.Hurt(damage);
                    hit = true;
                }
            }
        }

        if ((targets & HurtsObjects) != 0)
        {
            using var copy = ListCopy<MdkObject>.Of(Objects);
            foreach (var other in copy)
            {
                if (other == obj || other.Dead || other.Arena != obj.Arena || (other.Flags & Untouchable) != 0)
                {
                    continue;
                }

                if (!box.Intersects(GetWorldBounds(other)))
                {
                    continue;
                }

                hit = true;
                other.HitEvent = -3;
                other.HitType = -4;
                other.HitDirection = obj.Yaw;
                if (other.Health < Indestructible)
                {
                    other.Health -= damage;
                }

                if (other.Health < 1)
                {
                    Kill(other);
                }
            }
        }

        if (hit && (flags & DiesOnHit) != 0)
        {
            Kill(obj);
        }

        return hit;
    }

    /// <summary>push_kurt: knocks Kurt down (unless he's already down), pushed along the object's frame
    /// (mode 0: [0, a, b, up]) or away from it ([mode, a, up]), and up added to his vertical speed.</summary>
    public void PushKurt(MdkObject obj, object?[] args)
    {
        if (Kurt.Knocked || Kurt.Health == 0)
        {
            return;
        }

        Vector2 push;
        float up;
        if (I(args[0]) == 0)
        {
            var (s, c) = MathF.SinCos(float.DegreesToRadians(obj.Yaw));
            push = new Vector2(-F(args[1]) * c - F(args[2]) * s, -F(args[2]) * c - F(args[1]) * s);
            up = F(args[3]);
        }
        else
        {
            var away = new Vector2(KurtPosition.X - obj.Position.X, KurtPosition.Y - obj.Position.Y);
            push = away != Vector2.Zero ? Vector2.Normalize(away) * F(args[1]) : new Vector2(F(args[1]), 0f);
            up = F(args[2]);
        }

        // The push is added once per tick of the frame, in units per tick.
        push *= Tick * TicksPerSecond;
        Kurt.KnockDown(push);
        Kurt.AddVerticalSpeed(up);
    }

    /// <summary>Kurt's bounding box.</summary>
    public Box GetKurtBox() => new(
        KurtPosition - new Vector3(KurtHalfWidth, KurtHalfWidth, 0f),
        KurtPosition + new Vector3(KurtHalfWidth, KurtHalfWidth, KurtHeight));

    /// <summary>Lowest point (Z) of an arena's geometry.</summary>
    public float GetArenaFloor(string arena) => _floors.GetValueOrDefault(arena, NoArenaFloor);

    /// <summary>The arenas rays meet: Kurt's and the second, or every arena before he has one
    /// (arrays kept: no list per ray).</summary>
    private string[] RayArenas()
    {
        if (CurrentArena.Length == 0)
        {
            if (_bspNames.Length != _bsps.Count)
            {
                _bspNames = [.. _bsps.Keys];
            }

            return _bspNames;
        }

        _rayPair[0] = CurrentArena;
        _rayPair[1] = SecondArena;
        return _rayPair;
    }

    private string[] _bspNames = [];
    private readonly string[] _rayPair = new string[2];

    /// <summary>The nearest arena triangle a segment crosses, or null. Only Kurt's arena and the second
    /// one (loaded, active or not) stop rays (0x421680); the others are passed through.</summary>
    public RayHit? Raycast(Vector3 from, Vector3 to)
    {
        var arenas = RayArenas();
        RayHit? best = null;
        var bestDistance = float.MaxValue;
        foreach (var name in arenas)
        {
            if (!_bsps.TryGetValue(name, out var bsp))
            {
                continue;
            }

            var triangle = bsp.Segment(from, to, Bsp.SegmentMode.Any, out var point, Bsp.Clip.PassesThrough);
            var distance = Vector3.DistanceSquared(from, point);
            if (triangle == Bsp.None || distance >= bestDistance)
            {
                continue;
            }

            bestDistance = distance;
            best = new RayHit(point, TriangleNormal(bsp.Arena, triangle), name, TriangleGroup(bsp.Arena, triangle));
        }

        return best;
    }

    /// <summary>Whether a segment crosses an up-facing face (|nz| ≥ 0.5) of one arena (0x421708):
    /// the scripts' floor probe, which other objects (platforms) don't stop.</summary>
    public bool CrossesFloor(string arena, Vector3 from, Vector3 to)
    {
        if (!_bsps.TryGetValue(arena, out var bsp))
        {
            return false;
        }

        return bsp.Segment(from, to, Bsp.SegmentMode.Floor, out _, Bsp.Clip.PassesThrough) != Bsp.None;
    }

    /// <summary>A box of half extents <paramref name="half"/> swept without sliding against Kurt's
    /// arena, then the second one (bsp_sweep_box, the mortar round in 0x462708). Faces stop it only
    /// from their front: a wall seen from its back is passed (LEVEL6 OLYM_3's opening). The point is
    /// the box centre at the contact, the normal the face's plane.</summary>
    public RayHit? Sweep(Vector3 from, Vector3 to, Vector3 half)
    {
        var arenas = RayArenas();
        foreach (var name in arenas)
        {
            if (!_bsps.TryGetValue(name, out var bsp))
            {
                continue;
            }

            var triangle = bsp.SweepBox(from, to, half, 0, 0f, out var point, out var node);
            if (triangle == Bsp.None)
            {
                continue;
            }

            return new RayHit(point, bsp.Arena.Nodes[node].Normal, name, TriangleGroup(bsp.Arena, triangle));
        }

        return null;
    }

    private static Vector3 TriangleNormal(Arena arena, int triangle)
    {
        var a = arena.Vertices[arena.TriangleIndices[triangle * 3]];
        var b = arena.Vertices[arena.TriangleIndices[triangle * 3 + 1]];
        var c = arena.Vertices[arena.TriangleIndices[triangle * 3 + 2]];
        var normal = Vector3.Cross(b - a, c - a);
        return normal == Vector3.Zero ? Vector3.UnitZ : Vector3.Normalize(normal);
    }

    /// <summary>The group of an arena triangle (the top byte of its flags).</summary>
    private static int TriangleGroup(Arena arena, int triangle)
    {
        const int GroupShift = 24;
        return (int)(arena.TriangleFlags[triangle] >> GroupShift);
    }

    /// <summary>The BSP of an arena, for objects' moves.</summary>
    public Bsp? BspOf(string arena) => _bsps.GetValueOrDefault(arena);

    /// <summary>Destination near the target (move_near_target): the target offset by forward and side
    /// × 1% of the distance, in the frame of the direction from the object to the target.</summary>
    public Vector3 NearTargetDestination(MdkObject obj, float forward, float side)
    {
        var direction = Heading(obj.Position, TargetPosition);
        var offset = Rotated(new Vector2(forward, side), direction) * 0.01f * obj.DistanceTo(TargetPosition);
        return new Vector3(TargetPosition.X + offset.X, TargetPosition.Y + offset.Y, TargetPosition.Z + obj.HeightOffset);
    }

    /// <summary>Whether the sender may command the receiver (command_objects, if_count_objects): the
    /// receiver has no other leader of a higher priority, and obeys the sender's priority.</summary>
    public static bool MayCommand(MdkObject sender, MdkObject receiver)
    {
        var leader = receiver.Leader;
        if (leader != null && leader != sender && !leader.Dead && sender.Priority <= leader.Priority)
        {
            return false;
        }

        return receiver.ObeyLevel >= sender.Priority;
    }

    /// <summary>Sends a command to objects (command_objects 0x440384). Commands: 1 join a formation
    /// around the sender, 7 goto target, 0xFC gosub target, 43 go to destination. Selectors: 2 all of
    /// a type, 3 all, 4 the first of a type, 5 of a type with instance id, 6 of a type within param
    /// with a line of sight, 7 of a type following the sender, 8 all following the sender, 9 the
    /// linked object, 10 of a type with Y >= param.</summary>
    public void CommandObjects(MdkObject sender, int command, int target, Vector3 destination, int selector, string type, float param, int id)
    {
        const float SightHeight = 8f;
        var formationIndex = 0;
        if (selector == 9)
        {
            if (sender.Linked is { Dead: false })
            {
                CommandObject(sender, sender.Linked, command, target, destination, formationIndex);
            }

            return;
        }

        var up = new Vector3(0f, 0f, SightHeight);
        foreach (var receiver in GetArenaObjects(sender))
        {
            if (selector is not (3 or 8) && !receiver.TypeName.Equals(type, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            if (!MayCommand(sender, receiver))
            {
                continue;
            }

            if (selector is 7 or 8 && receiver.Leader != sender)
            {
                continue;
            }

            if (selector == 5 && receiver.InstanceId != id)
            {
                continue;
            }

            if (selector == 6 && (sender.DistanceTo(receiver.Position) > param || Raycast(sender.Position + up, receiver.Position + up) != null))
            {
                continue;
            }

            if (selector == 10 && receiver.Position.Y < param)
            {
                continue;
            }

            if ((command == 7 && receiver.CommandTarget == target) || receiver.Health == 0)
            {
                continue;
            }

            if (CommandObject(sender, receiver, command, target, destination, formationIndex))
            {
                formationIndex++;
            }

            if (selector == 4)
            {
                return;
            }
        }
    }

    /// <summary>Applies a command to one object (0x4405d0). Returns true when it joined a formation.</summary>
    private bool CommandObject(MdkObject sender, MdkObject receiver, int command, int target, Vector3 destination, int formationIndex)
    {
        switch (command)
        {
            case 1:
                // Formation places alternate left and right, 5 units apart (wider for XE).
                var wide = receiver.TypeName.Equals("XE", StringComparison.OrdinalIgnoreCase);
                var spread = wide ? 4f : 1f;
                var back = wide ? 2.5f : 1f;
                var side = (formationIndex & 1) != 0 ? 1f : -1f;
                receiver.Waypoint = new Vector3(side * (formationIndex / 2 + 1) * 5f * spread, back * -4f, 8f);
                receiver.MoveDestination = ObjectMotion.FormationPosition(sender, receiver.Waypoint);
                receiver.Leader = sender;
                receiver.MoveCommand = 1;
                receiver.Path = 0;
                return true;
            case 7:
                receiver.WaitTime = 0f;
                receiver.Restart = target;
                receiver.WaitResume = target;
                receiver.Leader = sender;
                receiver.GosubReturns.Clear();
                receiver.GosubRestarts.Clear();
                receiver.LevelTimers[0] = 0f;
                receiver.CommandTarget = target;
                break;
            case 0xFC:
                if (receiver.GosubReturns.Count < ScriptVm.GosubDepth)
                {
                    receiver.GosubReturns.Add(receiver.Restart);
                    receiver.GosubRestarts.Add(receiver.Restart);
                    receiver.WaitTime = 0f;
                    receiver.Restart = target;
                    receiver.WaitResume = target;
                    receiver.Leader = sender;
                    receiver.LevelTimers[receiver.GosubReturns.Count] = 0f;
                }

                break;
            case 43:
                if (receiver.Arena == CurrentArena)
                {
                    receiver.MoveDestination = destination;
                    receiver.Leader = sender;
                    receiver.MoveCommand = 43;
                    receiver.Path = 0;
                    Motion.PlanMove(receiver);
                }

                break;
        }

        return false;
    }

    /// <summary>World bounds of an object in its current pose (obj+0x198), or of a model-space box,
    /// turned by its yaw.</summary>
    public Box GetWorldBounds(MdkObject obj, Box? bounds = null)
    {
        if (bounds == null && obj.Model == null)
        {
            return new Box(obj.Position, obj.Position);
        }

        var box = bounds ?? obj.PoseBounds();
        var world = new Box(obj.Position, obj.Position);
        for (var i = 0; i < 8; i++)
        {
            var corner = new Vector3((i & 1) != 0 ? box.Max.X : box.Min.X, (i & 2) != 0 ? box.Max.Y : box.Min.Y, (i & 4) != 0 ? box.Max.Z : box.Min.Z);
            var point = obj.Position + Vector3.UnitZ * obj.Lift + RotatedZ(corner * obj.Scale, obj.Yaw);
            world = i == 0 ? new Box(point, point) : world.Expand(point);
        }

        return world;
    }

    /// <summary>Objects of the arena of an object, alive, other than it (for commands and counts).</summary>
    public List<MdkObject> GetArenaObjects(MdkObject obj) => Objects.Where(o => o != obj && !o.Dead && o.Arena == obj.Arena).ToList();

    /// <summary>The animation of a script operand: one stored in the CMI file, or (when its first u32
    /// is 0) the arena animation named after it.</summary>
    public ModelAnimation? GetAnimation(MdkObject obj, int offset)
    {
        if (offset == 0)
        {
            return null;
        }

        // The 1996 demo's scripts point at animations in the file.
        if (IsBeta)
        {
            return Cmi.GetBetaAnimation(offset);
        }

        var bytes = Cmi.Bytes;
        if (Bin.U32(bytes, offset) == 0)
        {
            return FindArenaAnimation(obj.Arena, Bin.Ascii(bytes, offset + 4, AnimationNameLength));
        }

        if (!_animations.TryGetValue(offset, out var animation))
        {
            animation = _animations[offset] = ModelAnimation.Parse($"{CmiAnimationPrefix}{offset:x}", bytes, offset);
        }

        return animation;
    }

    private readonly ScriptDecoder _decoder;
    /// <summary>anim_once, anim_loop (the animation) and door_set_anims (two): their animation operands.</summary>
    private static readonly Dictionary<int, int> AnimationOperands = new() { [3] = 1, [59] = 1, [150] = 2 };

    /// <summary>The models of the level's CMI (any arena's objects may have them).</summary>
    public IEnumerable<Model> LevelModels() => Cmi.ModelOffsets.Keys.Select(Cmi.GetModel).OfType<Model>();

    /// <summary>Bakes the poses and boxes of every model with every animation that moves it: the
    /// arenas' animations and those the scripts play (a level's load: nothing is baked while playing).</summary>
    public void PreparePoses()
    {
        var animations = Level.Arenas.SelectMany(a => a.Animations.Values).Concat(Items.Animations()).ToList();
        var anyObject = new MdkObject();
        foreach (var ins in _decoder.Decoded)
        {
            for (var i = 0; i < AnimationOperands.GetValueOrDefault(ins.Opcode); i++)
            {
                if (ins.Operands[i] is int offset && offset != 0 && Bin.U32(Cmi.Bytes, offset) != 0
                    && GetAnimation(anyObject, offset) is { } animation)
                {
                    animations.Add(animation);
                }
            }
        }

        foreach (var model in LevelModels().Concat(Level.Arenas.SelectMany(a => a.Models.Values)).Distinct())
        {
            MdkObject.Prepare(model, null);
            foreach (var animation in animations.Distinct())
            {
                if (animation.Animates(model))
                {
                    MdkObject.Prepare(model, animation);
                }
            }
        }
    }

    /// <summary>An animation of an arena's models, by name (arena_find_animation 0x440adc).</summary>
    public ModelAnimation? FindArenaAnimation(string arena, string name) =>
        Level.Mto.Has(arena) ? Level.Mto.GetArena(arena).Animations.GetValueOrDefault(name) : null;

    /// <summary>A global model of the CMI, or one of the arena's.</summary>
    public Model? FindModel(string arena, string type)
    {
        var model = Cmi.GetModel(type);
        if (model != null)
        {
            return model;
        }

        return Level.Mto.Has(arena) ? Level.Mto.GetArena(arena).Models.GetValueOrDefault(type) : null;
    }

    /// <summary>A texture of an arena, or of the level.</summary>
    private Texture? FindTexture(string arena, string name)
    {
        if (Level.Mto.Has(arena) && Level.Mto.GetArena(arena).Textures.Textures.TryGetValue(name, out var texture))
        {
            return texture;
        }

        return Level.LevelTextures.Textures.GetValueOrDefault(name);
    }

    /// <summary>can_see_kurt: Kurt within range, inside a cone around the object's yaw, with a clear
    /// line of sight. The cone's formula isn't known; it's used as a half angle in degrees.</summary>
    public bool CanSeeKurt(MdkObject obj, float range, float cone)
    {
        const float MinCone = 10f;
        if (obj.DistanceTo(KurtPosition) > range)
        {
            return false;
        }

        if (cone < HalfTurn && MathF.Abs(WrapAngle(obj.YawTo(KurtPosition) - obj.Yaw)) > MathF.Max(cone, MinCone))
        {
            return false;
        }

        return Raycast(obj.Position + new Vector3(0f, 0f, EyeHeight), KurtPosition + new Vector3(0f, 0f, KurtChest)) == null;
    }

    /// <summary>play_sound (0x442402): flags & 3: 0 play, 1 restart, 2 play unless it's playing, 3
    /// stop (3D) or nothing (2D). 0x80: without position; 0x10: following the object at an offset;
    /// 0x20: at a reference point; 0x40: at a point; none: where the object is (following it with
    /// flag 4, which also makes it the object's tracked sound).</summary>
    /// <summary>play_sound's start modes 0-2.</summary>
    private static readonly SoundMixer.Start[] SoundStarts = [SoundMixer.Start.New, SoundMixer.Start.Restart, SoundMixer.Start.Once];

    public void PlaySound(MdkObject obj, string name, int flags, object? position)
    {
        const int ModeMask = 3;
        const int StopMode = 3;
        const int Tracked = 4;
        const int Following = 0x10;
        const int AtReference = 0x20;
        const int AtPoint = 0x40;
        const int Flat = 0x80;
        var starts = SoundStarts;
        var mode = flags & ModeMask;
        if ((flags & Flat) != 0)
        {
            if (mode != StopMode)
            {
                Mixer.Play(name, starts[mode]);
            }

            return;
        }

        if (mode == StopMode)
        {
            Mixer.Stop(name);
            return;
        }

        var point = position is object?[] list ? V(list) : Vector3.Zero;
        int voice;
        if ((flags & Following) != 0)
        {
            voice = PlayOn(name, obj, starts[mode], point);
        }
        else if ((flags & AtReference) != 0)
        {
            voice = Mixer.PlayAt(name, obj.ReferencePoint(I(position)), starts[mode]);
        }
        else if ((flags & AtPoint) != 0)
        {
            voice = Mixer.PlayAt(name, point, starts[mode]);
        }
        else if ((flags & Tracked) != 0)
        {
            voice = PlayOn(name, obj, starts[mode], Vector3.Zero);
        }
        else
        {
            voice = Mixer.PlayAt(name, obj.Position, starts[mode]);
        }

        if ((flags & Tracked) != 0)
        {
            obj.TrackedSound = name;
            obj.TrackedVoice = voice;
        }
    }

    /// <summary>A sound following an object, at an offset in its frame (kept to stop it with the object).</summary>
    private int PlayOn(string name, MdkObject obj, SoundMixer.Start start, Vector3 offset)
    {
        var voice = Mixer.PlayOn(name, obj, offset, start);
        if (!_following.TryGetValue(obj, out var voices))
        {
            _following[obj] = voices = _spareVoiceLists.Count > 0 ? _spareVoiceLists.Pop() : [];
        }

        // Ended voices are forgotten.
        for (var i = voices.Count - 1; i >= 0; i--)
        {
            if (!Mixer.IsVoicePlaying(voices[i]))
            {
                voices.RemoveAt(i);
            }
        }

        voices.Add(voice);
        return voice;
    }
}
