using Mdk.Engine.Platform;

namespace Mdk.Game.Flow;

/// <summary>What a screen asks the game to do next (see <see cref="Game"/>).</summary>
public enum Event
{
    /// <summary>The screen goes on.</summary>
    None,
    Quit,
    /// <summary>The main menu (with the splash when <see cref="GameState.Splash"/>).</summary>
    Menu,
    /// <summary>Load and play <see cref="GameState.Level"/>.</summary>
    Play,
    /// <summary>The briefing of <see cref="GameState.Level"/>, then the level (a new game, a save made before a level).</summary>
    Briefing,
    /// <summary>The screens after the level <see cref="GameState.Level"/> (statistics, save prompt, next briefing).</summary>
    Statistics,
    /// <summary>The level ended: the tornado is over.</summary>
    LevelEnded,
    /// <summary>The level ended badly (the town flattened).</summary>
    GameOver,
    KurtDied,
    /// <summary>The end of the game (event 81): the end movies.</summary>
    GameFinished,
    /// <summary>The fall before <see cref="GameState.Level"/>, then the level.</summary>
    Fall,
    /// <summary>The stream after the level <see cref="GameState.Level"/>.</summary>
    Stream,
    /// <summary>The stream is over: the statistics, or the last level after LEVEL8.</summary>
    StreamEnded,
}

/// <summary>A screen of the game: the menu, the loading screen, a level, the statistics...</summary>
public interface IScreen : IDisposable
{
    /// <summary>One frame of <paramref name="elapsed"/> seconds: input, update, draw and present (saving
    /// it to <paramref name="screenshot"/> when given).</summary>
    Event Frame(float elapsed, string? screenshot);

    /// <summary>The debug overlay's lines about the screen: its name, its state.</summary>
    IReadOnlyList<string> Status { get; }

    /// <summary>A touch screen's keyboard: shown while the screen reads typed text (a save's name).</summary>
    Typing Typing => Typing.Off;
}
