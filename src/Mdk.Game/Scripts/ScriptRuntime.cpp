#include "Mdk.Game/Scripts/ScriptRuntime.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <limits>
#include <unordered_set>

#include "Mdk.Formats/Arena.h"
#include "Mdk.Formats/BinReader.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Formats/ModelAnimation.h"
#include "Mdk.Formats/SpriteAnimation.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Formats/TextureArchive.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Collision/Bsp.h"
#include "Mdk.Game/Hud/Messages.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Kurt/Scope.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Scripts/AirStrike.h"
#include "Mdk.Game/Scripts/AnimatedTextures.h"
#include "Mdk.Game/Scripts/BulletHoles.h"
#include "Mdk.Game/Scripts/Effects.h"
#include "Mdk.Game/Scripts/EndLevel.h"
#include "Mdk.Game/Scripts/Fans.h"
#include "Mdk.Game/Scripts/Items.h"
#include "Mdk.Game/Scripts/ObjectBehaviors.h"
#include "Mdk.Game/Scripts/ObjectMotion.h"
#include "Mdk.Game/Scripts/Rides.h"
#include "Mdk.Game/Scripts/ScriptMath.h"
#include "Mdk.Game/Scripts/ScriptVm.h"
#include "Mdk.Game/Scripts/SniperRounds.h"

namespace Mdk::Game::Scripts
{
    using namespace ScriptMath;
    using Formats::Box;
    using Game::Level::LevelData;
    using Game::Level::TriangleGroups;

    namespace
    {
        bool ContainsName(const std::vector<std::string>& names, const std::string& name)
        {
            return std::find(names.begin(), names.end(), name) != names.end();
        }
    }

    ScriptRuntime::ArenaState::ArenaState(std::string name, int script)
        : Name(std::move(name)), Controller(std::make_shared<MdkObject>()), HitScripts(std::make_shared<MdkObject>())
    {
        Controller->Arena = Name;
        Controller->Restart = script;
        HitScripts->Arena = Name;
    }

    float ScriptRuntime::TownTimeLeft() const
    {
        return _townTotal == 0 ? 1.0f : Math::Clamp(static_cast<float>(TownTicks) / _townTotal, 0.0f, 1.0f);
    }

    const std::vector<std::string>& ScriptRuntime::SolidArenas()
    {
        if (!Rides->OnBoard())
        {
            return _drawnArenas;
        }

        _solidFirst.clear();
        if (!_drawnArenas.empty())
        {
            _solidFirst.push_back(_drawnArenas[0]);
        }

        return _solidFirst;
    }

    ScriptRuntime::ScriptRuntime(std::shared_ptr<Game::Level::LevelData> level, std::shared_ptr<Formats::Cmi> cmi, std::shared_ptr<Formats::Bni> sprites,
        std::shared_ptr<Collision::ArenaSpace> space, std::shared_ptr<Game::Level::TriangleGroups> groups,
        std::shared_ptr<Audio::SoundMixer> mixer, std::shared_ptr<Game::Kurt::Kurt> kurt, std::optional<int> seed)
    {
        Rng = seed.has_value() ? std::make_shared<Random>(*seed) : std::make_shared<Random>();
        Kurt = kurt;
        Rides = std::make_shared<Scripts::Rides>(*this);
        _sprites = sprites;
        Items = std::make_shared<Scripts::Items>(*this, sprites);
        SniperRounds = std::make_shared<Scripts::SniperRounds>(*this);
        AirStrike = std::make_shared<Scripts::AirStrike>(*this);
        kurt->SniperFire = [this](int type) { return FireSniper(type); };
        kurt->ItemUsed = [this]() { Items->UseItem(); };
        kurt->BombTriggered = [this]() { Items->TriggerBomb(); };
        kurt->CanUseItem = [this](Game::Kurt::Inventory::Item item) { return Items->CanUse(item); };
        kurt->SolidsWithin = [this](const Box& region) { return SolidsWithin(region); };
        Effects = std::make_shared<Scripts::Effects>(Rng);
        Effects->FrameCount = [this](const std::string& arena, const std::string& name) { return TextureFrames(arena, name); };
        Effects->Ray = [this](const std::string& arena, Vector3 from, Vector3 to) { return RayIn(arena, from, to); };
        AnimatedTextures = std::make_shared<Scripts::AnimatedTextures>([this](const std::string& arena, const std::string& name) { return TextureFrames(arena, name); });
        Fans = std::make_shared<Scripts::Fans>(level->Dti->Arenas, Rng);
        Fans->Spark = [this](const std::string& arena, Vector3 point) { FanSpark(arena, point); };
        Fans->IsLive = [this](const std::string& arena) { return IsLiveArena(arena); };
        Debris = std::make_shared<Scripts::Debris>(Rng);
        Debris->Ray = [this](const std::string& arena, Vector3 from, Vector3 to) { return RayIn(arena, from, to); };
        Debris->Trail = [this](const std::string& arena, Vector3 point) { Effects->SpawnTrail(arena, point); };
        Debris->Updraft = [this](const std::string& arena, Vector3 point, float vz, float dt) { return Fans->Query(arena, point, vz, Scripts::Fans::MaskEffects, dt); };
        Debris->GroupFacets = [this](const std::string& arena, int number) { return GroupFacets(arena, number); };
        Game::Kurt::Kurt* kurtPtr = kurt.get();
        kurt->Updraft = [this, kurtPtr](float vz, float dt) { return KurtUpdraft(kurtPtr->Feet, vz, dt); };
        kurt->Keeper = [](MdkObject* owner) { return KeeperAt(owner); };
        Level = level;
        Cmi = cmi;
        Mixer = mixer;
        _space = space;
        _groups = groups;
        auto decoder = Formats::Scripts::ScriptDecoder::For(*cmi);
        decoder->Preload(cmi->EntryPoints());
        _decoder = decoder;
        Vm = std::make_shared<Scripts::ScriptVm>(*this, decoder);
        Motion = std::make_shared<Scripts::ObjectMotion>(*this);
        Behaviors = std::make_shared<Scripts::ObjectBehaviors>(*this);
        for (const auto& arena : level->Arenas)
        {
            _bsps[arena->Name] = std::make_shared<Collision::Bsp>(arena);
            auto floor = NoArenaFloor;
            if (!arena->Vertices.empty())
            {
                floor = arena->Vertices[0].Z;
                for (const auto& v : arena->Vertices)
                {
                    floor = MathF::Min(floor, v.Z);
                }
            }

            _floors[arena->Name] = floor;
        }

        FindConnections();

        // No town to save in the last level, nor in the 1996 demo's.
        if (GameStats::IndexOf(level->Number) != LastLevelIndex && !IsBeta())
        {
            TownTicks = _townTotal = TownTicksByDifficulty[static_cast<int>(Kurt->Inventory->Difficulty)];
        }
    }

    void ScriptRuntime::Update(float delta)
    {
        _time += delta;
        while (_time >= Tick)
        {
            _time -= Tick;
            for (const auto& obj : Objects)
            {
                obj->Remember();
            }

            RunTick();
            _tickCount++;
        }

        Rides->Follow();
    }

    float ScriptRuntime::TickBlend(float ahead)
    {
        return Math::Clamp((_time + ahead) / Tick, 0.0f, 1.0f);
    }

    std::shared_ptr<ScriptRuntime::ArenaState> ScriptRuntime::GetArenaState(const std::string& arena)
    {
        std::shared_ptr<ArenaState> state;
        if (!_arenas.TryGetValue(arena, state))
        {
            state = _arenas[arena] = std::make_shared<ArenaState>(arena, Cmi->ArenaScripts.GetValueOrDefault(arena));
        }

        return state;
    }

    int ScriptRuntime::GetKurtFloorGroup()
    {
        auto hit = Raycast(KurtPosition + Vector3::UnitZ(), KurtPosition - Vector3(0.0f, 0.0f, 10.0f));
        return hit.has_value() ? hit->Group : 0;
    }

    void ScriptRuntime::SelectTarget(MdkObject& obj)
    {
        constexpr int AlwaysKurt = 2;
        TargetPosition = KurtPosition;
        TargetYaw = KurtYaw;
        if (obj.TargetMode == AlwaysKurt)
        {
            return;
        }

        MdkObject* other = nullptr;
        if (Items->Decoy != nullptr && !Items->Decoy->Dead)
        {
            other = Items->Decoy.get();
        }
        else if (AlienTarget != nullptr && !AlienTarget->Dead)
        {
            other = AlienTarget.get();
        }

        if (other != nullptr)
        {
            TargetPosition = other->Position;
            TargetYaw = other->Yaw;
        }
    }

    bool ScriptRuntime::IsSoundPlaying(const std::string& name)
    {
        return Mixer->IsPlaying(Str::ToUpper(name));
    }

    void ScriptRuntime::EnterArena(const std::string& arena)
    {
        if (ArenaEntered)
        {
            ArenaEntered(arena);
        }

        if (auto entered = Level->ArenaNamed(arena))
        {
            _space->Add(entered);
        }
    }

    void ScriptRuntime::TeleportKurt(const std::string& arena, Vector3 position, float yaw)
    {
        if (!arena.empty())
        {
            EnterArena(arena);

            auto from = CurrentArena;
            CurrentArena = arena;
            TeleportSecond(arena, from);
            ShowArena(arena);

            // No move crosses a connection (0x41bce4 sets 0x5739cc too).
            _previousKurtPosition = position;
            _teleported = true;
        }

        Kurt->Teleport(position, yaw);
        if (arena.empty())
        {
            Kurt->WhiteFlash = MathF::Max(Kurt->WhiteFlash, TeleportFlash);
        }
    }

    void ScriptRuntime::ShutDoors()
    {
        for (const auto& door : Objects)
        {
            if (door->Dead || (door->Flags & MdkObject::FlagDoor) == 0)
            {
                continue;
            }

            Behaviors->Shut(*door);
        }
    }

    void ScriptRuntime::TeleportSecond(const std::string& arena, const std::string& from)
    {
        if (Level->Mto->Has(arena))
        {
            SecondArena = "";
            SecondActive = false;
            return;
        }

        if (IsCorridorLoaded(arena, from))
        {
            return;
        }

        const Formats::Dti::ArenaEntry* neighbour = nullptr;
        for (const auto& a : Level->Dti->Arenas)
        {
            if (!Level->Mto->Has(a.Name))
            {
                continue;
            }

            for (const auto& r : a.Records)
            {
                if (r.Type == LevelData::Connection && _connections.GetValueOrDefault({a.Name, r.Id}) == arena)
                {
                    neighbour = &a;
                    break;
                }
            }
        }

        SecondArena = neighbour != nullptr ? neighbour->Name : "";
        SecondActive = false;
        if (neighbour != nullptr)
        {
            LoadArena(neighbour->Name);
        }
    }

    bool ScriptRuntime::IsCorridorLoaded(const std::string& corridor, const std::string& from)
    {
        if (corridor == SecondArena || corridor == from)
        {
            return true;
        }

        for (const auto& r : ArenaRecords(_residentArena))
        {
            if (r.Type == LevelData::Connection && _connections.GetValueOrDefault({_residentArena, r.Id}) == corridor)
            {
                return true;
            }
        }

        return false;
    }

    bool ScriptRuntime::Same(const std::vector<std::string>& a, const std::vector<std::string>& b)
    {
        if (a.size() != b.size())
        {
            return false;
        }

        for (size_t i = 0; i < a.size(); i++)
        {
            if (a[i] != b[i])
            {
                return false;
            }
        }

        return true;
    }

    void ScriptRuntime::KurtTouchesGroups()
    {
        auto& touched = _touchedGroups;
        touched.clear();
        auto contacts = Kurt->TakeContacts();
        for (const auto& [arena, triangle] : contacts)
        {
            auto group = TriangleGroup(*arena, triangle);
            if (arena->Name != CurrentArena || group == 0 || !touched.insert(group).second)
            {
                continue;
            }

            HitGroup(CurrentArena, group, 0, HitKurt, HitTypeKurt);
        }
    }

    void ScriptRuntime::ShowArena(const std::string& arena)
    {
        if (arena.empty() || arena == NoArena)
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
        auto state = GetArenaState(arena);
        if (!state->Started)
        {
            state->Started = true;
            SpawnDtiAliens(arena);
        }
    }

    void ScriptRuntime::PreloadArena(const std::string& arena)
    {
        if (arena.empty() || arena == NoArena || arena == SecondArena)
        {
            return;
        }

        SecondArena = arena;
        LoadArena(arena);
        SecondActive = false;
    }

    void ScriptRuntime::LoadArena(const std::string& arena)
    {
        if (Level->Mto->Has(arena))
        {
            _residentArena = arena;
        }

        PullDoors(arena);
    }

