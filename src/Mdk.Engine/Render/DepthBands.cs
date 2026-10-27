namespace Mdk.Engine.Render;

/// <summary>A draw's share of the depth range: the front's draws cover the back's whatever their
/// depth. The original has no depth buffer: it draws the other arena first and the camera's over
/// it (0x41df35), so the camera's arena (front) covers the other one (back).</summary>
public enum DepthBand { Front, Back }

/// <summary>The bands' depth ranges, set as the viewport's (the depth tested within a band). The
/// screen passes map them back to the whole range (screen.hlsli's BAND_SPLIT).
/// <code>
///   0 ──── front ──── Split ──── back ──── 1
/// </code></summary>
internal static class DepthBands
{
    public const float Split = 0.5f;

    public static (float Min, float Max) Of(DepthBand band) => band == DepthBand.Front ? (0f, Split) : (Split, 1f);
}
