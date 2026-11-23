#pragma once

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/Numerics.h"
#include "engine/render/Renderer.h"
#include "game/flow/SaveGames.h"
#include "game/flow/Screen.h"
#include "game/FrameBlend.h"
#include "game/FrameDump.h"
#include "game/hd_textures/HdImage.h"
#include "game/hud/HudView.h"
#include "game/mods/ModReport.h"
#include "game/objects/ObjectView.h"
#include "game/RideTest.h"
#include "game/scripts/StrikeScene.h"
#include "game/SoakTest.h"

namespace Mdk::Engine::Audio
{
    class AudioDevice;
}

namespace Mdk::Engine::Platform
{
    class Input;
}

namespace Mdk::Formats
{
    class BetaDemo;
    class Dti;
}

namespace Mdk::Game::Audio
{
    class LevelMusic;
    class SoundMixer;
}

namespace Mdk::Game::Collision
{
    class ArenaSpace;
}

namespace Mdk::Game::DevTools
{
    class ILevelTarget;
    class LevelCommands;
}

namespace Mdk::Game::Flow
{
    class Cheats;
    class GameState;
    class LoadProgress;
}

namespace Mdk::Game::Kurt
{
    class FollowCamera;
    class Kurt;
    class KurtSprite;
}

namespace Mdk::Game::Level
{
    class LevelData;
    class LevelLight;
    class LevelView;
    class MaterialResolver;
}

namespace Mdk::Game::Menu
{
    class PauseMenu;
    class SavePrompt;
    class Ui;
}

namespace Mdk::Game::Objects
{
    class EffectsView;
    class MdkObject;
    class RopeView;
}

namespace Mdk::Game::Scripts
{
    class GameStats;
    class ScriptRuntime;
}

namespace Mdk::Game
{
    class CutsceneCamera;
    class FreeCamera;
    class SniperTest;
    class SniperView;

    // A roll of the 1996 demo's levels.
    enum class BetaRoll { Left, Right };

    enum class SoundMode { On, Muted };

    // How a session starts, and what tests make it do: hold keys for some seconds, then save
    // a frame and quit.
    struct ViewerOptions
    {
        int Level = 0;
        std::optional<Vector3> Position;
        std::optional<float> Yaw;
        float Pitch = 0;
        SoundMode Sound{};

        // Save a frame after Wait seconds of game time, print Kurt's state, and quit.
        std::optional<std::string> Screenshot;
        float Wait = 0;
        // Hold "forward" this many seconds from the start.
        float Walk = 0;
        // The follow camera's look down (degrees; negative: up), as the mouse's.
        float Look = 0;
        // Seconds before the held keys start (like the Godot port's --delay).
        float Delay = 0;
        bool Jump = false;
        // Hold "fire".
        bool Fire = false;
        // Pickups Kurt starts with (SW_HBOMB...), and press "use" once he has landed.
        std::vector<std::string> Give;
        bool Use = false;
        bool Fly = false;
        // Print the scripted objects once per second of game time.
        bool Profile = false;
        // Print every frame where Kurt, the camera and the object carrying him are drawn.
        bool Trace = false;
        // Sniper mode once Kurt stands, at this zoom (X) and pitch (Y); see SniperTest.
        std::optional<Vector2> Sniper;
        // Hold "zoom in" this many seconds in sniper mode.
        float Zoom = 0;
        // Fire one sniper round once the clip is loaded.
        bool SniperFire = false;
        // Bones' full-screen strike after a second.
        std::optional<Scripts::StrikeScene::Plane> Strike;
        // Kurt is hurt to death after a second.
        bool Die = false;
        // A special_event after a second (1 ends the level).
        std::optional<int> Event;
        // After the delay: a teleport, an object killed, a walker ridden, the XE boarded (see RideTest).
        std::optional<TeleportTarget> Teleport;
        std::optional<std::string> Kill;
        std::optional<std::string> Ride;
        std::optional<BomberTest> Bomber;
        // At the screenshot, a full save of this name (as F2), its hash printed.
        std::optional<std::string> Snapshot;
        // The soak test's seed: random keys (SoakKeys) and checks (SoakTest) until the screenshot.
        std::optional<int> Soak;
        SoakRoute Route{};
        // After the delay: one of the 1996 demo's teleports, a roll held (tests).
        std::optional<int> BetaTeleport;
        std::optional<BetaRoll> Roll;
        // After the delay: the console opens and runs these commands ("pos;god").
        std::optional<std::string> Console;
        // After the wait: frames saved, then the game quits (--frames).
        std::optional<FrameDump> Frames;
        // Test frames a second (--fps): above 60, frames are drawn between the steps.
        std::optional<int> FrameRate;

