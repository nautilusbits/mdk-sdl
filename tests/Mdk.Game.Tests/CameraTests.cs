using System.Numerics;
using Mdk.Engine.Audio;
using Mdk.Engine.Platform;
using Mdk.Game.Audio;
using Mdk.Game.Collision;
using Mdk.Game.Kurt;

namespace Mdk.Game.Tests;

/// <summary>The follow camera's pitch (camera_update 0x4174d0): the climb term −40 × the smoothed
/// rise of Kurt's feet per tick (0x490db8).</summary>
public class CameraTests
{
    private const float Step = 1f / 60f;
    private const float ArenaPitch = 4f;
    /// <summary>The rise per tick is clamped to 0.5: the term tops out at −20°.</summary>
    private const float MaxClimbPitch = -20f;
    /// <summary>Mouse motion per step far past the camera's pitch limits.</summary>
    private const float FullLook = 1000f;
    /// <summary>The view's least share along Kurt's facing: about 1° short of straight down.</summary>
    private const float MinAhead = 0.01f;
    /// <summary>The mouse's look stops here: further down Kurt's sprite lies flat under the camera,
    /// further up the camera sinks towards his feet.</summary>
    private const float LookDownLimit = 60f;
    private const float LookUpLimit = -30f;

    private static readonly AudioDevice Device = new(Output.Muted);

    /// <summary>Kurt standing on a floor that rises <paramref name="risePerTick"/> each tick.</summary>
    private static float PitchAfter(float risePerTick, float seconds) => CameraAfter(risePerTick, seconds).Pitch;

    /// <summary>The camera, and its pitch, after Kurt's floor rose and the mouse moved down
    /// <paramref name="mouseDown"/> each step.</summary>
    private static (FollowCamera Camera, Kurt.Kurt Kurt, float Pitch) CameraAfter(float risePerTick, float seconds, float mouseDown = 0f)
    {
        var kurt = new Kurt.Kurt(new ArenaSpace(), new SoundMixer(Device, _ => null), _ => 1) { OnFloor = true };
        var camera = new FollowCamera();
        var input = new Input();
        for (var t = 0f; t < seconds; t += Step)
        {
            kurt.Feet += new Vector3(0f, 0f, risePerTick * Kurt.Kurt.Ticks * Step);
            input.ClearMouse();
            input.AddMouseMotion(0f, mouseDown);
            camera.Update(kurt, ArenaPitch, input, Step);
        }

        // Positive pitch looks down.
        return (camera, kurt, -float.RadiansToDegrees(MathF.Asin(camera.Forward.Z)));
    }

    [Fact]
    public void StandingStillKeepsTheArenaPitch()
    {
        Assert.Equal(ArenaPitch, PitchAfter(0f, 3f), 2);
    }

    [Fact]
    public void ClimbingTiltsTheViewUp()
    {
        // Rising 1 unit a tick (clamped to 0.5) for 5 s: the smoothed rise nears 0.5.
        Assert.Equal(ArenaPitch + MaxClimbPitch, PitchAfter(1f, 5f), 0);
    }

    [Fact]
    public void FallingTiltsTheViewDown()
    {
        var pitch = PitchAfter(-0.25f, 5f);
        Assert.InRange(pitch, ArenaPitch + 9f, ArenaPitch + 10.5f);
    }

    [Theory]
    [InlineData(0f, FullLook)]
    [InlineData(0f, -FullLook)]
    [InlineData(-0.25f, FullLook)]
    public void LookingFullyUpOrDownStaysUpright(float risePerTick, float mouseDown)
    {
        // The mouse pushed past the limit (and falling tilting further down) never turns the view
        // over: it still looks a little the way Kurt faces (issue #11).
        var (camera, kurt, _) = CameraAfter(risePerTick, 3f, mouseDown);
        var ahead = Vector3.Dot(camera.Forward, kurt.Facing);
        Assert.True(ahead > MinAhead, $"forward {camera.Forward}, facing {kurt.Facing}");
    }

    [Theory]
    [InlineData(FullLook)]
    [InlineData(-FullLook)]
    public void FramesBetweenStepsStayUprightLookingFullyUpOrDown(float mouseDown)
    {
        // The frames drawn between steps (above 60 Hz) place the camera again: looking all the way
        // down they turned the view over, and Kurt jumped from the middle to the top of the screen.
        var (camera, kurt, _) = CameraAfter(0f, 3f, mouseDown);
        camera.Blend(kurt, kurt.Feet, kurt.Yaw, 1f);
        var ahead = Vector3.Dot(camera.Forward, kurt.Facing);
        Assert.True(ahead > MinAhead, $"forward {camera.Forward}, facing {kurt.Facing}");
    }

    [Theory]
    [InlineData(0f, FullLook, LookDownLimit)]
    [InlineData(-0.25f, FullLook, LookDownLimit)]
    [InlineData(0f, -FullLook, LookUpLimit)]
    [InlineData(1f, -FullLook, LookUpLimit)]
    public void LookingFullyUpOrDownStopsAtTheLimit(float risePerTick, float mouseDown, float limit)
    {
        // Falling or climbing tilts the view too: the mouse's look stops at the same pitch.
        Assert.Equal(limit, CameraAfter(risePerTick, 3f, mouseDown).Pitch, 0);
    }
}