    void ScriptRuntime::PullDoors(const std::string& arena)
    {
        for (const auto& record : ArenaRecords(arena))
        {
            std::string other;
            if (record.Type != LevelData::Connection || !_connections.TryGetValue({arena, record.Id}, other)
                || other == CurrentArena || (SecondActive && other == SecondArena))
            {
                continue;
            }

            for (const auto& door : Objects)
            {
                if (door->Dead || (door->Flags & MdkObject::FlagDoor) == 0 || door->Arena != other || door->Connects != arena)
                {
                    continue;
                }

                ObjectBehaviors::MoveDoor(*door);
            }
        }
    }

    void ScriptRuntime::UpdateArenas()
    {
        auto& loaded = _loadedNext;
        loaded.clear();
        loaded.push_back(CurrentArena);
        if (!SecondArena.empty())
        {
            loaded.push_back(SecondArena);
        }

        auto& drawn = _drawnNext;
        drawn.clear();
        drawn.push_back(CurrentArena);
        if (SecondActive && !SecondArena.empty())
        {
            drawn.push_back(SecondArena);
        }

        if (!Same(loaded, _loadedArenas))
        {
            auto copy = Objects;
            for (const auto& obj : copy)
            {
                if (ContainsName(_loadedArenas, obj->Arena) && !ContainsName(loaded, obj->Arena))
                {
                    // Kurt's thrown items and effects there go (0x43f800).
                    if ((obj->Flags & (Scripts::Items::FlagThrown | Scripts::Items::FlagActive)) != 0)
                    {
                        Items->Forget(*obj);
                        Remove(*obj);
                        continue;
                    }

                    Mixer->StopVoice(obj->LoopSound);
                    obj->LoopSound = 0;
                }
                else if (ContainsName(loaded, obj->Arena) && !ContainsName(_loadedArenas, obj->Arena) && !obj->LoopSoundName.empty())
                {
                    obj->LoopSound = PlayOn(obj->LoopSoundName, *obj, Audio::SoundMixer::Start::New, Vector3::Zero());
                }
            }

            _loadedArenas = loaded;
        }

        _drawnArenas = drawn;
        for (const auto& obj : Objects)
        {
            obj->Visible = ContainsName(drawn, obj->Arena) && !Rides->Hides(*obj);
        }
    }

    std::string ScriptRuntime::CameraArena(Vector3 eye)
    {
        if (_drawnArenas.size() < 2)
        {
            return CurrentArena;
        }

        auto from = KurtPosition + Vector3::UnitZ() * CameraLineHeight;
        return CrossedArena(CurrentArena, from, eye) == SecondArena ? SecondArena : CurrentArena;
    }

    bool ScriptRuntime::IsLiveArena(const std::string& arena)
    {
        return arena == CurrentArena || (SecondActive && arena == SecondArena);
    }

    void ScriptRuntime::CheckTriggers()
    {
        auto from = Vector2(_previousKurtPosition.X, _previousKurtPosition.Y);
        auto to = Vector2(KurtPosition.X, KurtPosition.Y);
        for (const auto& record : ArenaRecords(CurrentArena))
        {
            if (record.Type != TriggerShow && record.Type != TriggerLoad)
            {
                continue;
            }

            auto min = Vector2(MathF::Min(record.Position.X, record.BoxEnd.X), MathF::Min(record.Position.Y, record.BoxEnd.Y));
            auto max = Vector2(MathF::Max(record.Position.X, record.BoxEnd.X), MathF::Max(record.Position.Y, record.BoxEnd.Y));
            auto inside = to.X >= min.X && to.Y >= min.Y && to.X < max.X && to.Y < max.Y;
            if (!inside && !Crosses(min, max, from, to))
            {
                continue;
            }

            auto arena = record.Id >= 0 && record.Id < static_cast<int>(Level->Dti->Arenas.size()) ? Level->Dti->Arenas[record.Id].Name : std::string();
            if (record.Type == TriggerShow)
            {
                ShowArena(arena);
            }
            else if (arena.empty())
            {
                ShowArena("");
            }
            else
            {
                PreloadArena(arena);
            }
        }
    }

    bool ScriptRuntime::Crosses(Vector2 min, Vector2 max, Vector2 from, Vector2 to)
    {
        std::array<Vector2, 4> corners = {min, Vector2(max.X, min.Y), max, Vector2(min.X, max.Y)};
        for (size_t i = 0; i < corners.size(); i++)
        {
            if (SegmentsCross(from, to, corners[i], corners[(i + 1) % corners.size()]))
            {
                return true;
            }
        }

        return false;
    }

    bool ScriptRuntime::SegmentsCross(Vector2 a, Vector2 b, Vector2 c, Vector2 d)
    {
        auto r = b - a;
        auto s = d - c;
        auto denominator = r.X * s.Y - r.Y * s.X;
        if (denominator == 0.0f)
        {
            return false;
        }

        auto t = ((c.X - a.X) * s.Y - (c.Y - a.Y) * s.X) / denominator;
        auto u = ((c.X - a.X) * r.Y - (c.Y - a.Y) * r.X) / denominator;
        return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
    }

    void ScriptRuntime::FollowArenas(MdkObject& obj)
    {
        auto arena = CrossedArena(obj.Arena, obj.PreviousPosition, obj.Position);
        if (!arena.empty())
        {
            obj.Arena = arena;
        }
    }

    std::string ScriptRuntime::CrossedArena(const std::string& arena, Vector3 from, Vector3 to)
    {
        for (const auto& record : ArenaRecords(arena))
        {
            std::string other;
            if (record.Type == LevelData::Connection && CrossesDoorway(record, from, to)
                && _connections.TryGetValue({arena, record.Id}, other))
            {
                return other;
            }
        }

        return "";
    }

    bool ScriptRuntime::CrossesDoorway(const Formats::Dti::Record& record, Vector3 from, Vector3 to)
    {
        auto min = record.Position;
        auto max = record.BoxEnd;
        auto bottom = min.Z - DoorwayDrop;
        auto plane = min.Z - HatchDrop;
        auto withinX = Spans(from.X, to.X, min.X, max.X);
        auto withinY = Spans(from.Y, to.Y, min.Y, max.Y);
        auto withinZ = Spans(from.Z, to.Z, bottom, max.Z);
        auto side = (to.X - min.X) * (max.Y - min.Y) - (max.X - min.X) * (to.Y - min.Y);
        switch (static_cast<Doorway>(std::bit_cast<int>(record.Angle)))
        {
            case Doorway::MinusX:
                return withinY && withinZ && to.X < min.X && from.X >= min.X;
            case Doorway::PlusX:
                return withinY && withinZ && to.X > min.X && from.X <= min.X;
            case Doorway::MinusY:
                return withinX && withinZ && to.Y < min.Y && from.Y >= min.Y;
            case Doorway::PlusY:
                return withinX && withinZ && to.Y > min.Y && from.Y <= min.Y;
            case Doorway::MinusZ:
                return withinX && withinY && to.Z < plane && from.Z >= plane;
            case Doorway::PlusZ:
                return withinX && withinY && to.Z > plane && from.Z <= plane;
            case Doorway::Right:
                return withinX && withinY && withinZ && side > 0.0f;
            default:
                return withinX && withinY && withinZ && side < 0.0f;
        }
    }

    bool ScriptRuntime::Spans(float a, float b, float min, float max)
    {
        return MathF::Max(a, b) >= min && MathF::Min(a, b) <= max;
    }

    std::vector<Formats::Dti::Record> ScriptRuntime::ArenaRecords(const std::string& arena)
    {
        std::vector<Formats::Dti::Record> records;
        if (!_records.TryGetValue(arena, records))
        {
            const auto* entry = Level->EntryNamed(arena);
            records = _records[arena] = entry != nullptr ? entry->Records : std::vector<Formats::Dti::Record>();
        }

        return records;
    }

    void ScriptRuntime::FindConnections()
    {
        Dictionary<int, std::vector<std::string>> byId;
        for (const auto& entry : Level->Dti->Arenas)
        {
            for (const auto& record : entry.Records)
            {
                if (record.Type != LevelData::Connection)
                {
                    continue;
                }

                byId[record.Id].push_back(entry.Name);
            }
        }

        for (const auto& [id, names] : byId)
        {
            for (const auto& a : names)
            {
                for (const auto& b : names)
                {
                    if (a == b)
                    {
                        continue;
                    }

                    _connections[{a, id}] = b;
                }
            }
        }
    }

    void ScriptRuntime::RunTick()
    {
        KurtPosition = Kurt->Feet;
        TargetPosition = KurtPosition;
        TargetYaw = Wrap360(Kurt->Yaw);
        KurtYaw = TargetYaw;
        if (Cutscene != 0)
        {
            CutsceneTick();
            return;
        }

        AlarmTicks = Math::Max(AlarmTicks - 1, 0);
        AlarmEndedTicks = Math::Max(AlarmEndedTicks - 1, 0);
        CameraTrackTicks = Math::Max(CameraTrackTicks - 1, 0);
        Shake = 0.0f;
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
        auto arena = IsBeta() ? BetaArena()
            : CurrentArena.empty() ? _space->ArenaAt(KurtPosition).value_or("") : CrossedArena(CurrentArena, _previousKurtPosition, KurtPosition);
        if (!arena.empty() && arena != CurrentArena)
        {
            // Crossing into another arena: the one left stays as the active second arena.
            if (!CurrentArena.empty())
            {
                SecondArena = CurrentArena;
                Kurt->EnterArena();
            }

            SecondActive = true;
            CurrentArena = arena;
            ShowArena(arena);
        }

        CheckTriggers();
        UpdateArenas();
        KurtTouchesGroups();
        Rides->Update();

        // Kurt moves and takes pickups, then fires, then the objects run (game_frame).
        if (_tickCount > 0)
        {
            CollectPickups();
        }

        KurtVelocity = (KurtPosition - _previousKurtPosition) * TicksPerSecond;
        _previousKurtPosition = KurtPosition;
        if (Kurt->Firing)
        {
            FireChainGun();
        }

        Items->UpdateTwisters();
        Effects->Update(1.0f);
        Fans->Update();
        Debris->Update(1.0f);
        SniperRounds->Update(1.0f);
        AirStrike->Update(1.0f);
        if (EndLevel != nullptr)
        {
            EndLevel->Update(1.0f);
        }

        UpdateSniperTarget();

        // Only the objects of Kurt's arena, then those of the active second arena (as it is by then)
        // are updated (game_frame: 0x43c7dc on each), each arena's newest first.
        auto objects = Objects;
        _ranObjects.clear();
        RunObjects(objects, CurrentArena);
        if (SecondActive && !SecondArena.empty() && SecondArena != CurrentArena)
        {
            RunObjects(objects, SecondArena);
        }

        // Then the arenas' own scripts (game_frame: 0x43c7dc, then 0x440bc8). E.g. MEAT_5's
        // arena_show NONE drops CMEAT_4 only after the ridden board there followed Kurt in.
        RunControllers();
    }

    void ScriptRuntime::RunObjects(const std::vector<std::shared_ptr<MdkObject>>& objects, const std::string& arena)
    {
        for (const auto& obj : objects)
        {
            if (obj->Dead || obj->Arena != arena || !_ranObjects.insert(obj.get()).second)
            {
                continue;
            }

            if ((obj->Flags & MdkObject::FlagDoor) != 0)
            {
                Behaviors->UpdateDoor(*obj);
            }

            auto position = obj->Position;
            auto yaw = obj->Yaw;
            Vm->Run(*obj);
            if (!obj->Dead)
            {
                Motion->Update(*obj);
            }

            // Kurt moves and turns with the platform he stands on; one that keeps him (0x800000)
            // takes his XY in the air too (0x46634e).
            if (obj.get() == Kurt->Platform && !obj->Dead && Kurt->OnFloor)
            {
                Kurt->Carry(position, obj->Position, obj->Yaw - yaw);
            }
            else if (obj.get() == Kurt->Platform && !obj->Dead && (obj->Flags & MdkObject::FlagStandable) != 0)
            {
                Kurt->Carry(Vector3(position.X, position.Y, obj->Position.Z), obj->Position, 0.0f);
            }
        }
    }

    void ScriptRuntime::RunControllers()
    {
        if (!CurrentArena.empty())
        {
            Vm->Run(*GetArenaState(CurrentArena)->Controller);
        }

        if (SecondActive && !SecondArena.empty())
        {
            Vm->Run(*GetArenaState(SecondArena)->Controller);
        }
    }