        bool operator==(const ViewerOptions&) const = default;
    };

    // Plays a level: Kurt walks it (F1 switches to a flying camera), the scripts run. Esc
    // opens the pause menu. It ends when Kurt dies, the level ends (after the tornado) or the game ends.
    //   input ──► Kurt (60 steps/s) ──► scripts (30 ticks/s) ──► camera ──► level, objects, sky, Kurt ──► frame
    //                └── mixer (listener at the camera)
    class Viewer : public Flow::IScreen
    {
    public:
        // Kurt moves in fixed steps; the tests' frames are steps too.
        static constexpr float Step = 1.f / 60.f;

        Viewer(Menu::Ui& ui, const ViewerOptions& options, Flow::GameState& state, Flow::LoadProgress& progress);
        ~Viewer() override;

        // The console's commands on the level.
        std::shared_ptr<DevTools::ILevelTarget> Commands() const;

        // The tests' --console runs after the delay, once the scripts have set Kurt's arena.
        bool ConsoleReady() const;

        // The debug overlay's lines: the pause menu or F2's prompt, Kurt and the level.
        std::vector<std::string> Status() const override;

        Engine::Platform::Typing Typing() const override;

        // The counts of the level (for the statistics).
        std::shared_ptr<Scripts::GameStats> Stats() const;

        // Kurt's health when the level ended (the save after LEVEL8 keeps it).
        int Health() const;

        Flow::Event Frame(float elapsed, const std::optional<std::string>& screenshot) override;

        void Dispose() override;

    private:
        // Steps caught up at most after a slow frame.
        static constexpr int MaxSteps = 10;
        // The start is slightly below the landing pad (the original lands Kurt by chute).
        static constexpr float StartDrop = 3.f;
        // A pickup's name stays this long.
        static constexpr float PickupMessageSeconds = 2.f;
        // Quick save and load messages ("Game saved") stay this long.
        static constexpr float NoticeSeconds = 2.f;
        // The test's "use" is held from 1 second on, for 0.1 (game time).
        static constexpr float UseStart = 1.f;
        static constexpr float UseTime = 0.1f;
        // The tests' --die and --event happen after 1 second (game time).
        static constexpr float TestStart = 1.f;
        static constexpr int FatalDamage = 1000;
        // The game goes on this long after the level ended, longer when it's lost (main.gd).
        static constexpr float EndDelay = 0.5f;
        static constexpr float GameOverDelay = 5.f;

        // How far the loading is after each part: its share of the time on a desktop, the
        // GPU's uploads of the part (at the next frame of the loading screen) included.
        struct Loaded
        {
            static constexpr float Data = 0.06f;
            static constexpr float Geometry = 0.46f;
            static constexpr float Light = 0.55f;
            static constexpr float Kurt = 0.86f;
            static constexpr float Scripts = 0.89f;
            static constexpr float All = 1.f;
        };

        // The level ended: the event that follows once the delay is over.
        struct Ending
        {
            Flow::Event Event{};
            float Delay = 0;
        };

        // The camera's axes and field of view outside sniper mode.
        struct EyeAxes
        {
            Vector3 Forward;
            Vector3 Up;
            Vector3 Right;
            float FieldOfView = 0;
        };

        using Looks = std::unordered_map<std::string, Objects::ObjectView::Look>;

