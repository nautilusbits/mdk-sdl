using Android.App;
using Android.Content;
using Android.Content.PM;
using Build = Android.OS.Build;
using Mdk.Engine.Diagnostics;
using Mdk.Game;
using Mdk.Game.Flow;
using Org.Libsdl.App;

namespace Mdk.Android;

/// <summary>The app: SDL's activity (its Java side comes with ppy.SDL3-CS) running the game on
/// SDL's thread, from the main menu, with the files found on first start (<see cref="DataSetup"/>).
/// Settings and saves live in the MDK folder played from in place, else in the app's external
/// files folder.</summary>
[Activity(
    Label = "MDK",
    MainLauncher = true,
    Exported = true,
    Icon = "@drawable/icon",
    Theme = "@android:style/Theme.NoTitleBar.Fullscreen",
    LaunchMode = LaunchMode.SingleTask,
    ScreenOrientation = ScreenOrientation.SensorLandscape,
    ConfigurationChanges = ConfigChanges.Orientation | ConfigChanges.ScreenSize | ConfigChanges.SmallestScreenSize
        | ConfigChanges.ScreenLayout | ConfigChanges.Keyboard | ConfigChanges.KeyboardHidden | ConfigChanges.Navigation
        | ConfigChanges.UiMode | ConfigChanges.Locale | ConfigChanges.LayoutDirection | ConfigChanges.Density)]
public sealed class MainActivity : SDLActivity
{
    /// <summary>The level the menu's "new game" would start with otherwise (unused from the menu).</summary>
    private const int DefaultLevel = 7;
    /// <summary>The imported game files, under the app's files folder.</summary>
    private const string DataFolder = "mdk";
    /// <summary>The game's user folder (settings, saves): see Game.</summary>
    private const string UserFolderVariable = "MDK_USER_DIR";

    private DataSetup? _setup;

    protected override string[] GetLibraries() => ["SDL3"];

    /// <summary>SDL's thread: the files, then the game until it quits.</summary>
    protected override void Main()
    {
        // The log keeps what's printed from here: mdk.log gets it once the user folder is known.
        Log.Capture(Log.Lines);
        Console.WriteLine($"Device: {Build.Manufacturer} {Build.Model}, Android {Build.VERSION.Release} (API {(int)Build.VERSION.SdkInt})");
        var files = GetExternalFilesDir(null)?.AbsolutePath ?? FilesDir!.AbsolutePath;
        var setup = new DataSetup(this, Path.Combine(files, DataFolder), files);
        _setup = setup;
        Exception? stopped;
        try
        {
            // The setup's dialogs over the project's art, gone before the game draws.
            Backdrop.Show(this);
            var found = setup.Get();
            Backdrop.Hide(this);
            if (found == null)
            {
                return;
            }

            Environment.SetEnvironmentVariable(UserFolderVariable, found.UserFolder);
            stopped = Play(found, setup);
        }
        catch (Exception e)
        {
            Console.Error.WriteLine(e);
            stopped = e;
        }

        // No console on a phone: say why the game stopped (e.g. no Vulkan for SDL_GPU).
        if (stopped != null)
        {
            Dialogs.Tell(this, new Prompt("MDK stopped", $"{stopped.GetType().Name}: {stopped.Message}\n\nThe details are in mdk.log."));
        }
    }

    /// <summary>The game until it quits; an exception is logged (mdk.log still open) before the
    /// game is disposed, and returned.</summary>
    private Exception? Play(GameFiles found, DataSetup setup)
    {
        var game = new Mdk.Game.Flow.Game(found.Data, new GameOptions(StartScreen(), StartLevel()) { Splash = true, Import = setup.Reimport });
        try
        {
            game.Run();
            return null;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"MDK stopped: {e}");
            return e;
        }
        finally
        {
            game.Dispose();
        }
    }

#if DEBUG
    /// <summary>A debug build's intent extra "level" starts that level (the emulator's tests:
    /// <c>am start -p io.github.nemo22.mdk --ei level 3</c>).</summary>
    private const string LevelExtra = "level";

    private int AskedLevel => Intent?.GetIntExtra(LevelExtra, 0) ?? 0;
#else
    private static int AskedLevel => 0;
#endif

    private Start StartScreen() => AskedLevel > 0 ? Start.Level : Start.Menu;

    private ViewerOptions StartLevel() => new(AskedLevel > 0 ? AskedLevel : DefaultLevel, null, null, 0f, SoundMode.On);

    protected override void OnActivityResult(int requestCode, Result resultCode, Intent? data)
    {
        if (_setup?.OnResult(requestCode, resultCode, data) == true)
        {
            return;
        }

        base.OnActivityResult(requestCode, resultCode, data);
    }

    public override void OnRequestPermissionsResult(int requestCode, string[] permissions, Permission[] grantResults)
    {
        base.OnRequestPermissionsResult(requestCode, permissions, grantResults);
        _setup?.OnPermissions(requestCode);
    }
}
