using System.Globalization;
using Android.App;
using Android.Content;
using Android.Provider;
using Mdk.Formats;
using Mdk.Game.Flow;
using Mdk.Game.HdTextures;
using Mdk.Game.Mods;
using Uri = Android.Net.Uri;

namespace Mdk.Android;

/// <summary>The game's files and the user folder (settings, saves, mods, mdk.log).</summary>
internal sealed record GameFiles(MdkData Data, string UserFolder);

/// <summary>The game's files on first start. With All files access the user picks the MDK folder
/// copied to the phone and the game plays from it in place, keeping settings, saves and mods
/// there (<see cref="StorageChoice"/> remembers it, each start checks it). Without (declined, a
/// store build) its game folders are copied into the app's own folder once, and its <c>mods</c>
/// (HD textures made on a PC among them) into the user folder. An install that copied before
/// keeps its copy. Options, "Import from folder" picks it again: in place, another folder (or,
/// from the copy, the folder in place); copied, what's new or changed. Runs on the setup
/// screen's worker thread; dialogs show on the UI thread.
/// <code>
///   chosen folder readable? ─yes─► MdkData in place
///      └gone─► "can't read it" ─► forget ─┐
///   copied before? ─yes─► MdkData in the app ◄┘
///      └no─► "play in place?" ─allow─► All files access ─► folder picker ─► path, MDK, writable? ─► remember
///              └copy instead, refused, store build ─► folder picker ─► MDK in it? ─► copy (progress)
/// </code></summary>
internal sealed class DataSetup(Activity activity, string dir, string userFolder)
{
    /// <summary>The folder picker's request code ("MD").</summary>
    public const int PickRequest = 0x4d44;
    /// <summary>The progress shows every this many files.</summary>
    private const int ProgressStep = 20;
    private static readonly Prompt Intro = new("Copy the game files", "Copy your MDK installation (GOG, Steam or CD: the folder with TRAVERSE, MISC, FALL3D and STREAM) to this device, then pick that folder.\n\nIts game files (about 170 MB) are copied into the app once.");
    private static readonly Prompt NotMdk = new("Not an MDK folder", "That folder has no MDK game files (TRAVERSE/TRAVSPRT.BNI). Pick the MDK folder.");
    private const string CopyFailed = "Copying failed";
    private static readonly Prompt Again = new("Import from folder", "Pick your MDK folder again: new or changed game files are copied, and its mods (HD textures made on a PC among them).");
    private static readonly Prompt AccessIntro = new("Play from your MDK folder?", "MDK can play straight from your MDK installation (GOG, Steam or CD: the folder with TRAVERSE, MISC, FALL3D and STREAM) copied to this device, keeping settings, saves and mods there.\n\nThat needs All files access: switch it on for MDK on the next screen, then come back.\n\nOr copy the game files (about 170 MB) into the app instead.");
    private static readonly Prompt PickIntro = new("Pick your MDK folder", "Pick the MDK folder on this device's storage (e.g. MDK on the internal storage). The game reads it in place and keeps its settings, saves and mods there.");
    private static readonly Prompt PickAgain = new("Pick your MDK folder", "Pick the MDK folder to play from. Settings and saves are copied there; the game reads it from the next start.");
    private static readonly Prompt Switch = new("Import from folder", "Play from your MDK folder in place (All files access; settings and saves are copied there, from the next start), or copy new or changed game files into the app again?");
    private static readonly Prompt NoAccess = new("No All files access", "MDK can't read the folder in place without All files access.");
    private static readonly Prompt NoPath = new("Not on this device", "That folder isn't on this device's storage (a cloud, the Downloads list?).\n\nIn the picker's menu, show the internal storage, then pick your MDK folder.");
    private static readonly Prompt NotWritable = new("Folder is read-only", "MDK can't write to that folder (settings and saves go there). Pick a folder on the internal storage.");
    private static readonly Prompt Lost = new("MDK folder not found", "MDK can't read {0} any more (moved, deleted, or All files access switched off). Saves there stay there; pick the folder again to play from it.");
    private static readonly Prompt NextStart = new("Folder changed", "MDK plays from {0} from the next start; your settings and saves were copied there.");
    private static readonly Prompt Imported = new("Import done", "HD textures apply from the next level.");
    /// <summary>The primary storage's usual root, when the app's folder doesn't tell.</summary>
    private const string DefaultPrimary = "/storage/emulated/0";

    private readonly AllFilesAccess _access = new(activity);
    private readonly StorageChoice _choice = new(userFolder);
    private TaskCompletionSource<Uri?>? _picked;

    /// <summary>The installation: the chosen folder, the copy, or asked for; null when the user quits.</summary>
    public GameFiles? Get()
    {
        var plan = _choice.Plan(_access.Current, MdkData.Imported(dir));
        if (plan.Kind == StorageKind.Lost)
        {
            Dialogs.Tell(activity, Lost with { Text = string.Format(CultureInfo.InvariantCulture, Lost.Text, _choice.Folder) });
            _choice.Forget();
            plan = _choice.Plan(_access.Current, MdkData.Imported(dir));
        }

        if (plan.Data is { } data)
        {
            return new GameFiles(data, plan.Kind == StorageKind.InPlace ? data.Dir : userFolder);
        }

        // Declined, refused or backed out of: the copy, as before.
        if (plan.Kind == StorageKind.AskAccess
            && Dialogs.Ask(activity, AccessIntro, "Allow", "Copy instead") == Answer.Yes
            && ChooseInPlace(PickIntro, "Copy instead") is { } chosen)
        {
            return new GameFiles(chosen, chosen.Dir);
        }

        return PickAndCopy(Intro, "Quit") is { } copy ? new GameFiles(copy, userFolder) : null;
    }