        Menu::Ui& _ui;
        ViewerOptions _options;
        Flow::GameState& _state;
        Engine::Render::Renderer& _renderer;
        Engine::Audio::AudioDevice& _audio;
        std::shared_ptr<Level::LevelData> _level;
        std::shared_ptr<Level::LevelView> _view;
        std::shared_ptr<Audio::SoundMixer> _mixer;
        std::shared_ptr<Audio::LevelMusic> _music;
        std::shared_ptr<Collision::ArenaSpace> _space;
        std::shared_ptr<Kurt::KurtSprite> _sprite;
        std::shared_ptr<Kurt::Kurt> _kurt;
        std::shared_ptr<Scripts::ScriptRuntime> _scripts;
        std::shared_ptr<Hud::HudView> _hud;
        std::shared_ptr<Objects::ObjectView> _objects;
        std::shared_ptr<Objects::EffectsView> _effects;
        std::shared_ptr<Objects::RopeView> _ropes;
        Looks _looks;
        // The HUD's inventory slots, filled again each time (kept: no list per frame).
        std::vector<Hud::HudSlot> _hudSlots;
        std::shared_ptr<SniperView> _sniper;
        std::shared_ptr<SniperTest> _sniperTest;
        std::shared_ptr<RideTest> _rideTest;
        std::shared_ptr<SoakTest> _soak;
        // F2's name prompt, while it's open.
        std::shared_ptr<Menu::SavePrompt> _snapshotPrompt;
        std::shared_ptr<Kurt::FollowCamera> _follow;
        std::shared_ptr<CutsceneCamera> _cutscene;
        std::shared_ptr<FreeCamera> _fly;
        std::shared_ptr<Menu::PauseMenu> _pause;
        // The 1996 demo, in its levels.
        std::shared_ptr<Formats::BetaDemo> _beta;
        bool _test = false;
        // Frames between the steps (not the tests': their frames are steps).
        std::shared_ptr<FrameBlend> _blend;
        Blending _blending{};
        bool _flying = false;
        float _time = 0;
        float _pending = 0;
        float _strikeTime = 0;
        // Frames drawn since --frames' wait.
        int _frameStep = 0;
        std::shared_ptr<Flow::Cheats> _cheats;
        Flow::Event _next = Flow::Event::None;
        // The level ended: the event that follows once the delay is over.
        std::optional<Ending> _ending;
        std::shared_ptr<DevTools::LevelCommands> _commands;
        // The enhanced look's light; null in the original look.
        std::shared_ptr<Level::LevelLight> _light;
        // What the level took from the mods (the enhanced look's).
        Mods::ModReport _mods = Mods::ModReport::None();
        // Whether the load's garbage was collected (Settle).
        bool _settled = false;
        // The arenas' camera pitches, found once.
        std::unordered_map<std::string, float> _pitches;

        // The console's map: Kurt starts on a floor of the arena asked for.
        void EnterStartArena(Flow::GameState& state);

        // The console's save: a full save of that name, as F2 makes.
        bool SaveSlot(const std::string& name);

        // Kurt keeps the health and the pickups of the fall (taken again, without their messages).
        void CarryFromFall(Flow::GameState& state);

        // After the first frame (the HD images are on the GPU, the renderer dropped them):
        // one compacting collection gives the load's large arrays back at once, not over play's
        // collections (Android: 36 full ones, a 627 MB heap).
        void Settle();

        // The next full collection compacts the large object heap too. Mono (Android)
        // has no such mode: its setter throws, and its collector compacts as it sees fit.
        static void CompactLargeObjects();

        // Kurt in fixed steps; the tests' keys by game time.
        void RunSteps(Engine::Platform::Input& input, float elapsed);

        // A step of Kurt, or of the flying camera (timed as physics).
        void UpdateKurt(Engine::Platform::Input& input);

        // A step of Kurt's camera, unless the flying one is on (timed as physics).
        void UpdateCamera(Engine::Platform::Input& input);

        // The mouse turns Kurt and tilts the camera every frame, not with the steps (screens
        // above 60 Hz); not the flying camera's, nor in tests (they have no mouse).
        void MouseLook(Engine::Platform::Input& input);

        // What carries Kurt, drawn with him: the XE he flies, the platform he stands on.
        std::shared_ptr<Objects::MdkObject> Carrier() const;

        // What Kurt rides that follows him each step: the XD2, the board.
        std::shared_ptr<Objects::MdkObject> Driven() const;

        // This frame between the last step and the next: Kurt, the objects and the cameras
        // as drawn.
        void BlendFrame();

        // The tests' held keys: forward for --walk seconds, jump, fire, and use at 1 second.
        void HoldTestKeys(Engine::Platform::Input& input);

        // The 1996 demo's teleports (BetaDemo.LoadTeleports), on a digit while T or Alt is held (the
        // digits alone pick items): the lines of this level's arenas, in order. The city has no
        // other way up to the top of ARENA_4.
        void BetaTeleport(int index);

        // The tests' --die (Kurt is hurt to death) and --event (a special_event: 1 ends the level).
        void RunTests();

        // The camera, the scene, the HUD and the sounds' listener.
        Engine::Render::View DrawScene(float elapsed);

        Engine::Render::View SceneView();

        // Sniper mode's view as drawn: from Kurt between steps, the scope's tilt and zoom too.
        Engine::Render::View ScopeView(float aspect);

        // The camera's axes and field of view outside sniper mode: flying, a cutscene's or Kurt's.
        EyeAxes Eye() const;

        // The pitch the follow camera eases to: camera_track's (opcode 203) while a script
        // asks for it, else the arena's.
        float PitchGoal();

        // A full save being loaded: the level and Kurt as they were.
        void RestoreSnapshot(Flow::GameState& state);

        // F2 (0x42b520(0)): the game stops and the name is asked, the level's number offered.
        void OpenSnapshot();