    void ScriptRuntime::SpecialEvent(MdkObject& obj, int value)
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
                PlayStrikeScene(Scripts::StrikeScene::Kind::Kurt, Scripts::StrikeScene::Plane::WithPilot);
                Kurt->Teleport(obj.Position, obj.Yaw);
                obj.Position.Y += 4.0f;
                StartCutscene(CutsceneStrike, obj);
                CameraDistance = 10.0f;
                CameraPosition = obj.Position - Vector3(10.0f, 4.0f, -8.0f);
                CameraPitch = 0.0f;
                CameraYaw = 90.0f - obj.Yaw;
                break;
            case 52:
            {
                auto dog = FindObjectNamed("XBN");
                if (dog != nullptr)
                {
                    StartCutscene(CutsceneDog, *dog);
                    CameraMode = 0;
                }

                break;
            }
            case 53:
            case 92:
                EndCutscene();
                break;
            case 55:
                CameraMode = 2;
                break;
            case 61:
            {
                auto gunter = FindObjectNamed("XGUNTAM");
                if (gunter != nullptr)
                {
                    StartCutscene(CutsceneAll, *gunter);
                    CameraMode = 11;
                    auto heading = FromAngle(gunter->Yaw) * 45.0f;
                    CameraPosition = gunter->Position + Vector3(heading.X, heading.Y, 3.0f);
                    CameraPitch = -20.0f;
                    CameraYaw = 270.0f - gunter->Yaw;
                    CameraDistance = 45.0f;
                }

                break;
            }
            case 81:
                // The end of the game: MISC/FLIC/MDKEND.FLC and MDKBZK.MVE (game state 8).
                StartCutscene(CutsceneEnd, obj);
                if (GameFinished)
                {
                    GameFinished();
                }

