using Mdk.Engine.Render;

namespace Mdk.Game.Level;

/// <summary>An arena's depth band: the camera's arena, its objects and effects in front, the other
/// drawn arena's behind (the original draws the camera's over the other, 0x41df35). Kurt's sprite
/// and board are drawn in front.</summary>
public static class ArenaBand
{
    public static DepthBand Of(string arena, string camera) => arena == camera ? DepthBand.Front : DepthBand.Back;
}