        // A frame of F2's prompt (on black); Enter saves the level as it was, Esc gives up.
        Flow::Event AskSnapshot(float elapsed, const std::optional<std::string>& screenshot);

        // Quick load: this session's last full save, else the newest save; the level
        // reloads and says so (GameState.Notice).
        void QuickLoad();

        // Whether quick load has a save to load (the pause menu's Quick load).
        bool CanQuickLoad() const;

        // A message of the port's own on the HUD (zooming, ahead of the others), also printed.
        void Notify(const std::string& text);

        // The message the last level left for this one ("Game loaded").
        void ShowNotice(Flow::GameState& state);

        // The tests' full save (--snapshot): written at once, its hash printed.
        void SaveSnapshot(const std::string& name);

        Flow::SaveGame SnapshotSave(const std::string& json);

        // The mouse looks around again once the pause menu closes (not under F2's prompt,
        // which the menu's Quick save opens).
        void CaptureMouse();

        // Letters typed in a level: TOOSCARYFORME turns gore on or off (not saved),
        // SEETHEWHOLEGAME the main menu's debug keys (0x5742bc).
        void TypeCheats(const std::string& typed);

        // Kurt died (damp_control 0x466b40): the death is counted and the game goes back to the
        // main menu, whose "Continue" starts the level again.
        void OnDied();

        // What the HUD shows of Kurt and the object he shoots at.
        static Hud::HudState HudStateOf(Kurt::Kurt& kurt, Scripts::ScriptRuntime& scripts, std::vector<Hud::HudSlot>& slots);

        // Lays out the models before they're drawn: each arena's own, and those of the objects
        // there at the start. (Every level model in every arena would make the enhanced look's colour
        // textures for every palette: hundreds of MB.)
        void PreloadModels(Level::LevelData& level);

        // Every texture of the level with each arena's palette (and the level's), on the GPU.
        static void Preload(Level::MaterialResolver& surfaces, Level::LevelData& level);

        // The visible objects, each with its arena's palette, textures and depth band.
        static void DrawObjects(Engine::Render::Renderer& renderer, Objects::ObjectView& view, Scripts::ScriptRuntime& scripts,
            Level::LevelData& level, Looks& looks, FrameBlend& blend, const std::string& camera);

        static bool IsBoard(Scripts::ScriptRuntime& scripts, const Objects::MdkObject& obj);

        // The ridden board, in front of the floors it stands above (FloorPull).
        void DrawBoard(Vector3 eye, Vector3 forward);

        // An arena's palette and textures for its objects (the level's outside the arenas).
        static Objects::ObjectView::Look ObjectLook(Level::LevelData& level, Looks& looks, const std::string& name);

        // For tests: the objects of Kurt's arena (like the Godot port's --profile).
        static void Profile(Scripts::ScriptRuntime& scripts, float time);

        // For tests: where this frame draws Kurt, the camera and the platform or ride carrying him.
        void Trace(Vector3 camera);

        // For tests: in sniper mode, the scope and the round cameras as drawn ("blend scope t
        // scope zoom pitch clip", "blend round t slot x y z").
        void TraceScope();

        // For tests: each flying piece and sprite effect as drawn ("blend piece t id x y z",
        // "blend effect t id x y z").
        void TraceEffects();

        // For tests: the middle of each animated object's pose in Kurt's arena as drawn
        // ("blend pose t TYPE_ID x y z", model space).
        void TracePoses();

        // A music volume (0-0x7FFF) in whole decibels, as the Godot port's profile prints it.
        static int Decibels(float volume);

        // A coordinate rounded like Godot's Vector3.round().
        static std::string Rounded(float value);

        // The test's screenshot once its wait is over, or F12's.
        static std::optional<std::string> SavePath(const ViewerOptions& options, Engine::Platform::Input& input, float time);

        // --frames' file for this frame, once the wait is over.
        std::optional<std::string> FramePath();

        // For tests: where Kurt is and what he does.
        static void Report(Kurt::Kurt& kurt, Collision::ArenaSpace& space);

        // The camera pitch of Kurt's arena (DTI, degrees; camera_update 0x4174d0).
        float ArenaPitch(const std::string& name);

        // The level's sky and the panorama its mirrors show, through the level's palette.
        static Engine::Render::Panorama CreatePanorama(Engine::Render::Renderer& renderer, const Formats::Dti& dti,
            const std::optional<HdTextures::HdImage>& hdSky);

        // The background the scripts want (sky, black or the last frame) and its clear colour.
        Vector4 Background();

        // The colour above the sky panorama.
        static Vector4 SkyColour(const Formats::Dti& dti);
    };
}