    /// <summary>Options' "Import from folder". In place: another folder, from the next start.
    /// Copied: the folder in place from the next start (not on a store build), or picked and
    /// copied again. The game goes on with the files it has when the user cancels.
    /// <paramref name="inPlace"/>: the folder the game plays from in place, or null (the copy).</summary>
    public void Reimport(string? inPlace)
    {
        if (inPlace is { } current)
        {
            MoveTo(current, PickAgain);
            return;
        }

        if (_access.Current != FolderAccess.Unavailable && Dialogs.Ask(activity, Switch, "Play in place", "Copy again") == Answer.Yes)
        {
            MoveTo(userFolder, PickAgain);
            return;
        }

        if (PickAndCopy(Again, "Cancel") != null)
        {
            Dialogs.Tell(activity, Imported);
        }
    }

    /// <summary>A folder to play from in place from the next start; settings and saves follow.</summary>
    private void MoveTo(string current, Prompt intro)
    {
        if (ChooseInPlace(intro, "Cancel") is not { } chosen || chosen.Dir == current)
        {
            return;
        }

        StorageChoice.CopyUserFiles(current, chosen.Dir);
        Dialogs.Tell(activity, NextStart with { Text = string.Format(CultureInfo.InvariantCulture, NextStart.Text, chosen.Dir) });
    }

    /// <summary>All files access, then the MDK folder until one has the game and can be written
    /// (remembered: its data) or the user refuses (null).</summary>
    private MdkData? ChooseInPlace(Prompt intro, string no)
    {
        if (!_access.Request())
        {
            Dialogs.Tell(activity, NoAccess);
            return null;
        }

        var primary = TreePaths.PrimaryRoot(userFolder) ?? DefaultPrimary;
        var message = intro;
        while (Dialogs.Ask(activity, message, "Pick folder", no) == Answer.Yes)
        {
            if (Pick() is not { } uri)
            {
                message = intro;
                continue;
            }

            if (PathOf(uri, primary) is not { } path)
            {
                message = NoPath;
                continue;
            }

            if (MdkData.At(path) is not { } data)
            {
                message = NotMdk;
                continue;
            }

            if (!StorageChoice.IsWritable(data.Dir))
            {
                message = NotWritable;
                continue;
            }

            // An older build's textures-hd there moves into its mod, as a copy's does.
            HdGenerator.Migrate(data.Dir);
            _choice.Choose(data.Dir);
            return data;
        }

        return null;
    }

    /// <summary>The picked tree's file path (<see cref="TreePaths"/>), or null.</summary>
    private static string? PathOf(Uri tree, string primary)
    {
        if (tree.Authority is not { } authority || DocumentsContract.GetTreeDocumentId(tree) is not { } document)
        {
            return null;
        }

        return TreePaths.Resolve(authority, document, primary);
    }

    /// <summary>Asks for the MDK folder until one is copied (its data) or the user refuses (null).</summary>
    private MdkData? PickAndCopy(Prompt intro, string no)
    {
        var message = intro;
        while (Dialogs.Ask(activity, message, "Pick folder", no) == Answer.Yes)
        {
            if (Pick() is not { } uri)
            {
                message = intro;
                continue;
            }

            var tree = new DocumentTree(activity.ContentResolver!, uri);
            if (MdkData.FindIn(tree) is not { } root)
            {
                message = NotMdk;
                continue;
            }

            try
            {
                return Copy(tree, root);
            }
            catch (IOException e)
            {
                message = new Prompt(CopyFailed, e.Message);
            }
            catch (Java.Lang.Exception e)
            {
                message = new Prompt(CopyFailed, e.Message ?? e.ToString());
            }
        }

        return null;
    }

    /// <summary>The folder picker's answer, the All files access screen's or the storage
    /// permission's (the activity passes them on). Returns whether it was ours.</summary>
    public bool OnResult(int request, Result result, Intent? data)
    {
        if (_access.OnAnswer(request))
        {
            return true;
        }

        if (request != PickRequest)
        {
            return false;
        }

        _picked?.TrySetResult(result == Result.Ok ? data?.Data : null);
        return true;
    }

    /// <summary>The storage permission's answer (the activity passes it on). Returns whether it was ours.</summary>
    public bool OnPermissions(int request) => _access.OnAnswer(request);

    /// <summary>Android's folder picker; null when the user backs out.</summary>
    private Uri? Pick()
    {
        _picked = new TaskCompletionSource<Uri?>();
        activity.RunOnUiThread(() => activity.StartActivityForResult(new Intent(Intent.ActionOpenDocumentTree), PickRequest));
        return _picked.Task.Result;
    }

    /// <summary>Copies the installation and its mods (HD textures made on a PC among them; an older
    /// build's textures-hd moved into its mod), showing the progress.</summary>
    private MdkData Copy(DocumentTree tree, string root)
    {
        var progress = Dialogs.Show(activity, "Copying the game files", "Reading the folder…");
        Action<int, int> Progress(string what) => (done, total) =>
        {
            if (done % ProgressStep == 0 || done == total)
            {
                progress.Update(what, done, total);
            }
        };

        try
        {
            var data = MdkData.Import(tree, root, dir, Progress("Game files"));
            MdkData.CopyFolder(tree, root, ModCatalog.FolderName, ModCatalog.FolderIn(userFolder), Progress("Mods"));
            MdkData.CopyFolder(tree, root, HdGenerator.OldFolder, Path.Combine(userFolder, HdGenerator.OldFolder), Progress("HD textures"));
            HdGenerator.Migrate(userFolder);
            return data;
        }
        finally
        {
            progress.Close();
        }
    }
}
