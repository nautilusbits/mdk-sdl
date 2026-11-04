using Android.App;
using Android.Content;
using Android.Content.PM;
using Android.OS;
using Mdk.Engine.Diagnostics;

namespace Mdk.Android;

/// <summary>What the setup screen is shown for.</summary>
internal enum SetupMode { Start, Reimport, Stopped }

/// <summary>The launcher: the setup screen (<see cref="Backdrop"/> with <see cref="Dialogs"/>
/// beside it) while <see cref="DataSetup"/> finds the game's files on a worker thread, then the
/// game (<see cref="GameActivity"/>). The game shows it again, and waits on SDL's thread, for
/// Options' "Import from folder" and to say why it stopped.</summary>
[Activity(
    Label = "MDK",
    MainLauncher = true,
    Exported = true,
    Icon = "@drawable/icon",
    Theme = "@android:style/Theme.NoTitleBar.Fullscreen",
    ScreenOrientation = ScreenOrientation.SensorLandscape,
    ConfigurationChanges = ConfigChanges.Orientation | ConfigChanges.ScreenSize | ConfigChanges.SmallestScreenSize
        | ConfigChanges.ScreenLayout | ConfigChanges.Keyboard | ConfigChanges.KeyboardHidden | ConfigChanges.Navigation
        | ConfigChanges.UiMode | ConfigChanges.Locale | ConfigChanges.LayoutDirection | ConfigChanges.Density)]
public sealed class SetupActivity : Activity
{
    /// <summary>The imported game files, under the app's files folder.</summary>
    private const string DataFolder = "mdk";
    private const string ModeExtra = "mode";
    private const string InPlaceExtra = "inPlace";
    private const string MessageExtra = "message";
    /// <summary>The game's own action: the launcher's intent would only bring the task back.</summary>
    private const string GameAction = "io.github.nemo22.mdk.SETUP";
    private const string StoppedTitle = "MDK stopped";

    /// <summary>The game's wait for the screen it showed.</summary>
    private static TaskCompletionSource? _closed;

    private DataSetup? _setup;

    /// <summary>Options' "Import from folder" (SDL's thread): returns once it's done or cancelled.</summary>
    internal static void Reimport(string? inPlace) => Await(SetupMode.Reimport, InPlaceExtra, inPlace);

    /// <summary>Why the game stopped (SDL's thread): returns once it's read.</summary>
    internal static void Stopped(string message) => Await(SetupMode.Stopped, MessageExtra, message);

    /// <summary>The setup screen over the game, until it closes.</summary>
    private static void Await(SetupMode mode, string key, string? value)
    {
        var closed = new TaskCompletionSource();
        _closed = closed;
        var context = Application.Context;
        var intent = new Intent(context, typeof(SetupActivity)).SetAction(GameAction)!
            .AddFlags(ActivityFlags.NewTask | ActivityFlags.NoAnimation)!
            .PutExtra(ModeExtra, (int)mode)!
            .PutExtra(key, value)!;
        context.StartActivity(intent);
        closed.Task.Wait();
    }

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);

        // The log keeps what's printed from here: mdk.log gets it once the game starts.
        Log.Capture(Log.Lines);
        var files = GetExternalFilesDir(null)?.AbsolutePath ?? FilesDir!.AbsolutePath;
        var setup = new DataSetup(this, Path.Combine(files, DataFolder), files);
        _setup = setup;
        var mode = (SetupMode)(Intent?.GetIntExtra(ModeExtra, (int)SetupMode.Start) ?? (int)SetupMode.Start);
        Backdrop.Show(this);
        Task.Run(() => Run(setup, mode));
    }

    /// <summary>The worker thread: the mode's dialogs, then the game (first start) and close.</summary>
    private void Run(DataSetup setup, SetupMode mode)
    {
        try
        {
            Show(setup, mode);
        }
        catch (Exception e)
        {
            Console.Error.WriteLine(e);
            Dialogs.Tell(this, new Prompt(StoppedTitle, $"{e.GetType().Name}: {e.Message}"));
        }
        finally
        {
            RunOnUiThread(Finish);
            _closed?.TrySetResult();
        }
    }

    private void Show(DataSetup setup, SetupMode mode)
    {
        if (mode == SetupMode.Reimport)
        {
            setup.Reimport(Intent?.GetStringExtra(InPlaceExtra));
            return;
        }

        if (mode == SetupMode.Stopped)
        {
            Dialogs.Tell(this, new Prompt(StoppedTitle, Intent?.GetStringExtra(MessageExtra) ?? ""));
            return;
        }

        // Same task, no transition: one black screen into the game.
        if (setup.Get() is { } found)
        {
            RunOnUiThread(() => StartActivity(GameActivity.For(this, found, AskedLevel).AddFlags(ActivityFlags.NoAnimation)));
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
