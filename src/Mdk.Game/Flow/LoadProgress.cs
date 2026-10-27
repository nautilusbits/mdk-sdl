using System.Diagnostics;

namespace Mdk.Game.Flow;

/// <summary>How far a level's loading is (0 to 1), for the loading screen's bar: redrawn at most
/// every <see cref="Every"/>, as each redraw waits for the display.</summary>
public sealed class LoadProgress(Action<float> draw, Func<TimeSpan> clock)
{
    public static readonly TimeSpan Every = TimeSpan.FromMilliseconds(100);

    private TimeSpan? _drawn;

    /// <summary>Timed by the wall clock.</summary>
    public LoadProgress(Action<float> draw) : this(draw, Stopwatch.StartNew())
    {
    }

    private LoadProgress(Action<float> draw, Stopwatch watch) : this(draw, () => watch.Elapsed)
    {
    }

    public void Report(float done)
    {
        var now = clock();
        if (_drawn is { } drawn && now - drawn < Every)
        {
            return;
        }

        _drawn = now;
        draw(Math.Clamp(done, 0f, 1f));
    }
}