                break;
            case 91:
                StartCutscene(CutsceneAllFlagged, obj);
                CameraMode = 12;
                CameraPosition = Vector3(-121.0f, 3347.0f, -350.0f);
                break;
            case 93:
                StartCutscene(CutsceneBoss, obj);
                CameraMode = 12;
                CameraPosition = Vector3(1158.0f, 5006.0f, 315.0f);
                break;
        }
    }

    bool ScriptRuntime::GunAim(MdkObject& obj, float y, float z)
    {
        constexpr float Lead = 1.67333f;
        constexpr float Down = 270.0f;
        constexpr float Cone = 3.0f;
        constexpr int MaxTargets = 4;
        auto lead = Vector3(KurtPosition.X + KurtVelocity.X * Lead, KurtPosition.Y, 0.0f);
        auto angle = obj.YawTo(lead) + Rng->Next(500) * 0.001f - 0.25f;
        auto aimed = MathF::Abs(angle - Down) <= Cone;
        if (aimed && Rng->Next(70) < Round(MathF::Abs(KurtVelocity.X)) + 10)
        {
            obj.Yaw = angle;
            return true;
        }

        std::vector<float> angles;
        for (const auto& other : Objects)
        {
            if (other->Dead || other->Arena != obj.Arena || MathF::Abs(other->Position.Y - y) > 2.0f || MathF::Abs(other->Position.Z - z) > 3.0f)
            {
                continue;
            }

            auto otherAngle = obj.YawTo(other->Position);
            if (MathF::Abs(otherAngle - Down) > Cone)
            {
                continue;
            }

            angles.push_back(otherAngle);
            if (static_cast<int>(angles.size()) == MaxTargets)
            {
                break;
            }
        }

        if (!angles.empty())
        {
            obj.Yaw = angles[Rng->Next(static_cast<int>(angles.size()))];
            return true;
        }

        if (aimed)
        {
            obj.Yaw = angle;
            return true;
        }

        obj.Yaw = Down;
        return Rng->Next(100) > 70;
    }

    void ScriptRuntime::PlaceNearKurt(MdkObject& obj, float xMin, float xMax, float yLimit)
    {
        constexpr float Lead = 2.67333f;
        constexpr int Tries = 100;
        constexpr float Spacing = 12.0f;
        auto r = Rng->Next(100);
        float x;
        if (r < 25 || KurtPosition.X < xMin || KurtPosition.X > xMax || KurtPosition.Y > yLimit)
        {
            x = xMin + (xMax - xMin) * Rng->Next(10000) * 0.0001f;
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

        for (auto i = 0; i < Tries; i++)
        {
            auto moved = false;
            for (const auto& other : Objects)
            {
                if (other.get() == &obj || other->Dead || other->Arena != obj.Arena || other->Position.Y != obj.Position.Y
                    || MathF::Abs(x - other->Position.X) >= Spacing - 0.5f)
                {
                    continue;
                }

                x = other->Position.X - Spacing;
                moved = true;
            }

            if (!moved)
            {
                break;
            }
        }

        obj.Position.X = x;
    }

    void ScriptRuntime::CameraTrack(MdkObject& obj, int mode, float height)
    {
        constexpr float MaxUp = -30.0f;
        auto off = Wrap360(KurtYaw - Heading(KurtPosition, obj.Position));
        if (off > HalfTurn)
        {
            off = FullTurn - off;
        }

        auto top = obj.Position.Z + height * obj.Scale;
        if (mode == 0)
        {
            auto boundsTop = obj.Position.Z + (obj.Model != nullptr ? obj.Model->Bounds.Max.Z * obj.Scale : 0.0f);
            top = 0.3f * obj.Position.Z + 0.7f * boundsTop;
        }

        auto rest = ArenaCameraPitch();
        auto target = rest;
        if (off <= 90.0f && top >= KurtPosition.Z)
        {
            auto distance = Distance2D(obj.Position, KurtPosition);
            target = Math::Clamp(-ToDegrees(MathF::Atan2(top - KurtPosition.Z, distance)) * (120.0f - off) / 120.0f, MaxUp, rest);
        }

        CameraTrackPitch = target;
        CameraTrackTicks = 2;
    }

    float ScriptRuntime::ArenaCameraPitch()
    {
        const auto* entry = Level->EntryNamed(CurrentArena);
        return entry != nullptr ? entry->Pitch : DefaultCameraPitch;
    }

    void ScriptRuntime::UpdateSniperTarget()
    {
        constexpr int Untargetable = MdkObject::FlagNotSolid | MdkObject::FlagNotTarget;
        constexpr int Corners = 8;
        using Game::Kurt::Scope;
        SniperTarget = nullptr;
        Kurt->Scope->ZoomLimit = Scope::ZoomMin;
        if (!Kurt->Sniping)
        {
            return;
        }

        auto eye = Kurt->SniperEye();
        auto forward = Kurt->SniperForward();
        auto zoom = Kurt->Scope->Zoom;
        auto bestDepth = std::numeric_limits<float>::max();
        for (const auto& obj : Objects)
        {
            if (obj->Dead || obj->Arena != CurrentArena || obj->Model == nullptr || (obj->Flags & Untargetable) != 0)
            {
                continue;
            }

            auto bounds = GetWorldBounds(*obj);
            auto centre = Scope::ToScreen(eye, forward, zoom, Center(bounds));
            if (!centre.has_value())
            {
                continue;
            }

            auto min = *centre;
            auto max = *centre;
            for (auto i = 0; i < Corners; i++)
            {
                auto corner = Vector3((i & 1) != 0 ? bounds.Max.X : bounds.Min.X, (i & 2) != 0 ? bounds.Max.Y : bounds.Min.Y, (i & 4) != 0 ? bounds.Max.Z : bounds.Min.Z);
                if (auto p = Scope::ToScreen(eye, forward, zoom, corner))
                {
                    min = Vector2::Min(min, *p);
                    max = Vector2::Max(max, *p);
                }
            }

            auto depth = Vector3::Distance(eye, Center(bounds));
            if (Scope::Locks(min, max) && depth < bestDepth)
            {
                bestDepth = depth;
                SniperTarget = obj;
            }
        }

        if (SniperTarget != nullptr)
        {
            auto bounds = GetWorldBounds(*SniperTarget);
            Kurt->Scope->ZoomLimit = Scope::LockLimit(Size(bounds).Z, Vector3::Distance(KurtPosition, Center(bounds)));
        }
    }

    bool ScriptRuntime::FireSniper(int type)
    {
        auto fired = type == Game::Kurt::Scope::Strike
            ? AirStrike->Call(Kurt->SniperEye(), Kurt->SniperForward())
            : SniperRounds->Fire(type, Kurt->SniperEye(), Wrap360(Kurt->Yaw), Kurt->Scope->Pitch, SniperTarget);
        if (fired)
        {
            Stats->SniperShots++;
        }

        return fired;
    }

    void ScriptRuntime::StampBulletHole(MdkObject& obj)
    {
        auto model = obj.Model;
        if (model == nullptr || obj.ShotPart <= 0 || obj.ShotPart > static_cast<int>(model->PartList.size()))
        {
            return;
        }

        const auto& part = *model->PartList[obj.ShotPart - 1];
        auto pose = obj.Pose();
        const auto& vertices = pose[obj.ShotPart - 1];
        if (vertices.empty())
        {
            return;
        }

        // The hit in the model's frame.
        auto local = Vector3::Transform(obj.ShotPoint - obj.Position, Matrix4x4::CreateRotationZ(-ToRadians(obj.Yaw))) / obj.Scale;
        auto textureOf = [&](int value) -> std::shared_ptr<Formats::Texture>
        {
            return value >= 0 && value < static_cast<int>(model->Materials.size()) ? ArenaTexture(obj.Arena, model->Materials[value]) : nullptr;
        };
        auto face = BulletHoles::Nearest(part, vertices, local, [&](int v) { return textureOf(v) != nullptr; });
        if (!face.has_value())
        {
            return;
        }

        auto texture = textureOf(part.TriangleMaterials[face->Triangle]);
        auto w = face->Weights;
        auto uv = part.TriangleUvs[face->Triangle * 3] * w.X + part.TriangleUvs[face->Triangle * 3 + 1] * w.Y + part.TriangleUvs[face->Triangle * 3 + 2] * w.Z;
        BulletHoles::Stamp(*texture, uv, *_sprites->GetImage(std::string(Option != 0 ? BulletHoles::Hole : BulletHoles::GorelessHole)));
        if (TextureStamped)
        {
            TextureStamped(*texture);
        }
    }

    std::shared_ptr<Formats::Texture> ScriptRuntime::ArenaTexture(const std::string& arena, const std::string& name)
    {
        auto found = Level->ArenaNamed(arena);
        auto archives = found != nullptr ? Level->ArchivesOf(*found) : std::vector<std::shared_ptr<Formats::TextureArchive>>{Level->LevelTextures};
        for (const auto& a : archives)
        {
            if (auto t = a->Textures.GetValueOrDefault(name))
            {
                return t;
            }
        }

        return nullptr;
    }

    std::shared_ptr<Game::Objects::MdkObject> ScriptRuntime::FindObjectNamed(const std::string& type)
    {
        for (const auto& o : Objects)
        {
            if (!o->Dead && o->TypeName == type)
            {
                return o;
            }
        }

        return nullptr;
    }

    void ScriptRuntime::StartCutscene(int state, MdkObject& target)
    {
        Cutscene = state;
        CutsceneTarget = target.shared_from_this();
        Kurt->StopFiring();
        Kurt->Frozen = true;
    }

    void ScriptRuntime::EndCutscene()
    {
        Cutscene = 0;
        CutsceneTarget = nullptr;
        Kurt->Frozen = false;
        Kurt->Visible = true;
        for (const auto& obj : Objects)
        {
            obj->Visible = true;
        }
    }

    void ScriptRuntime::CutsceneTick()
    {
        Kurt->Visible = Cutscene == CutsceneStrike || Cutscene == CutsceneEnd;
        auto copy = Objects;
        for (const auto& obj : copy)
        {
            if (obj->Dead || obj->Arena != CurrentArena)
            {
                continue;
            }

            auto flagged = (obj->Flags & CutsceneHiddenFlags) != 0 || obj->ThrownKind > 0;
            bool runs;
            bool shown;
            if (Cutscene == CutsceneEnd)
            {
                runs = false;
                shown = false;
            }
            else if (Cutscene < CutsceneAll)
            {
                runs = std::find(CutsceneTypes.begin(), CutsceneTypes.end(), obj->TypeName) != CutsceneTypes.end();
                shown = runs && obj->TypeName != "BOLT" && obj->TypeName != "BIGBOLT";
            }
            else
            {
                runs = Cutscene == CutsceneAllFlagged || !flagged;
                shown = !flagged;
            }

            obj->Visible = shown;
            if (!runs)
            {
                continue;
            }

            Vm->Run(*obj);
            if (!obj->Dead)
            {
                Motion->Update(*obj);
            }
        }

        if (Cutscene != 0 && CutsceneTarget != nullptr && !CutsceneTarget->Dead)
        {
            UpdateCutsceneCamera();
        }
    }

    void ScriptRuntime::UpdateCutsceneCamera()
    {
        auto target = CutsceneTarget->Position;
        auto point = CameraPosition;
        auto yaw = CameraYaw;
        auto distance = CameraDistance;
        switch (CameraMode)
        {
            case 0:
                point = Vector3(423.0f, 85.0f, MathF::Max(target.Z - 25.0f, -2260.0f));
                yaw = 90.0f - Heading(point, target);
                distance = Vector3::Distance(point, target);
                if (distance >= 12.0f)
                {
                    CameraMode = 1;
                }

                CameraTimer = CameraBlendTicks;
                break;
            case 1:
            case 2:
            {
                distance = (CameraMode == 1 ? 12.0f : 25.0f) * 0.1f + CameraDistance * 0.9f;
                auto around = FromAngle(CutsceneTarget->Yaw + 150.0f) * distance;
                point = Vector3(target.X + around.X, target.Y + around.Y, -2258.0f);
                if (CameraMode == 2)
                {
                    point.Z = MathF::Min(CameraPosition.Z + ObjectMotion::Ticks, -2246.0f);
                }

                if (CameraTimer > 0)
                {
                    point = point * 0.2f + CameraPosition * 0.8f;
                }

                yaw = 120.0f - CutsceneTarget->Yaw;
                break;
            }
            case 12:
                yaw = 90.0f - Heading(point, target);
                distance = Vector3::Distance(point, target);
                CameraTimer = CameraBlendTicks;
                break;
        }

        auto pitch = CameraPitch;
        if (CameraMode != 11)
        {
            pitch = -ToDegrees(MathF::Atan2(target.Z + 3.0f - point.Z, distance));
            if (pitch < -HalfTurn)
            {
                pitch += FullTurn;
            }
        }

        if (CameraMode == 1 || CameraMode == 2)
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

    void ScriptRuntime::EndTheLevel()
    {
        if (LevelOver)
        {
            return;
        }

        LevelOver = true;
        Stats->TownFlags = GlobalFlags;
        Kurt->StopFiring();
        Mixer->Play("NUKE");
        Mixer->Play("TORNADO");
        EndLevel = std::make_shared<Scripts::EndLevel>(_sprites->GetAnimation(std::string(Scripts::EndLevel::Takeoff))->FrameCount());
        EndLevel->Finished = [this]()
        {
            if (LevelEnded)
            {
                LevelEnded(GameOver::No);
            }
        };
        EndLevel->Start(*this);
    }

    void ScriptRuntime::SpawnDtiAliens(const std::string& arena)
    {
        auto controller = GetArenaState(arena)->Controller;
        for (const auto& record : ArenaRecords(arena))
        {
            if (record.Type == DtiAlien)
            {
                auto key = std::format("{}${}_{}", arena, record.Name, record.Id);
                Spawn(*controller, record.Name, record.Position, record.Angle, record.Id, Cmi->AlienScripts.GetValueOrDefault(key), Spawning::Plain);
                continue;
            }

            if (record.Type != DtiStatic)
            {
                continue;
            }

            auto script = Cmi->AlienScripts.GetValueOrDefault(std::format("{}${}", arena, record.Name));
            auto obj = Spawn(*controller, record.Name, record.Position, record.Angle, -1, script, Spawning::Plain);
            if (obj != nullptr)
            {
                obj->Flags |= DtiStaticFlags;
                obj->Health = 1;
            }
        }
    }

    const Dictionary<std::string, Vector3> ScriptRuntime::SpritePickups = {
        {"SW_EWJ", Vector3(6.0f, 6.0f, 6.0f)},
        {"BONEFLC", Vector3(9.0f, 9.0f, 6.0f)},
    };

    void ScriptRuntime::SpawnFlagged(MdkObject& parent, const std::string& type, Vector3 position, int script)
    {
        Vector3 size;
        if (!SpritePickups.TryGetValue(type, size))
        {
            Spawn(parent, type, position, 0.0f, -1, script, Spawning::Flagged);
            return;
        }

        SpawnBox(parent, position, size, type, script)->Flags = SpawnFlaggedFlags;
    }

    std::shared_ptr<Game::Objects::MdkObject> ScriptRuntime::Spawn(MdkObject& parent, const std::string& type, Vector3 position, float yaw, int instance, int script, Spawning spawning)
    {
        auto model = FindModel(parent.Arena, type);
        if (model == nullptr)
        {
            return nullptr;
        }

        auto obj = std::make_shared<MdkObject>();
        obj->Arena = parent.Arena;
        obj->TypeName = type;
        obj->Model = model;
        obj->InstanceId = instance >= 0 ? instance : _nextInstance;
        obj->Position = position;
        obj->SpawnPosition = position;
        obj->PreviousPosition = position;
        obj->Yaw = Wrap360(yaw);
        if (instance < 0)
        {
            _nextInstance++;
        }

        if (spawning == Spawning::Flagged)
        {
            obj->Flags = SpawnFlaggedFlags;
        }

        // Newest first, like the arena lists (0x45fd4c): next tick it runs before older objects,
        // e.g. LEVEL7 DANT_6's XG count themselves before the cannon checks that count.
        Objects.insert(Objects.begin(), obj);
        Stats->CountEnemy(type, GameStats::Kill::Created);

        // The object type's init script runs once at creation.
        auto initScript = Cmi->ObjectScripts.GetValueOrDefault(std::format("{}${}", parent.Arena, type));
        if (initScript != 0)
        {
            obj->Restart = initScript;
            Vm->Run(*obj);
        }

        obj->Restart = script;
        return obj;
    }

    std::shared_ptr<Game::Objects::MdkObject> ScriptRuntime::SpawnBox(MdkObject& parent, Vector3 position, Vector3 size, const std::string& texture, int script)
    {
        // One model per size and texture: the models' poses and boxes are kept by model.
        std::shared_ptr<Formats::Model> model;
        if (!_boxModels.TryGetValue({size, texture}, model))
        {
            model = _boxModels[{size, texture}] = BoxModel(size, texture);
        }

        auto obj = std::make_shared<MdkObject>();
        obj->Arena = parent.Arena;
        obj->TypeName = texture;
        obj->Model = model;
        obj->InstanceId = _nextInstance++;
        obj->Position = position;
        obj->SpawnPosition = position;
        obj->PreviousPosition = position;
        obj->Restart = script;
        Objects.insert(Objects.begin(), obj);
        std::erase_if(_boxes, [](const std::shared_ptr<MdkObject>& b) { return b->Dead; });
        _boxes.push_back(obj);
        return obj;
    }

    std::shared_ptr<Formats::Model> ScriptRuntime::BoxModel(Vector3 size, const std::string& texture)
    {
        constexpr int NoMaterial = -256;
        constexpr int Corners = 8;
        static constexpr std::array<std::array<int, 3>, 12> triangles = {{
            {0, 1, 2}, {1, 2, 3}, {0, 4, 6}, {0, 2, 6}, {0, 1, 5}, {0, 5, 4},
            {1, 5, 7}, {1, 3, 7}, {3, 2, 6}, {3, 7, 6}, {5, 4, 6}, {5, 7, 6},
        }};
        auto half = size * 0.5f;
        auto bounds = Box{-half, half};
        auto part = std::make_shared<Formats::Model::Part>();
        for (auto i = 0; i < Corners; i++)
        {
            part->Vertices.emplace_back((i & 1) != 0 ? half.X : -half.X, (i & 2) != 0 ? half.Y : -half.Y, (i & 4) != 0 ? half.Z : -half.Z);
        }

        for (const auto& t : triangles)
        {
            part->TriangleIndices.insert(part->TriangleIndices.end(), t.begin(), t.end());
        }

        part->TriangleMaterials.assign(triangles.size(), NoMaterial);
        part->TriangleUvs.resize(triangles.size() * 3);
        part->Bounds = bounds;
        auto model = std::make_shared<Formats::Model>();
        model->Name = texture;
        model->PartList = {part};
        model->Bounds = bounds;
        return model;
    }

    void ScriptRuntime::Remove(MdkObject& obj)
    {
        Rides->Lost(obj);
        if (obj.Attached != nullptr && !obj.Attached->Dead)
        {
            Remove(*obj.Attached);
        }

        // The object's sounds go with it.
        Mixer->StopVoice(obj.LoopSound);
        Mixer->StopVoice(obj.TrackedVoice);
        std::vector<int> voices;
        if (_following.Remove(&obj, voices))
        {
            for (auto voice : voices)
            {
                Mixer->StopVoice(voice);
            }

            voices.clear();
            _spareVoiceLists.push(std::move(voices));
        }

        obj.LoopSound = 0;
        obj.TrackedVoice = 0;
        obj.Dead = true;
        auto found = std::find_if(Objects.begin(), Objects.end(), [&](const std::shared_ptr<MdkObject>& o) { return o.get() == &obj; });
        if (found != Objects.end())
        {
            Objects.erase(found);
        }
    }

    void ScriptRuntime::Kill(MdkObject& obj, float yaw)
    {
        obj.Health = 0;
        NoteAlarmEnded(obj);
        if (obj.DeathScript == 0)
        {
            Explode(obj, yaw);
            return;
        }

        obj.MoveCommand = 0;
        obj.Flags |= MdkObject::FlagNotTarget;
        obj.Restart = obj.DeathScript;
        obj.WaitTime = 0.0f;
        obj.WaitResume = obj.DeathScript;
        obj.DeathScript = 0;
    }

    void ScriptRuntime::PlayStrikeScene(Scripts::StrikeScene::Kind kind, Scripts::StrikeScene::Plane plane)
    {
        auto scene = std::make_shared<Scripts::StrikeScene>();
        Strike = scene;
        scene->Finished = [this]()
        {
            if (StrikeSceneChanged)
            {
                StrikeSceneChanged(StrikeState::Ended);
            }
        };
        if (StrikeSceneChanged)
        {
            StrikeSceneChanged(StrikeState::Started);
        }

        scene->Play(*this, kind, plane);
    }

    void ScriptRuntime::Explode(MdkObject& obj, float yaw)
    {
        constexpr float MinHeight = 0.1f;
        // Remove drops the list's reference: keep the object alive for the rest.
        auto keep = obj.shared_from_this();
        auto bounds = GetWorldBounds(obj);
        auto center = obj.Model != nullptr ? Center(bounds) : obj.Position;
        PlaySoundAt(!obj.Labels[0].empty() ? obj.Labels[0] : std::string(ExplodeSound), center);
        FlashAt(center);
        BreakUp(obj);
        Remove(obj);
        auto effect = SpawnExplosion(obj.Arena, center, 1.0f, yaw, ExplosionLift);
        if (effect != nullptr && effect->Model != nullptr)
        {
            effect->Scale = Size(bounds).Z / MathF::Max(Size(effect->Model->Bounds).Z, MinHeight) * ExplosionSizeFactor;
        }
    }

    void ScriptRuntime::FlashAt(Vector3 point)
    {
        auto distance = Vector3::Distance(KurtPosition, point);
        if (distance <= 0.0f || Kurt->WhiteFlash >= ExplosionFlashMax)
        {
            return;
        }

        Kurt->WhiteFlash = MathF::Min(Kurt->WhiteFlash + MathF::Round(ExplosionFlash / distance), ExplosionFlashMax);
    }

    void ScriptRuntime::BreakUp(MdkObject& obj)
    {
        constexpr std::string_view BreakUpSuffix = "D";
        constexpr float DropSpread = 1.0f / 16384.0f;
        auto model = obj.Model != nullptr ? FindModel(obj.Arena, obj.Model->Name + std::string(BreakUpSuffix)) : nullptr;
        if (model == nullptr)
        {
            auto [colour, range] = SparkColours[static_cast<int>(Spark::Fire)];
            Debris->Spark(obj.Arena, obj.Position, FireSparks, 1.0f, colour, range);
            return;
        }

        Debris->BreakUp(obj, *model);

        // Slime drops thrown like the pieces, with gore (0x43d55e).
        if (Option == 0)
        {
            return;
        }

        auto velocity = obj.Position - obj.PreviousPosition;
        for (auto i = 0; i < GoreDrops; i++)
        {
            // C# evaluates the constructor's arguments left to right: keep that order.
            auto x = static_cast<float>(Rng->Next(32768) - 0x4000);
            auto y = static_cast<float>(Rng->Next(32768) - 0x4000);
            auto z = static_cast<float>(Rng->Next(32768) - 0x800);
            auto thrown = Vector3(x, y, z) * DropSpread;
            auto scale = GoreScale + Rng->Next(32768) * 5e-5f;
            Effects->SpawnDrop(obj.Arena, obj.Position, velocity + thrown, scale);
        }
    }

    std::shared_ptr<Game::Objects::MdkObject> ScriptRuntime::SpawnExplosion(const std::string& arena, Vector3 center, float scale, float yaw, float lift)
    {
        auto modelName = Cmi->ModelOffsets.begin() != Cmi->ModelOffsets.end() ? Cmi->ModelOffsets.begin()->first : std::string();
        auto effect = Spawn(*GetArenaState(arena)->Controller, modelName, center, yaw, -1, 0, Spawning::Plain);
        if (effect == nullptr || effect->Model == nullptr)
        {
            return nullptr;
        }

        effect->Flags |= MdkObject::FlagNotTarget | MdkObject::FlagNotSolid2;
        effect->EffectFrames = ExplosionFrames;
        auto texture = !effect->Model->Materials.empty() ? FindTexture(arena, effect->Model->Materials[0]) : nullptr;
        if (texture != nullptr)
        {
            effect->EffectFrames = texture->FrameCount;
        }

        effect->Scale = scale;
        auto horizontal = Distance2D(Eye, center);
        effect->Yaw = yaw == 0.0f ? Wrap360(Heading(center, Eye)) : yaw;
        auto rise = Eye.Z + lift - center.Z;
        if (horizontal > ExplosionNear || MathF::Abs(rise) > ExplosionNearHeight)
        {
            effect->Pitch = ToDegrees(MathF::Atan2(rise, horizontal));
        }

        effect->TextureFrame = 0;
        return effect;
    }

    void ScriptRuntime::FallOut(MdkObject& obj)
    {
        constexpr float Restore = 150.0f;
        if (obj.DeathScript == 0)
        {
            Remove(obj);
            return;
        }

        Kill(obj);
        obj.Velocity.Z = 0.0f;
        obj.Position.Z = GetArenaFloor(obj.Arena) - Restore;
        obj.Flags &= ~MdkObject::FlagGravity;
    }

    void ScriptRuntime::FireChainGun()
    {
        constexpr int ShotsPerTick = 6;
        auto origin = KurtPosition + Vector3(0.0f, 0.0f, EyeHeight);

        // The super chain gun does 6 times the damage while its time lasts.
        auto superGun = Kurt->Inventory->SuperChainGun > 0;
        auto damage = ChainGunDamage * (superGun ? SuperChainGunFactor : 1);
        if (superGun)
        {
            Kurt->Inventory->TickSuperChainGun(1);
        }

        Stats->Shots += ShotsPerTick;
        std::shared_ptr<MdkObject> best;
        auto bestScore = -1.0f;
        auto bestPart = -1;
        Box bestBounds{};
        for (const auto& obj : Objects)
        {
            if (obj->Dead || obj->Arena != CurrentArena || obj->Health == 0 || (obj->Flags & (MdkObject::FlagNotSolid | MdkObject::FlagNotTarget)) != 0)
            {
                continue;
            }

            // Weak parts are targets of their own.
            if ((obj->Flags & MdkObject::FlagWeakParts) != 0 && obj->Model != nullptr)
            {
                const auto& partBounds = obj->PartBounds();
                for (auto i = 0; i < static_cast<int>(obj->Model->PartList.size()); i++)
                {
                    if ((obj->HiddenParts & (1 << (i & 31))) != 0 || !IsWeakPart(*obj, i) || !partBounds[i].has_value())
                    {
                        continue;
                    }

                    auto partBox = GetWorldBounds(*obj, *partBounds[i]);
                    auto partScore = AimScore(partBox, origin, bestScore);
                    if (partScore < 0.0f)
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

            auto bounds = GetWorldBounds(*obj);
            auto score = AimScore(bounds, origin, bestScore);
            if (score >= 0.0f)
            {
                best = obj;
                bestScore = score;
                bestPart = -1;
                bestBounds = bounds;
            }
        }

        if (best != nullptr)
        {
            ChainGunHit(*best, bestPart, bestBounds, origin, damage, superGun ? Gun::Super : Gun::Normal);
            return;
        }

        auto direction = FromAngle(TargetYaw) * ChainGunWallReach;
        auto shot = Vector3(direction.X, direction.Y, 0.0f);
        auto hit = Raycast(origin, origin + shot);
        if (!hit.has_value())
        {
            return;
        }

        auto reacted = (HitGroupAt(*hit, damage, HitChainGun, superGun ? -2 : -1) & 1) != 0;
        SparkAt(hit->Point - Vector3::Normalize(shot), 1, "", reacted ? Spark::Group : Spark::Hard);
    }

    int ScriptRuntime::HitGroupAt(const RayHit& hit, int amount, int kind, int weapon)
    {
        return HitGroup(hit.Arena, hit.Group, amount, kind, weapon);
    }

    int ScriptRuntime::HitGroup(const std::string& arena, int group, int amount, int kind, int weapon)
    {
        constexpr int ScriptRan = 1;
        constexpr int Stopped = 2;
        if (group < 1 || group > GroupCount)
        {
            return 0;
        }

        auto state = GetArenaState(arena);
        auto i = group - 1;
        auto flags = state->GroupHitFlags[i];
        auto always = false;
        auto result = 0;
        if ((flags & kind) != 0)
        {
            if ((flags & GroupDestructible) != 0)
            {
                // A destructible group: its damaged version appears.
                _groups->SetState(arena, group, GroupShow);
            }

            if ((flags & GroupAlwaysHit) != 0)
            {
                always = true;
                amount = Math::Max(amount, 1);
            }

            if ((flags & GroupStopsHit) != 0)
            {
                result = Stopped;
            }
        }

        if (state->GroupHitScripts[i] == 0 || ((state->GroupHitMasks[i] & kind) == 0 && !always))
        {
            return result;
        }

        state->GroupCounters[i] += amount;

        // The script runs at once, from its start, in the arena's scratch object (0x45c9a0).
        auto& scratch = *state->HitScripts;
        scratch.HitType = weapon;
        scratch.HitEvent = 0;
        scratch.WaitTime = 0.0f;
        scratch.GosubReturns.clear();
        scratch.GosubRestarts.clear();
        scratch.Restart = state->GroupHitScripts[i];
        Vm->Run(scratch);
        return result | ScriptRan;
    }

    std::vector<Vector3> ScriptRuntime::GroupCenters(const std::string& arena, int group)
    {
        std::vector<Vector3> centers;
        auto data = Level->ArenaNamed(arena);
        auto triangles = _groups->Get(arena, group);
        if (data == nullptr || triangles == nullptr || Any(triangles->State & TriangleGroups::State::NotSolid))
        {
            return centers;
        }

        for (auto t : triangles->Triangles)
        {
            auto i = t * 3;
            centers.push_back((data->Vertices[data->TriangleIndices[i]] + data->Vertices[data->TriangleIndices[i + 1]] + data->Vertices[data->TriangleIndices[i + 2]]) / 3.0f);
        }

        return centers;
    }

    void ScriptRuntime::SetGroupState(const std::string& arena, int group, int operation)
    {
        _groups->SetState(arena, group, operation);
    }

    void ScriptRuntime::SetGroupTexture(const std::string& arena, int group, int value)
    {
        _groups->SetMaterial(arena, group, value);
    }

    void ScriptRuntime::RaiseShake(float amount)
    {
        Shake = MathF::Max(Shake, amount);
        if (ShakeRaised)
        {
            ShakeRaised(amount);
        }
    }

    Vector3 ScriptRuntime::ClipView(Vector3 from, Vector3 to)
    {
        constexpr int Blocks = 0x1000000;
        constexpr int Ignored = MdkObject::FlagNotSolid | MdkObject::FlagNotSolid2;
        auto end = to;
        for (const auto& obj : Objects)
        {
            if (obj->Dead || obj->Health == 0 || obj->Arena != CurrentArena || obj->Model == nullptr || (obj->Flags & Blocks) == 0
                || (obj->Flags & Ignored) != 0)
            {
                continue;
            }

            for (const auto& part : obj->PartBounds())
            {
                // A part the head is in doesn't count (0x45f588 returns 2).
                if (!part.has_value())
                {
                    continue;
                }

                auto box = GetWorldBounds(*obj, *part);
                if (Contains(box, from))
                {
                    continue;
                }

                if (auto entry = SegmentEntry(box, from, end))
                {
                    end = *entry;
                }
            }
        }

        return end;
    }

    float ScriptRuntime::AimScore(const Box& bounds, Vector3 origin, float bestScore)
    {
        constexpr float MinSize = 10.0f;
        auto size = MathF::Max(Size(bounds).Length(), MinSize);
        auto center = Center(bounds);
        auto distance = Vector3::Distance(origin, center);
        if (distance > size + ChainGunReach)
        {
            return -1.0f;
        }

        auto angle = Wrap360(Heading(origin, center) - TargetYaw);
        auto cone = (size - 2.0f) * 90.0f / (size - 2.0f + distance);
        if (angle > cone && angle < FullTurn - cone)
        {
            return -1.0f;
        }

        auto height = center.Z - KurtPosition.Z;
        auto score = Vector2(center.X - origin.X, center.Y - origin.Y).LengthSquared() + 4.0f * height * height;
        if (bestScore >= 0.0f && score > bestScore)
        {
            return -1.0f;
        }

        return Raycast(origin, center).has_value() ? -1.0f : score;
    }

    bool ScriptRuntime::IsWeakPart(MdkObject& obj, int index)
    {
        const auto& name = obj.Model->PartList[index]->Name;
        auto length = obj.WeakPrefixLength;
        if (static_cast<int>(name.size()) <= length || name[length] < '0' || name[length] > '9')
        {
            return false;
        }

        auto prefix = obj.WeakPrefix.substr(0, Math::Min(static_cast<size_t>(length), obj.WeakPrefix.size()));
        return Str::StartsWith(name, prefix);
    }

    void ScriptRuntime::ChainGunHit(MdkObject& obj, int part, const Box& bounds, Vector3 origin, int damage, Gun gun)
    {
        auto superGun = gun == Gun::Super;
        auto center = Center(bounds);
        auto direction = Heading(origin, center);
        Stats->ShotHits++;
        obj.HitEvent = -1;
        if (IsBeta() && part < 0)
        {
            auto hit = BetaHitPart(obj, origin);
            if (hit >= 0)
            {
                obj.HitEvent = hit + 1;
            }
        }

        if (part >= 0 && part < static_cast<int>(obj.PartHealth.size()))
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
                BarObject = obj.shared_from_this();
                BarTime = 1.0f;
            }

            // Sparks on the side of the box facing Kurt.
            auto toward = FromAngle(direction);
            auto size = Size(bounds);
            auto point = center - Vector3(toward.X * size.X, toward.Y * size.Y, 0.0f) * 0.5f;
            SparkAt(point, 1, obj.Labels[1], obj.Indestructible && part < 0 ? Spark::Hard : Spark::Flesh);
            return;
        }

        obj.Health = 0;
        if (superGun)
        {
            // The super chain gun throws what it kills away.
            auto push = FromAngle(direction) * SuperChainGunPush;
            obj.Velocity += Vector3(push.X, push.Y, 0.0f);
        }

        Stats->CountEnemy(obj.TypeName, GameStats::Kill::Killed);
        Kill(obj, direction + HalfTurn);
    }

    void ScriptRuntime::FlattenTown()
    {
        RaiseShake(TownShake);
        auto text = std::format("OOT_L{}", GameStats::IndexOf(Level->Number) + 1);
        if ((GlobalFlags & FlagSecondTown) != 0)
        {
            text += "A";
            GlobalFlags |= FlagSecondTownFlattened;
        }
        else
        {
            GlobalFlags |= FlagTownFlattened;
        }

        if (Messages != nullptr)
        {
            Messages->Push(text, Hud::Messages::FlagZoom | Hud::Messages::FlagFront, TownMessageSeconds);
        }
    }

    void ScriptRuntime::ShowBarValues(int health, int maxHealth)
    {
        BarObject = nullptr;
        BarHealth = health;
        BarMax = maxHealth;
        BarTime = health > 0 ? 1.0f : 0.0f;
    }

    ScriptRuntime::Bar ScriptRuntime::GetBar()
    {
        if (BarTime <= 0.0f)
        {
            return {0, 0};
        }

        return BarObject != nullptr ? Bar{BarObject->Health, BarObject->MaxHealth} : Bar{BarHealth, BarMax};
    }

    void ScriptRuntime::UpdateBar()
    {
        if (BarTime <= 0.0f)
        {
            return;
        }

        BarTime -= Tick;
        if (BarObject != nullptr && BarObject->Dead)
        {
            BarObject = nullptr;
            BarTime = 0.0f;
            return;
        }

        auto [health, max] = GetBar();
        if (max == 0 || max > MaxBarHealth || health < 1)
        {
            BarTime = 0.0f;
        }
    }

    void ScriptRuntime::SparkAt(Vector3 point, int count, const std::string& sound, Spark kind)
    {
        constexpr int RicochetEvery = 3;
        auto quiet = !Effects->HasEffects(CurrentArena) && !Debris->HasPieces(CurrentArena);
        if (quiet || count > 1 || (_tickCount & RicochetEvery) == 0)
        {
            PlaySoundAt(!sound.empty() ? sound : std::string(Ricochets[Rng->Next(static_cast<int>(Ricochets.size()))]), point);
        }

        auto [colour, range] = kind != Spark::Flesh || Option != 0 ? SparkColours[static_cast<int>(kind)] : SparkFleshNoGore;
        Debris->Spark(CurrentArena, point, count, SparkSize, colour, range, kind == Spark::Hard ? SparkSlow : 1.0f);
    }

    void ScriptRuntime::SparkFire(const std::string& arena, Vector3 point)
    {
        auto [colour, range] = SparkColours[static_cast<int>(Spark::Fire)];
        Debris->Spark(arena, point, FireSparks, 1.0f, colour, range);
    }

    void ScriptRuntime::FanSpark(const std::string& arena, Vector3 point)
    {
        auto [colour, range] = SparkColours[static_cast<int>(Spark::Fire)];
        Debris->Spark(arena, point, 1, SparkSize, colour, range, 1.0f, Scripts::Debris::Launch::Still);
    }

    std::optional<Vector3> ScriptRuntime::KeeperAt(MdkObject* owner)
    {
        if (owner != nullptr && !owner->Dead && (owner->Flags & MdkObject::FlagStandable) != 0)
        {
            return owner->Position;
        }

        return std::nullopt;
    }

    float ScriptRuntime::KurtUpdraft(Vector3 feet, float vz, float dt)
    {
        auto lifted = Fans->Query(CurrentArena, feet, vz, Scripts::Fans::MaskKurt, dt);
        if (!std::isnan(lifted) || SecondArena.empty())
        {
            return lifted;
        }

        return Fans->Query(SecondArena, feet, vz, Scripts::Fans::MaskKurt, dt);
    }

    int ScriptRuntime::TextureFrames(const std::string& arena, const std::string& name)
    {
        auto texture = FindTexture(arena, name);
        return texture != nullptr ? texture->FrameCount : 0;
    }

    std::optional<ScriptRuntime::RayHit> ScriptRuntime::RayIn(const std::string& arena, Vector3 from, Vector3 to)
    {
        std::shared_ptr<Collision::Bsp> bsp;
        if (!_bsps.TryGetValue(arena, bsp))
        {
            return std::nullopt;
        }

        Vector3 point;
        auto triangle = bsp->Segment(from, to, Collision::Bsp::SegmentMode::Any, point, Collision::Bsp::Clip::PassesThrough);
        if (triangle == Collision::Bsp::None)
        {
            return std::nullopt;
        }

        return RayHit{point, TriangleNormal(*bsp->Arena, triangle), arena, TriangleGroup(*bsp->Arena, triangle)};
    }

    std::optional<Scripts::Debris::Group> ScriptRuntime::GroupFacets(const std::string& arena, int number)
    {
        auto data = Level->ArenaNamed(arena);
        auto group = _groups->Get(arena, number);
        if (data == nullptr || group == nullptr || number == 0)
        {
            return std::nullopt;
        }

        std::vector<Scripts::Debris::Facet> facets;
        for (auto t : group->Triangles)
        {
            auto corner = [&](int k) { return data->Vertices[data->TriangleIndices[t * 3 + k]]; };
            auto uv = [&](int k) { return data->TriangleUvs[t * 3 + k]; };
            facets.push_back(Scripts::Debris::Facet{corner(0), corner(1), corner(2), uv(0), uv(1), uv(2), group->Material.value_or(data->TriangleMaterials[t])});
        }

        // The names are the arena's own list (the C# shares it): an aliasing pointer keeps the arena.
        return Scripts::Debris::Group{std::shared_ptr<const std::vector<std::string>>(data, &data->Materials), std::move(facets)};
    }

    void ScriptRuntime::SetLoopSound(MdkObject& obj, const std::string& name)
    {
        Mixer->StopVoice(obj.LoopSound);
        obj.LoopSound = 0;
        obj.LoopSoundName = name;
        if (name.empty())
        {
            return;
        }

        // It only loops if the sound itself does (its SNI flag).
        obj.LoopSound = PlayOn(name, obj, Audio::SoundMixer::Start::New, Vector3::Zero());
    }

    void ScriptRuntime::WindZone(const std::string& arena, Wind wind, float yaw, float speed)
    {
        if (wind == Wind::Off)
        {
            Kurt->StopSlide();
            return;
        }

        for (const auto& record : ArenaRecords(arena))
        {
            if (record.Type != DtiWind || !Contains(Box{record.Position, record.BoxEnd}, KurtPosition))
            {
                continue;
            }

            if (Kurt->Sliding || Kurt->OnFloor)
            {
                Kurt->StartSlide();
                Kurt->Yaw = yaw;
                Kurt->SlideAccel(FromAngle(yaw) * speed, Tick);
            }
            else
            {
                Kurt->Blow(Tick);
            }

            return;
        }
    }

    void ScriptRuntime::PickWaypoint8(MdkObject& obj, int mode)
    {
        constexpr int PickCommand = 221;
        obj.MoveCommand = 0;
        auto records = OfType(ArenaRecords(obj.Arena), DtiWaypoint);
        auto nearestId = 0;
        if (mode == 1)
        {
            auto nearest = 999999.0f;
            for (const auto& record : records)
            {
                auto distance = Vector3::Distance(obj.Position, record.Position);
                if (distance < nearest)
                {
                    nearest = distance;
                    nearestId = record.Id;
                }
            }
        }

        auto& weights = _weights;
        weights.clear();
        auto total = 0;
        for (const auto& record : records)
        {
            auto spot = record.Position;
            auto distance = Distance2D(spot, obj.Position);
            auto weight = 0;
            if ((mode == 1 && Math::Abs(record.Id - nearestId) <= 3) || (mode != 1 && distance >= 50.0f && distance <= 500.0f))
            {
                auto toKurt = Distance2D(spot, KurtPosition);
                weight = Round(500.0f - distance);
                if (toKurt < 200.0f)
                {
                    weight = Round(weight + 200.0f - toKurt);
                }

                if (Distance2D(obj.Position, KurtPosition) < distance && toKurt > distance)
                {
                    weight += 250;
                }

                weight = Round(weight - MathF::Abs(spot.Z - obj.Position.Z) * 0.5f);
                weight = Math::Max(weight, 1);
            }

            weights.push_back(weight);
            total += weight;
        }

        if (total == 0)
        {
            return;
        }

        auto pick = Rng->Next(total);
        for (size_t i = 0; i < records.size(); i++)
        {
            pick -= weights[i];
            if (pick >= 0)
            {
                continue;
            }

            obj.MoveCommand = PickCommand;
            obj.MoveDestination = records[i].Position;
            obj.Waypoint = obj.MoveDestination;
            obj.ContactFlags &= ~MdkObject::ContactStuck;
            return;
        }
    }

    void ScriptRuntime::FindAdvanceSpot(MdkObject& obj)
    {
        constexpr int AdvanceCommand = 197;
        obj.MoveCommand = 0;
        auto toKurt = Distance2D(obj.Position, KurtPosition);
        auto& spots = _spots;
        spots.clear();
        for (const auto& record : OfType(ArenaRecords(obj.Arena), DtiCover))
        {
            auto spot = record.Position;
            auto distance = Distance2D(spot, obj.Position);
            if (distance < 5.0f || distance > 400.0f || distance > toKurt || Distance2D(spot, KurtPosition) >= toKurt)
            {
                continue;
            }

            spots.push_back(spot);
        }

        if (spots.empty())
        {
            return;
        }

        obj.MoveCommand = AdvanceCommand;
        obj.MoveDestination = spots[Rng->Next(static_cast<int>(spots.size()))];
        obj.Waypoint = obj.MoveDestination;
        obj.ContactFlags &= ~MdkObject::ContactStuck;
    }

    void ScriptRuntime::FindCoverSpot(MdkObject& obj)
    {
        constexpr int CoverCommand = 43;
        const auto& nearerFirst = NearerFirst;
        auto up = Vector3(0.0f, 0.0f, EyeHeight);
        auto toKurt = obj.DistanceTo(KurtPosition);
        auto& spots = _spots;
        spots.clear();
        for (const auto& record : OfType(ArenaRecords(obj.Arena), DtiCover))
        {
            auto spot = record.Position;
            auto distance = obj.DistanceTo(spot);
            if (distance < 9.0f || distance > 400.0f || distance > toKurt || Vector3::Distance(spot, KurtPosition) >= toKurt)
            {
                continue;
            }

            if (!Raycast(KurtPosition + up, spot + up).has_value() || Raycast(obj.Position + up, spot + up).has_value())
            {
                continue;
            }

            spots.push_back(spot);
        }

        obj.MoveCommand = 0;
        if (spots.empty())
        {
            return;
        }

        // List.Sort sorts these few spots by insertion (stable).
        std::stable_sort(spots.begin(), spots.end(), [&](Vector3 a, Vector3 b) { return obj.DistanceTo(a) < obj.DistanceTo(b); });
        auto choice = spots.size() >= 3 ? nearerFirst[Rng->Next(static_cast<int>(nearerFirst.size()))] : Rng->Next(static_cast<int>(spots.size()));
        auto destination = spots[Math::Min(choice, static_cast<int>(spots.size()) - 1)];
        obj.MoveCommand = CoverCommand;
        obj.MoveDestination = destination;
        obj.Waypoint = destination;
        obj.Path = 0;
    }

    const std::vector<Formats::Dti::Record>& ScriptRuntime::OfType(const std::vector<Formats::Dti::Record>& records, uint type)
    {
        _spotRecords.clear();
        for (const auto& record : records)
        {
            if (record.Type == type)
            {
                _spotRecords.push_back(record);
            }
        }

        return _spotRecords;
    }

    void ScriptRuntime::PlaySoundAt(const std::string& name, Vector3 point)
    {
        Mixer->PlayAt(name, point);
    }

    void ScriptRuntime::DropCow()
    {
        Mixer->Play("COW", Audio::SoundMixer::Start::Restart);
        auto target = CowTarget();
        auto point = target != nullptr ? target->Position : KurtPosition;
        auto cow = Spawn(*GetArenaState(CurrentArena)->Controller, "SW_HCOW", point + Vector3(0.0f, 0.0f, CowHeight), 0.0f, -1, 0, Spawning::Plain);
        if (cow == nullptr)
        {
            return;
        }

        cow->Flags = CowFlags;
        cow->Health = Indestructible;
        cow->Velocity.Z = -cow->Gravity;
        cow->CowTarget = target;
        cow->ParameterTimer = CowFuse;
    }

    std::shared_ptr<Game::Objects::MdkObject> ScriptRuntime::CowTarget()
    {
        std::shared_ptr<MdkObject> best;
        auto bestScore = std::numeric_limits<float>::infinity();
        for (const auto& obj : Objects)
        {
            if (obj->Dead || obj->Arena != CurrentArena || (obj->Flags & (MdkObject::FlagNotSolid | MdkObject::FlagNotTarget)) != 0
                || obj->Health >= Indestructible)
            {
                continue;
            }

            auto distance = obj->DistanceTo(KurtPosition);
            if (distance > CowRange)
            {
                continue;
            }

            auto angle = MathF::Abs(WrapAngle(KurtYaw - Heading(KurtPosition, obj->Position)));
            auto score = distance + (angle < 30.0f ? 0.0f : angle < 50.0f ? 400.0f : 1000.0f);
            if (score > bestScore)
            {
                continue;
            }

            auto above = obj->Position + Vector3(0.0f, 0.0f, EyeHeight);
            if (Raycast(above, above + Vector3(0.0f, 0.0f, CowHeight)).has_value())
            {
                continue;
            }

            auto covered = std::any_of(Objects.begin(), Objects.end(),
                [&](const std::shared_ptr<MdkObject>& other) { return (other->Flags & MdkObject::FlagCow) != 0 && other->CowTarget == obj; });
            if (covered)
            {
                continue;
            }

            best = obj;
            bestScore = score;
        }

        return best;
    }

    void ScriptRuntime::CollectPickups()
    {
        constexpr float ShrinkTicks = 30.0f;
        auto objects = Objects;
        for (const auto& obj : objects)
        {
            if (obj->Dead || obj->Arena != CurrentArena || (obj->Flags & MdkObject::FlagPickup) == 0
                || (obj->Flags & (MdkObject::FlagCollected | MdkObject::FlagNotSolid)) != 0)
            {
                continue;
            }

            auto world = GetWorldBounds(*obj);
            auto bounds = Box{world.Min - Vector3(1.0f, 1.0f, 5.0f), world.Max + Vector3(1.0f, 1.0f, 1.0f)};
            if (!Contains(bounds, KurtPosition) && !SegmentEntry(bounds, _previousKurtPosition, KurtPosition).has_value())
            {
                continue;
            }

            auto sound = Kurt->Collect(obj->TypeName);
            if (sound.empty())
            {
                continue;
            }

            Mixer->Play(sound, Audio::SoundMixer::Start::Restart);
            if (obj->TypeName == "SW_EWJ")
            {
                DropCow();
            }

            obj->Flags |= MdkObject::FlagCollected | Scripts::Items::FlagThrown;
            obj->ParameterTimer = ShrinkTicks;
            obj->Velocity = Vector3::Zero();
            obj->Flags &= ~MdkObject::FlagGravity;
            if (obj->Attached != nullptr)
            {
                Remove(*obj->Attached);
                obj->Attached = nullptr;
            }
        }
    }

    void ScriptRuntime::SpawnConnector(MdkObject& obj, const std::string& type, Vector3 position, float yaw, int instance, const std::string& otherArena, int script)
    {
        constexpr int DoorFlags = 0x1108000;
        if (otherArena == NoArena)
        {
            return;
        }

        for (const auto& other : Objects)
        {
            if (other->Dead || other->TypeName != type
                || !((other->Arena == obj.Arena && other->Connects == otherArena) || (other->Arena == otherArena && other->Connects == obj.Arena)))
            {
                continue;
            }

            // A door Kurt can see (his arena or the second one) stays where it is.
            if (other->Arena != CurrentArena && other->Arena != SecondArena)
            {
                other->Arena = obj.Arena;
                other->Connects = otherArena;
            }

            return;
        }

        auto door = Spawn(obj, type, position, yaw, instance, script, Spawning::Plain);
        if (door == nullptr)
        {
            return;
        }

        door->Flags |= DoorFlags;
        door->Connects = otherArena;
        Behaviors->SetupDoor(*door);
    }

    std::shared_ptr<Game::Objects::MdkObject> ScriptRuntime::Fire(MdkObject& obj, const OperandList& origin, const std::string& bullet, int script)
    {
        constexpr int ProjectileCommand = 61;
        Vector3 start;
        if (I(origin[0]) == 0)
        {
            start = obj.ReferencePoint(I(origin[1]));
        }
        else
        {
            const auto* name = std::get_if<std::string>(&origin[1]);
            start = obj.PartCenter(obj.FindPart(name != nullptr ? *name : std::string()));
        }

        auto shot = Spawn(obj, bullet, start, obj.Yaw, -1, 0, Spawning::Plain);
        if (shot == nullptr)
        {
            return nullptr;
        }

        shot->MoveCommand = ProjectileCommand;
        shot->Flags |= ProjectileFlags;
        shot->Pitch = obj.Pitch;
        shot->Restart = script;
        return shot;
    }

    void ScriptRuntime::HurtKurt(int damage)
    {
        // Riding the XD2, it takes the hits (0x46a498).
        if (!Rides->TakesHits())
        {
            Kurt->Hurt(damage);
            return;
        }

        auto ridden = Rides->Ridden;
        if (ridden->Health >= Indestructible)
        {
            return;
        }

        ridden->Health -= damage;
        if (ridden->Health < 1)
        {
            Kill(*ridden);
        }
    }

    bool ScriptRuntime::KurtLooksAt(MdkObject& obj, float minRange, float maxRange, float cone)
    {
        auto distance = Vector3::Distance(KurtPosition, obj.Position);
        if (distance < minRange || distance > maxRange)
        {
            return false;
        }

        auto angle = Wrap360(MathF::Abs(KurtYaw - Heading(KurtPosition, obj.Position)));
        auto allowed = (cone - 90.0f) / maxRange * distance + 90.0f;
        if (angle > allowed && angle < FullTurn - allowed)
        {
            return false;
        }

        return !Raycast(KurtPosition + Vector3(0.0f, 0.0f, EyeHeight), obj.Position + Vector3(0.0f, 0.0f, TargetHeight)).has_value();
    }

    std::shared_ptr<Game::Objects::MdkObject> ScriptRuntime::GetKurtPlatform()
    {
        return Kurt->OnFloor && Kurt->Platform != nullptr ? Kurt->Platform->shared_from_this() : nullptr;
    }

    std::span<const Collision::Solids::Solid> ScriptRuntime::SolidsWithin(const Box& region)
    {
        // Kurt uses each answer at once: one list does.
        auto& solids = _solidsWithin;
        solids.clear();
        for (const auto& obj : Objects)
        {
            if (obj->Dead || obj->Arena != CurrentArena || obj->Health == 0 || obj->Model == nullptr)
            {
                continue;
            }

            auto footing = obj->Footing();
            if (!footing.has_value() || obj == Rides->Ridden || !Intersects(GetWorldBounds(*obj), region))
            {
                continue;
            }

            const auto& parts = obj->PartBounds();
            for (auto i = 0; i < static_cast<int>(parts.size()); i++)
            {
                if (((obj->HiddenParts | obj->LockParts) & (1 << (i & 31))) != 0 || !parts[i].has_value())
                {
                    continue;
                }

                solids.push_back(Collision::Solids::Solid{GetWorldBounds(*obj, *parts[i]), obj.get(), *footing, i});
            }
        }

        return solids;
    }

    bool ScriptRuntime::TouchDamage(MdkObject& obj, int targets, int damage, int flags)
    {
        constexpr int HurtsKurt = 1;
        constexpr int HurtsObjects = 2;
        constexpr int DiesOnHit = 1;
        constexpr int Untouchable = 0x820;
        auto box = GetWorldBounds(obj);
        auto hit = false;
        if ((targets & HurtsKurt) != 0)
        {
            auto kurtBox = GetKurtBox();
            if (Intersects(box, kurtBox))
            {
                const auto& partBounds = obj.PartBounds();
                for (auto i = 0; i < static_cast<int>(partBounds.size()); i++)
                {
                    if ((obj.HiddenParts & (1 << (i & 31))) != 0 || !partBounds[i].has_value() || !Intersects(GetWorldBounds(obj, *partBounds[i]), kurtBox))
                    {
                        continue;
                    }

                    Kurt->Hurt(damage);
                    hit = true;
                }
            }
        }

        if ((targets & HurtsObjects) != 0)
        {
            auto copy = Objects;
            for (const auto& other : copy)
            {
                if (other.get() == &obj || other->Dead || other->Arena != obj.Arena || (other->Flags & Untouchable) != 0)
                {
                    continue;
                }

                if (!Intersects(box, GetWorldBounds(*other)))
                {
                    continue;
                }

                hit = true;
                other->HitEvent = -3;
                other->HitType = -4;
                other->HitDirection = obj.Yaw;
                if (other->Health < Indestructible)
                {
                    other->Health -= damage;
                }

                if (other->Health < 1)
                {
                    Kill(*other);
                }
            }
        }

        if (hit && (flags & DiesOnHit) != 0)
        {
            Kill(obj);
        }

        return hit;
    }

    void ScriptRuntime::PushKurt(MdkObject& obj, const OperandList& args)
    {
        if (Kurt->Knocked() || Kurt->Health == 0)
        {
            return;
        }

        Vector2 push;
        float up;
        if (I(args[0]) == 0)
        {
            auto [s, c] = MathF::SinCos(ToRadians(obj.Yaw));
            push = Vector2(-F(args[1]) * c - F(args[2]) * s, -F(args[2]) * c - F(args[1]) * s);
            up = F(args[3]);
        }
        else
        {
            auto away = Vector2(KurtPosition.X - obj.Position.X, KurtPosition.Y - obj.Position.Y);
            push = away != Vector2::Zero() ? Vector2::Normalize(away) * F(args[1]) : Vector2(F(args[1]), 0.0f);
            up = F(args[2]);
        }

        // The push is added once per tick of the frame, in units per tick.
        push *= Tick * TicksPerSecond;
        Kurt->KnockDown(push);
        Kurt->AddVerticalSpeed(up);
    }

    Box ScriptRuntime::GetKurtBox()
    {
        return Box{KurtPosition - Vector3(KurtHalfWidth, KurtHalfWidth, 0.0f), KurtPosition + Vector3(KurtHalfWidth, KurtHalfWidth, KurtHeight)};
    }

    float ScriptRuntime::GetArenaFloor(const std::string& arena)
    {
        return _floors.GetValueOrDefault(arena, NoArenaFloor);
    }

    const std::vector<std::string>& ScriptRuntime::RayArenas()
    {
        if (CurrentArena.empty())
        {
            if (_bspNames.size() != static_cast<size_t>(_bsps.Count()))
            {
                _bspNames.clear();
                for (const auto& [name, bsp] : _bsps)
                {
                    _bspNames.push_back(name);
                }
            }

            return _bspNames;
        }

        _rayPair[0] = CurrentArena;
        _rayPair[1] = SecondArena;
        return _rayPair;
    }

    std::optional<ScriptRuntime::RayHit> ScriptRuntime::Raycast(Vector3 from, Vector3 to)
    {
        const auto& arenas = RayArenas();
        std::optional<RayHit> best;
        auto bestDistance = std::numeric_limits<float>::max();
        for (const auto& name : arenas)
        {
            std::shared_ptr<Collision::Bsp> bsp;
            if (!_bsps.TryGetValue(name, bsp))
            {
                continue;
            }

            Vector3 point;
            auto triangle = bsp->Segment(from, to, Collision::Bsp::SegmentMode::Any, point, Collision::Bsp::Clip::PassesThrough);
            auto distance = Vector3::DistanceSquared(from, point);
            if (triangle == Collision::Bsp::None || distance >= bestDistance)
            {
                continue;
            }

            bestDistance = distance;
            best = RayHit{point, TriangleNormal(*bsp->Arena, triangle), name, TriangleGroup(*bsp->Arena, triangle)};
        }

        return best;
    }

    bool ScriptRuntime::CrossesFloor(const std::string& arena, Vector3 from, Vector3 to)
    {
        std::shared_ptr<Collision::Bsp> bsp;
        if (!_bsps.TryGetValue(arena, bsp))
        {
            return false;
        }

        Vector3 point;
        return bsp->Segment(from, to, Collision::Bsp::SegmentMode::Floor, point, Collision::Bsp::Clip::PassesThrough) != Collision::Bsp::None;
    }

    std::optional<ScriptRuntime::RayHit> ScriptRuntime::Sweep(Vector3 from, Vector3 to, Vector3 half)
    {
        const auto& arenas = RayArenas();
        for (const auto& name : arenas)
        {
            std::shared_ptr<Collision::Bsp> bsp;
            if (!_bsps.TryGetValue(name, bsp))
            {
                continue;
            }

            Vector3 point;
            auto node = 0;
            auto triangle = bsp->SweepBox(from, to, half, 0, 0.0f, point, node);
            if (triangle == Collision::Bsp::None)
            {
                continue;
            }

            return RayHit{point, bsp->Arena->Nodes[node].Normal, name, TriangleGroup(*bsp->Arena, triangle)};
        }

        return std::nullopt;
    }

    Vector3 ScriptRuntime::TriangleNormal(Formats::Arena& arena, int triangle)
    {
        auto a = arena.Vertices[arena.TriangleIndices[triangle * 3]];
        auto b = arena.Vertices[arena.TriangleIndices[triangle * 3 + 1]];
        auto c = arena.Vertices[arena.TriangleIndices[triangle * 3 + 2]];
        auto normal = Vector3::Cross(b - a, c - a);
        return normal == Vector3::Zero() ? Vector3::UnitZ() : Vector3::Normalize(normal);
    }

    int ScriptRuntime::TriangleGroup(Formats::Arena& arena, int triangle)
    {
        constexpr int GroupShift = 24;
        return static_cast<int>(arena.TriangleFlags[triangle] >> GroupShift);
    }

    std::shared_ptr<Collision::Bsp> ScriptRuntime::BspOf(const std::string& arena)
    {
        return _bsps.GetValueOrDefault(arena);
    }

    Vector3 ScriptRuntime::NearTargetDestination(MdkObject& obj, float forward, float side)
    {
        auto direction = Heading(obj.Position, TargetPosition);
        auto offset = Rotated(Vector2(forward, side), direction) * 0.01f * obj.DistanceTo(TargetPosition);
        return Vector3(TargetPosition.X + offset.X, TargetPosition.Y + offset.Y, TargetPosition.Z + obj.HeightOffset);
    }

    bool ScriptRuntime::MayCommand(MdkObject& sender, MdkObject& receiver)
    {
        auto leader = receiver.Leader;
        if (leader != nullptr && leader.get() != &sender && !leader->Dead && sender.Priority <= leader->Priority)
        {
            return false;
        }

        return receiver.ObeyLevel >= sender.Priority;
    }

    void ScriptRuntime::CommandObjects(MdkObject& sender, int command, int target, Vector3 destination, int selector, const std::string& type, float param, int id)
    {
        constexpr float SightHeight = 8.0f;
        auto formationIndex = 0;
        if (selector == 9)
        {
            if (sender.Linked != nullptr && !sender.Linked->Dead)
            {
                CommandObject(sender, *sender.Linked, command, target, destination, formationIndex);
            }

            return;
        }

        auto up = Vector3(0.0f, 0.0f, SightHeight);
        for (const auto& receiver : GetArenaObjects(sender))
        {
            if (selector != 3 && selector != 8 && !Str::EqualsIgnoreCase(receiver->TypeName, type))
            {
                continue;
            }

            if (!MayCommand(sender, *receiver))
            {
                continue;
            }

            if ((selector == 7 || selector == 8) && receiver->Leader.get() != &sender)
            {
                continue;
            }

            if (selector == 5 && receiver->InstanceId != id)
            {
                continue;
            }

            if (selector == 6 && (sender.DistanceTo(receiver->Position) > param || Raycast(sender.Position + up, receiver->Position + up).has_value()))
            {
                continue;
            }

            if (selector == 10 && receiver->Position.Y < param)
            {
                continue;
            }

            if ((command == 7 && receiver->CommandTarget == target) || receiver->Health == 0)
            {
                continue;
            }

            if (CommandObject(sender, *receiver, command, target, destination, formationIndex))
            {
                formationIndex++;
            }

            if (selector == 4)
            {
                return;
            }
        }
    }

    bool ScriptRuntime::CommandObject(MdkObject& sender, MdkObject& receiver, int command, int target, Vector3 destination, int formationIndex)
    {
        switch (command)
        {
            case 1:
            {
                // Formation places alternate left and right, 5 units apart (wider for XE).
                auto wide = Str::EqualsIgnoreCase(receiver.TypeName, "XE");
                auto spread = wide ? 4.0f : 1.0f;
                auto back = wide ? 2.5f : 1.0f;
                auto side = (formationIndex & 1) != 0 ? 1.0f : -1.0f;
                receiver.Waypoint = Vector3(side * (formationIndex / 2 + 1) * 5.0f * spread, back * -4.0f, 8.0f);
                receiver.MoveDestination = ObjectMotion::FormationPosition(sender, receiver.Waypoint);
                receiver.Leader = sender.shared_from_this();
                receiver.MoveCommand = 1;
                receiver.Path = 0;
                return true;
            }
            case 7:
                receiver.WaitTime = 0.0f;
                receiver.Restart = target;
                receiver.WaitResume = target;
                receiver.Leader = sender.shared_from_this();
                receiver.GosubReturns.clear();
                receiver.GosubRestarts.clear();
                receiver.LevelTimers[0] = 0.0f;
                receiver.CommandTarget = target;
                break;
            case 0xFC:
                if (static_cast<int>(receiver.GosubReturns.size()) < ScriptVm::GosubDepth)
                {
                    receiver.GosubReturns.push_back(receiver.Restart);
                    receiver.GosubRestarts.push_back(receiver.Restart);
                    receiver.WaitTime = 0.0f;
                    receiver.Restart = target;
                    receiver.WaitResume = target;
                    receiver.Leader = sender.shared_from_this();
                    receiver.LevelTimers[receiver.GosubReturns.size()] = 0.0f;
                }

                break;
            case 43:
                if (receiver.Arena == CurrentArena)
                {
                    receiver.MoveDestination = destination;
                    receiver.Leader = sender.shared_from_this();
                    receiver.MoveCommand = 43;
                    receiver.Path = 0;
                    Motion->PlanMove(receiver);
                }

                break;
        }

        return false;
    }

    Box ScriptRuntime::GetWorldBounds(MdkObject& obj, std::optional<Box> bounds)
    {
        if (!bounds.has_value() && obj.Model == nullptr)
        {
            return Box{obj.Position, obj.Position};
        }

        auto box = bounds.has_value() ? *bounds : obj.PoseBounds();
        auto world = Box{obj.Position, obj.Position};
        for (auto i = 0; i < 8; i++)
        {
            auto corner = Vector3((i & 1) != 0 ? box.Max.X : box.Min.X, (i & 2) != 0 ? box.Max.Y : box.Min.Y, (i & 4) != 0 ? box.Max.Z : box.Min.Z);
            auto point = obj.Position + Vector3::UnitZ() * obj.Lift() + RotatedZ(corner * obj.Scale, obj.Yaw);
            world = i == 0 ? Box{point, point} : Expand(world, point);
        }

        return world;
    }

    std::vector<std::shared_ptr<Game::Objects::MdkObject>> ScriptRuntime::GetArenaObjects(MdkObject& obj)
    {
        std::vector<std::shared_ptr<MdkObject>> found;
        for (const auto& o : Objects)
        {
            if (o.get() != &obj && !o->Dead && o->Arena == obj.Arena)
            {
                found.push_back(o);
            }
        }

        return found;
    }

    std::shared_ptr<Formats::ModelAnimation> ScriptRuntime::GetAnimation(MdkObject& obj, int offset)
    {
        if (offset == 0)
        {
            return nullptr;
        }

        // The 1996 demo's scripts point at animations in the file.
        if (IsBeta())
        {
            return Cmi->GetBetaAnimation(offset);
        }

        const auto& bytes = Cmi->Bytes();
        if (Formats::Bin::U32(*bytes, offset) == 0)
        {
            return FindArenaAnimation(obj.Arena, Formats::Bin::Ascii(*bytes, offset + 4, AnimationNameLength));
        }

        std::shared_ptr<Formats::ModelAnimation> animation;
        if (!_animations.TryGetValue(offset, animation))
        {
            animation = _animations[offset] = Formats::ModelAnimation::Parse(std::format("{}{:x}", CmiAnimationPrefix, static_cast<uint>(offset)), bytes, offset);
        }

        return animation;
    }

    const Dictionary<int, int> ScriptRuntime::AnimationOperands = {{3, 1}, {59, 1}, {150, 2}};

    std::vector<std::shared_ptr<Formats::Model>> ScriptRuntime::LevelModels()
    {
        std::vector<std::shared_ptr<Formats::Model>> models;
        for (const auto& [name, offset] : Cmi->ModelOffsets)
        {
            if (auto model = Cmi->GetModel(name))
            {
                models.push_back(model);
            }
        }

        return models;
    }

    void ScriptRuntime::PreparePoses()
    {
        std::vector<std::shared_ptr<Formats::ModelAnimation>> animations;
        for (const auto& a : Level->Arenas)
        {
            for (const auto& [name, animation] : a->Animations)
            {
                animations.push_back(animation);
            }
        }

        for (const auto& animation : Items->Animations())
        {
            animations.push_back(animation);
        }

        auto anyObject = std::make_shared<MdkObject>();
        for (const auto& ins : _decoder->Decoded())
        {
            for (auto i = 0; i < AnimationOperands.GetValueOrDefault(ins->Opcode); i++)
            {
                const auto* offset = std::get_if<int>(&ins->Operands[i]);
                if (offset == nullptr || *offset == 0 || Formats::Bin::U32(*Cmi->Bytes(), *offset) == 0)
                {
                    continue;
                }

                if (auto animation = GetAnimation(*anyObject, *offset))
                {
                    animations.push_back(animation);
                }
            }
        }

        // Distinct (by reference, first appearance first).
        std::vector<std::shared_ptr<Formats::ModelAnimation>> distinctAnimations;
        std::unordered_set<const Formats::ModelAnimation*> seenAnimations;
        for (const auto& animation : animations)
        {
            if (seenAnimations.insert(animation.get()).second)
            {
                distinctAnimations.push_back(animation);
            }
        }

        auto models = LevelModels();
        for (const auto& a : Level->Arenas)
        {
            for (const auto& [name, model] : a->Models)
            {
                models.push_back(model);
            }
        }

        std::unordered_set<const Formats::Model*> seenModels;
        for (const auto& model : models)
        {
            if (!seenModels.insert(model.get()).second)
            {
                continue;
            }

            MdkObject::Prepare(model, nullptr);
            for (const auto& animation : distinctAnimations)
            {
                if (animation->Animates(*model))
                {
                    MdkObject::Prepare(model, animation);
                }
            }
        }
    }

    std::shared_ptr<Formats::ModelAnimation> ScriptRuntime::FindArenaAnimation(const std::string& arena, const std::string& name)
    {
        return Level->Mto->Has(arena) ? Level->Mto->GetArena(arena)->Animations.GetValueOrDefault(name) : nullptr;
    }

    std::shared_ptr<Formats::Model> ScriptRuntime::FindModel(const std::string& arena, const std::string& type)
    {
        auto model = Cmi->GetModel(type);
        if (model != nullptr)
        {
            return model;
        }

        return Level->Mto->Has(arena) ? Level->Mto->GetArena(arena)->Models.GetValueOrDefault(type) : nullptr;
    }

    std::shared_ptr<Formats::Texture> ScriptRuntime::FindTexture(const std::string& arena, const std::string& name)
    {
        std::shared_ptr<Formats::Texture> texture;
        if (Level->Mto->Has(arena) && Level->Mto->GetArena(arena)->Textures->Textures.TryGetValue(name, texture))
        {
            return texture;
        }

        return Level->LevelTextures->Textures.GetValueOrDefault(name);
    }

    bool ScriptRuntime::CanSeeKurt(MdkObject& obj, float range, float cone)
    {
        constexpr float MinCone = 10.0f;
        if (obj.DistanceTo(KurtPosition) > range)
        {
            return false;
        }

        if (cone < HalfTurn && MathF::Abs(WrapAngle(obj.YawTo(KurtPosition) - obj.Yaw)) > MathF::Max(cone, MinCone))
        {
            return false;
        }

        return !Raycast(obj.Position + Vector3(0.0f, 0.0f, EyeHeight), KurtPosition + Vector3(0.0f, 0.0f, KurtChest)).has_value();
    }

    void ScriptRuntime::PlaySound(MdkObject& obj, const std::string& name, int flags, const Operand& position)
    {
        constexpr int ModeMask = 3;
        constexpr int StopMode = 3;
        constexpr int Tracked = 4;
        constexpr int Following = 0x10;
        constexpr int AtReference = 0x20;
        constexpr int AtPoint = 0x40;
        constexpr int Flat = 0x80;
        const auto& starts = SoundStarts;
        auto mode = flags & ModeMask;
        if ((flags & Flat) != 0)
        {
            if (mode != StopMode)
            {
                Mixer->Play(name, starts[mode]);
            }

            return;
        }

        if (mode == StopMode)
        {
            Mixer->Stop(name);
            return;
        }

        const auto* list = std::get_if<OperandList>(&position);
        auto point = list != nullptr ? V(*list) : Vector3::Zero();
        int voice;
        if ((flags & Following) != 0)
        {
            voice = PlayOn(name, obj, starts[mode], point);
        }
        else if ((flags & AtReference) != 0)
        {
            voice = Mixer->PlayAt(name, obj.ReferencePoint(I(position)), starts[mode]);
        }
        else if ((flags & AtPoint) != 0)
        {
            voice = Mixer->PlayAt(name, point, starts[mode]);
        }
        else if ((flags & Tracked) != 0)
        {
            voice = PlayOn(name, obj, starts[mode], Vector3::Zero());
        }
        else
        {
            voice = Mixer->PlayAt(name, obj.Position, starts[mode]);
        }

        if ((flags & Tracked) != 0)
        {
            obj.TrackedSound = name;
            obj.TrackedVoice = voice;
        }
    }

    int ScriptRuntime::PlayOn(const std::string& name, MdkObject& obj, Audio::SoundMixer::Start start, Vector3 offset)
    {
        auto voice = Mixer->PlayOn(name, &obj, offset, start);
        auto* voices = _following.Find(&obj);
        if (voices == nullptr)
        {
            std::vector<int> list;
            if (!_spareVoiceLists.empty())
            {
                list = std::move(_spareVoiceLists.top());
                _spareVoiceLists.pop();
            }

            voices = &(_following[&obj] = std::move(list));
        }

        // Ended voices are forgotten.
        for (auto i = static_cast<int>(voices->size()) - 1; i >= 0; i--)
        {
            if (!Mixer->IsVoicePlaying((*voices)[i]))
            {
                voices->erase(voices->begin() + i);
            }
        }

        voices->push_back(voice);
        return voice;
    }
}
