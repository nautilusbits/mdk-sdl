using Mdk.Game.Flow;

namespace Mdk.Game.Tests;

/// <summary>The loading screen's bar: redrawn as the level loads, not on every step.</summary>
public class LoadProgressTests
{
    [Fact]
    public void RedrawsAtMostEveryTenthOfASecond()
    {
        var now = TimeSpan.Zero;
        var drawn = new List<float>();
        var progress = new LoadProgress(drawn.Add, () => now);

        progress.Report(0.1f);
        now += TimeSpan.FromMilliseconds(50);
        progress.Report(0.2f);
        now += TimeSpan.FromMilliseconds(60);
        progress.Report(0.3f);

        Assert.Equal([0.1f, 0.3f], drawn);
    }
}
