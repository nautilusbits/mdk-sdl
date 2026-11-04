using System.Globalization;
using System.Runtime.InteropServices;
using Android.Content;
using Build = Android.OS.Build;
using Mdk.Engine.Diagnostics;
using Mdk.Formats;
using Mdk.Game;
using Mdk.Game.Flow;

namespace Mdk.Android;

/// <summary>The game: SDL's activity (Java/GameActivity.java: window, input, audio, the Vulkan
/// surface) runs <see cref="SdlMain"/> on SDL's thread with the files <see cref="SetupActivity"/>
/// found, from the main menu. Settings and saves live in the MDK folder played from in place,
/// else in the app's external files folder. Options' "Import from folder" and the message why
/// the game stopped show the setup screen over the game.
/// <code>
///   SetupActivity (C#) ─files─► GameActivity.java ─SDL_main─► Game.Run ─Import─► SetupActivity ─┐
///                                                                ▲    └stopped─► SetupActivity   │
///                                                                └───────────────────────────────┘
/// </code></summary>
internal static class GameActivity
{
    private const string ClassName = "io.github.nemo22.mdk.GameActivity";
    private const string DataExtra = "data";
    private const string UserExtra = "user";
    private const string LevelExtra = "level";
    /// <summary>SDL's argv ends with getArguments: the data folder, the user folder, the level.</summary>
    private const int SdlArgs = 3;
    /// <summary>The level the menu's "new game" would start with otherwise (unused from the menu).</summary>
    private const int DefaultLevel = 7;
    /// <summary>The game's user folder (settings, saves): see Game.</summary>
    private const string UserFolderVariable = "MDK_USER_DIR";

    /// <summary>The intent that plays <paramref name="files"/>, from <paramref name="level"/> (0: the menu).</summary>
    internal static Intent For(Context context, GameFiles files, int level) =>
        new Intent().SetClassName(context, ClassName)
            .PutExtra(DataExtra, files.Data.Dir)!
            .PutExtra(UserExtra, files.UserFolder)!
            .PutExtra(LevelExtra, level)!;

    /// <summary>SDL's entry: argv ends with GameActivity.java's getArguments.</summary>
    [UnmanagedCallersOnly(EntryPoint = "SDL_main")]
    private static unsafe int SdlMain(int argc, byte** argv)
    {
        // An exception here aborts the process without a log: a missing argument quits instead.
        if (argc < SdlArgs)
        {
            return 1;
        }

        var given = Enumerable.Range(0, argc).Select(i => Marshal.PtrToStringUTF8((nint)argv[i]) ?? "").ToArray();
        var level = int.TryParse(given[^1], CultureInfo.InvariantCulture, out var asked) ? asked : 0;
        Run(given[^3], given[^2], level);
        return 0;
    }

    /// <summary>The game until it quits; why it stopped, if it did, on the setup screen.</summary>
    private static void Run(string dataDir, string userFolder, int level)
    {
        // The log keeps what's printed from here: mdk.log gets it once the user folder is known.
        Log.Capture(Log.Lines);
        Exception? stopped;
        try
        {
            Console.WriteLine($"Device: {Build.Manufacturer} {Build.Model}, Android {Build.VERSION.Release} (API {(int)Build.VERSION.SdkInt})");
            var data = MdkData.At(dataDir) ?? throw new DirectoryNotFoundException($"No MDK game files in {dataDir}");
            Environment.SetEnvironmentVariable(UserFolderVariable, userFolder);
            stopped = Play(new GameFiles(data, userFolder), level);
        }
        catch (Exception e)
        {
            Console.Error.WriteLine(e);
            stopped = e;
        }

        // No console on a phone: say why the game stopped (e.g. no Vulkan for SDL_GPU).
        if (stopped != null)
        {
            SetupActivity.Stopped($"{stopped.GetType().Name}: {stopped.Message}\n\nThe details are in mdk.log.");
        }
    }

    /// <summary>The game until it quits; an exception is logged (mdk.log still open) before the
    /// game is disposed, and returned.</summary>
    private static Exception? Play(GameFiles found, int level)
    {
        // Played in place, the user folder is the MDK folder: Import picks another one.
        var inPlace = found.UserFolder == found.Data.Dir ? found.UserFolder : null;
        var options = new GameOptions(StartScreen(level), StartLevel(level)) { Splash = true, Import = () => SetupActivity.Reimport(inPlace) };
        var game = new Mdk.Game.Flow.Game(found.Data, options);
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

    private static Start StartScreen(int level) => level > 0 ? Start.Level : Start.Menu;

    private static ViewerOptions StartLevel(int level) => new(level > 0 ? level : DefaultLevel, null, null, 0f, SoundMode.On);
}
