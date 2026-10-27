using System.Numerics;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Game.Collision;

namespace Mdk.Game.Kurt;

/// <summary>The third-person camera behind Kurt (<c>camera_update</c>). Its pitch (positive looks
/// down) is the arena's (DTI), eased in, plus the mouse's look offset, a tilt in the air and
/// −40 × the smoothed rise of the feet per tick (climbing looks up, falling down). It rolls with
/// Kurt's moves (<see cref="CameraRoll"/>) and shakes (0x573aa8). It never gets closer: a wall or a
/// solid object between Kurt's head and the camera pushes Kurt away instead (camera_clearance).
/// <code>
///        camera ●                   wall │    camera ●         wall │  camera ●
///                 \  pitch               │  ◄ Kurt      ──►         │     ◄ ◄ Kurt (pushed d · n)
///   pivot 4.5 ─────● Kurt's feet + up
/// </code></summary>
public sealed class FollowCamera(ArenaSpace? space = null)
{
    /// <summary>Vertical field of view in degrees (the Godot port's, matching the original's view).</summary>
    public const float FieldOfView = 71.5f;
    private const float Distance = 8f;
    private const float MouseDegrees = 0.15f;
    /// <summary>The mouse's look stops at this pitch (the original's keys: −60° to 90°, back to 0 once
    /// let go). Further down Kurt's sprite lies flat under the camera and floors ahead hide it;
    /// further up the camera sinks to his feet, through the floor on a slope.</summary>
    private const float LookUpLimit = -30f;
    private const float LookDownLimit = 60f;
    /// <summary>The view's pitch stays short of straight up or down: past it the view turns over.</summary>
    private const float ViewPitchLimit = 89f;
    private const float AirPitchRate = 0.667f * Kurt.Ticks;
    private const float AirPitchMax = 40f;
    private const float AirPitchDecay = 40f;
    /// <summary>Falling faster than this (u/s) starts the air time.</summary>
    private const float FallStartSpeed = -16f;
    /// <summary>The arena pitch eases in by 0.85·old + 0.15·new per tick.</summary>
    private const float PitchEase = 0.85f;
    private const float DefaultPitch = 4f;
    /// <summary>Looking far up brings the camera closer: its distance shrinks below −20°.</summary>
    private const float CloseUpPitch = -20f;
    private const float CloseUpRange = 80f;
    private const float CloseUpBias = 100f;
    /// <summary>Looking down lifts the camera back towards Kurt by up to 5.</summary>
    private const float DownBack = 5f;
    private const float Near = 0.5f;
    private const float Far = 20000f;
    /// <summary>The climb (0x490db8): the rise per tick, clamped to ±0.5, eased in by 0.97·old +
    /// 0.03·new per tick, back towards a rise of the other sign by 0.02 per tick; 40° per unit.</summary>
    private const float ClimbClamp = 0.5f;
    private const float ClimbKeep = 0.97f;
    private const float ClimbReturn = 0.02f;
    private const float ClimbPitch = 40f;
    /// <summary>The camera's clearance (0x417ee8): the line of sight from the head (feet + 5.5) is a
    /// box of ±0.1; where Kurt is pushed needs a floor within 4 above or below his feet, else half
    /// the push to a side along the wall.</summary>
    private const float HeadHeight = 5.5f;
    private static readonly Vector3 SightBox = new(0.1f);
    private const float PushFloorRange = 4f;
    private const float PushSide = 0.5f;
    /// <summary>The camera's push on Kurt, and a solid object's shove, move him at most this fast
    /// (u/s), eased in over the next steps: all at once, a door behind him "fast-forwarded" him through
    /// it, far more visibly than in the original. float.MaxValue pushes all at once.</summary>
    private const float MaxPushSpeed = 40f;
    /// <summary>Screen shake: each tick above 1 the view turns by up to ±1.64 × the shake in pixels of
    /// the 360-high view (at most ±19 across, ±59 up and down); it drains by 0.25 per tick.</summary>
    private const float ShakeScale = 16384f * 0.0001f;
    private static readonly Vector2 ShakeLimit = new(19f, 59f);
    private const float ShakeDrain = 0.25f;
    private const float ShakeMin = 1f;
    private const float ViewHeight = 360f;

    private float _arenaPitch = DefaultPitch;
    private float _lookOffset;
    private float _airPitch;
    private float _airTime;
    private float _climb;
    private float? _lastFeetZ;
    private float _climbTime;
    private float _shake;
    private float _shakeTime;
    private readonly Random _random = new();
    /// <summary>The pitch but the mouse's (degrees), the pivot and the roll of the last two steps,
    /// for frames between them (<see cref="Blend"/>).</summary>
    private readonly record struct Aim(float Pitch, float Pivot, float Roll);
    private Aim? _previousAim;
    private Aim _aim;

    public Vector3 Position { get; private set; }
    public Vector3 Forward { get; private set; } = Vector3.UnitY;
    public Vector3 Up { get; private set; } = Vector3.UnitZ;
    public Vector3 Right { get; private set; } = Vector3.UnitX;
    /// <summary>The solid objects of Kurt's arena that block the view (flag 0x1000000): where the line
    /// of sight first meets one of their parts, or its end.</summary>
    public Func<Vector3, Vector3, Vector3>? ClipView;
    /// <summary>The mouse's look to start with (degrees, positive down; --look).</summary>
    public float Look { init => _lookOffset = value; }
    /// <summary>The shake's offset this tick (pixels of the 360-high view).</summary>
    public Vector2 ShakeOffset { get; private set; }

    /// <summary>Shakes the screen at least this much (0x467f7c, opcode 135).</summary>
    public void RaiseShake(float amount) => _shake = MathF.Max(_shake, amount);

    public void Update(Kurt kurt, float arenaPitch, Input input, float delta)
    {
        if (kurt.TopDownHeight is { } height)
        {
            LookDown(kurt, height);
            return;
        }

        _arenaPitch = float.Lerp(arenaPitch, _arenaPitch, MathF.Pow(PitchEase, delta * Kurt.Ticks));
        // Through the scope the mouse turns the scope instead.
        if (!kurt.Sniping)
        {
            _lookOffset += input.MouseY * MouseDegrees;
        }

        UpdateAirTime(kurt, delta);
        UpdateClimb(kurt.Feet.Z, delta);
        if (kurt.Rising)
        {
            // The end of a level lifts Kurt: no tilt in the air (end_level.gd).
            _airPitch = 0f;
        }
        else if (_airTime == 0f)
        {
            _airPitch = MathF.Max(_airPitch - AirPitchDecay * delta, 0f);
        }
        else
        {
            _airPitch = MathF.Max(_airPitch, MathF.Min(_airTime * AirPitchRate, AirPitchMax));
        }

        var auto = _arenaPitch + _airPitch - ClimbPitch * _climb + kurt.CameraTilt;
        _lookOffset = ClampLook(_lookOffset, auto);
        var pitch = ViewPitch(auto + _lookOffset);
        var facing = kurt.Facing;
        Position = Point(kurt.Feet, facing, pitch, kurt.CameraPivot);
        if (space != null && !kurt.Sniping && !kurt.Frozen && kurt.Current != Kurt.State.Dead)
        {
            Position = Clear(kurt, space, Position, delta);
        }

        Orient(facing, pitch);
        UpdateShake(delta);
        Turn(ShakeOffset * float.DegreesToRadians(FieldOfView) / ViewHeight, float.DegreesToRadians(kurt.CameraRoll.Roll));

        var aim = new Aim(auto, kurt.CameraPivot, kurt.CameraRoll.Roll);
        _previousAim = _previousAim == null ? aim : _aim;
        _aim = aim;
    }

    /// <summary>The mouse's <paramref name="look"/> (degrees) kept from turning the view past the look
    /// limits; the <paramref name="auto"/> pitch (arena, air, climb) alone may go past them.</summary>
    private static float ClampLook(float look, float auto) =>
        Math.Clamp(look, MathF.Min(LookUpLimit - auto, 0f), MathF.Max(LookDownLimit - auto, 0f));

    /// <summary>The view's pitch (radians) for <paramref name="degrees"/> down, short of straight up or
    /// down: at 90° the view's up has no way to point, and the float cosine's sign turned it over.</summary>
    private static float ViewPitch(float degrees) => float.DegreesToRadians(Math.Clamp(degrees, -ViewPitchLimit, ViewPitchLimit));

    /// <summary>Looks along <paramref name="facing"/>, <paramref name="pitch"/> (radians) down.</summary>
    private void Orient(Vector3 facing, float pitch)
    {
        Forward = Vector3.Normalize(facing * MathF.Cos(pitch) - Vector3.UnitZ * MathF.Sin(pitch));
        Up = Vector3.Normalize(Vector3.Cross(Vector3.Cross(Forward, Vector3.UnitZ), Forward));
        Right = Vector3.Normalize(Vector3.Cross(Forward, Vector3.UnitZ));
    }

    /// <summary>A frame's mouse look up and down, at once rather than with the next step (screens
    /// above 60 Hz).</summary>
    public void MouseLook(Kurt kurt, Input input)
    {
        if (kurt.TopDownHeight != null || kurt.Sniping)
        {
            return;
        }

        _lookOffset = ClampLook(_lookOffset + input.TakeMouseY() * MouseDegrees, _aim.Pitch);
    }

    /// <summary>A frame between steps: the camera placed again for Kurt as drawn (<paramref name="feet"/>,
    /// <paramref name="yaw"/>), its pitch, pivot and roll <paramref name="alpha"/> of the way between
    /// the last two steps', the mouse's look as it is now. (The camera's clearance moved Kurt, not
    /// the camera: it's where his feet put it.) The next step places it anew.</summary>
    public void Blend(Kurt kurt, Vector3 feet, float yaw, float alpha)
    {
        var yawRadians = float.DegreesToRadians(yaw);
        var facing = new Vector3(MathF.Cos(yawRadians), MathF.Sin(yawRadians), 0f);
        if (kurt.TopDownHeight is { } height)
        {
            Position = feet + Vector3.UnitZ * height;
            Forward = -Vector3.UnitZ;
            Up = facing;
            Right = new Vector3(facing.Y, -facing.X, 0f);
            return;
        }

        if (_previousAim is not { } from)
        {
            return;
        }

        var pitch = ViewPitch(FrameBlend.Lerp(from.Pitch, _aim.Pitch, alpha) + _lookOffset);
        Position = Point(feet, facing, pitch, FrameBlend.Lerp(from.Pivot, _aim.Pivot, alpha));
        Orient(facing, pitch);
        Turn(ShakeOffset * float.DegreesToRadians(FieldOfView) / ViewHeight, float.DegreesToRadians(FrameBlend.Lerp(from.Roll, _aim.Roll, alpha)));
    }

    /// <summary>Turns the view by the shake (radians across and up), then rolls it: a positive roll
    /// turns its up towards its right.</summary>
    private void Turn(Vector2 shake, float roll)
    {
        if (shake != Vector2.Zero)
        {
            Forward = Vector3.Normalize(Forward + Right * MathF.Tan(shake.X) + Up * MathF.Tan(shake.Y));
            Right = Vector3.Normalize(Vector3.Cross(Forward, Up));
            Up = Vector3.Normalize(Vector3.Cross(Right, Forward));
        }

        if (roll == 0f)
        {
            return;
        }

        var (up, right) = (Up, Right);
        Up = up * MathF.Cos(roll) + right * MathF.Sin(roll);
        Right = right * MathF.Cos(roll) - up * MathF.Sin(roll);
    }

    /// <summary>A new random offset each tick while the shake is above 1; it drains by 0.25 a tick.</summary>
    private void UpdateShake(float delta)
    {
        if (_shake <= 0f)
        {
            ShakeOffset = Vector2.Zero;
            return;
        }

        _shakeTime += delta * Kurt.Ticks;
        while (_shakeTime >= 1f)
        {
            _shakeTime -= 1f;
            ShakeOffset = Vector2.Zero;
            if (_shake > ShakeMin)
            {
                var amplitude = _shake * ShakeScale;
                var offset = new Vector2(Random(amplitude), Random(amplitude));
                ShakeOffset = Vector2.Round(Vector2.Clamp(offset, -ShakeLimit, ShakeLimit));
            }

            _shake -= ShakeDrain;
            if (_shake < ShakeDrain)
            {
                _shake = 0f;
            }
        }
    }

    private float Random(float amplitude) => (_random.NextSingle() * 2f - 1f) * amplitude;

    /// <summary>The camera's clearance (camera_clearance 0x417ee8): a wall between Kurt's head and the
    /// camera pushes Kurt d · n away from it (d: how far the camera is past it, n: its horizontal
    /// normal towards him) if there's floor there (or half of d to a side); then the solid objects in
    /// the way push him by what they cut off. The camera moves as far as Kurt did.</summary>
    private Vector3 Clear(Kurt kurt, ArenaSpace arenas, Vector3 camera, float delta)
    {
        var head = kurt.Feet + Vector3.UnitZ * HeadHeight;
        if (arenas.Sight(head, camera, SightBox) is var (point, plane))
        {
            var normal = new Vector2(plane.Normal.X, plane.Normal.Y);
            if (plane.Distance(head) < 0f)
            {
                normal = -normal;
            }

            var d = Vector2.Distance(new Vector2(camera.X, camera.Y), new Vector2(point.X, point.Y));
            if (Push(kurt, arenas, normal * d, new Vector2(normal.Y, -normal.X) * (d * PushSide)) is { } push)
            {
                // Eased in (MaxPushSpeed): what's left is pushed on the next steps.
                var limit = MaxPushSpeed * delta;
                if (push.Length() > limit)
                {
                    push = Vector2.Normalize(push) * limit;
                }

                camera += kurt.Shove(new Vector3(push, 0f));
            }
        }

        if (ClipView == null)
        {
            return camera;
        }

        var end = ClipView(kurt.Feet + Vector3.UnitZ * HeadHeight, camera);
        // A solid object (a door) behind Kurt shoves him on by up to ~8 units in one step: eased in too.
        var offset = end - camera;
        var clipLimit = MaxPushSpeed * delta;
        if (offset.Length() > clipLimit)
        {
            offset = Vector3.Normalize(offset) * clipLimit;
        }

        return camera + kurt.Shove(offset);
    }

    /// <summary>The push, or a push to a side, with a floor under it (in the air: the push), or null.</summary>
    private static Vector2? Push(Kurt kurt, ArenaSpace arenas, Vector2 push, Vector2 side)
    {
        if (!kurt.OnFloor)
        {
            return push;
        }

        foreach (var offset in (ReadOnlySpan<Vector2>)[push, push + side, push - side])
        {
            var at = kurt.Feet + new Vector3(offset, 0f);
            if (arenas.Crosses(at + Vector3.UnitZ * PushFloorRange, at - Vector3.UnitZ * PushFloorRange))
            {
                return offset;
            }
        }

        return null;
    }

    /// <summary>The bomber's view (0x4183f0): straight down from <paramref name="height"/> above
    /// Kurt, his heading at the top.</summary>
    private void LookDown(Kurt kurt, float height)
    {
        Position = kurt.Feet + Vector3.UnitZ * height;
        Forward = -Vector3.UnitZ;
        Up = kurt.Facing;
        Right = kurt.Right;
    }

    /// <summary>Air time (0x573a48): it starts once Kurt falls faster than 16 u/s and ends when he
    /// rises or stands still on a floor. Running downhill loses the floor for single steps without
    /// tilting the view.</summary>
    private void UpdateAirTime(Kurt kurt, float delta)
    {
        var vz = kurt.VerticalSpeed;
        if (_airTime == 0f)
        {
            _airTime = vz < FallStartSpeed ? delta : 0f;
            return;
        }

        var landed = vz == 0f && kurt.OnFloor;
        _airTime = vz > 0f || landed ? 0f : _airTime + delta;
    }

    /// <summary>The smoothed rise of the feet per tick (camera_update 0x4174d0), measured once a
    /// tick as the original's frames at 30 per second: Kurt's 60 steps a second go down a slope
    /// every other step, which would shake the view.</summary>
    private void UpdateClimb(float feetZ, float delta)
    {
        _lastFeetZ ??= feetZ;
        _climbTime += delta * Kurt.Ticks;
        if (_climbTime < 1f)
        {
            return;
        }

        var ticks = MathF.Floor(_climbTime);
        _climbTime -= ticks;
        var rise = Math.Clamp((feetZ - _lastFeetZ.Value) / ticks, -ClimbClamp, ClimbClamp);
        _lastFeetZ = feetZ;
        _climb = Climb(_climb, rise, ticks);
    }

    /// <summary>One step of the climb: eased towards the rise; going the other way it also comes
    /// back by 0.02 per tick, without passing the rise.</summary>
    public static float Climb(float climb, float rise, float ticks)
    {
        var keep = MathF.Pow(ClimbKeep, ticks);
        climb = rise * (1f - keep) + climb * keep;
        if (climb < 0f && rise >= 0f)
        {
            return MathF.Min(climb + ClimbReturn * ticks, rise);
        }

        if (climb > 0f && rise <= 0f)
        {
            return MathF.Max(climb - ClimbReturn * ticks, rise);
        }

        return climb;
    }

    /// <summary>The camera's place for the feet and pitch (radians).</summary>
    private static Vector3 Point(Vector3 feet, Vector3 facing, float pitch, float pivot)
    {
        var distance = Distance;
        var back = -distance * MathF.Cos(pitch);
        if (pitch > 0f)
        {
            back += DownBack * (1f - MathF.Cos(pitch));
        }
        else if (pitch < float.DegreesToRadians(CloseUpPitch))
        {
            distance = Distance * (float.RadiansToDegrees(pitch) + CloseUpBias) / CloseUpRange;
            back = -distance * MathF.Cos(pitch);
        }

        return feet + facing * back + Vector3.UnitZ * (pivot + distance * MathF.Sin(pitch));
    }

    public View View(float aspect) => CameraMath.View(Position, Forward, Up, CameraMath.KeepWidth(FieldOfView, aspect), aspect, Near, Far);

    /// <summary>Sniper mode: the view from Kurt's eye through the scope.</summary>
    public static View SniperView(Kurt kurt, float aspect) => SniperView(kurt.SniperEye, kurt.SniperForward, kurt.Scope.Zoom, aspect);

    /// <summary>Sniper mode's view from <paramref name="eye"/> (a frame between steps).</summary>
    public static View SniperView(Vector3 eye, Vector3 forward, float zoom, float aspect) =>
        CameraMath.ScopeView(eye, forward, zoom, aspect, Near, Far);

    /// <summary>The camera's up for a line of sight (no roll).</summary>
    public static Vector3 UpOf(Vector3 forward) =>
        Vector3.Normalize(Vector3.Cross(Vector3.Cross(forward, Vector3.UnitZ), forward));
}

/// <summary>View matrices for a camera (MDK coordinates, Z up).</summary>
public static class CameraMath
{
    /// <summary>The original's screen: 4:3.</summary>
    private const float OriginalAspect = 4f / 3f;

    /// <summary>A vertical field of view that keeps a 4:3 view's width on a narrower screen (a
    /// near-square foldable sees as far left and right, more above and below); wider screens see
    /// more at the sides at the same height.
    /// <code>
    ///   aspect ≥ 4:3: fov (Hor+)    aspect &lt; 4:3: 2 atan(tan(fov / 2) × (4/3) / aspect) (Vert-)
    /// </code></summary>
    public static float KeepWidth(float fieldOfView, float aspect)
    {
        if (aspect >= OriginalAspect || aspect <= 0f)
        {
            return fieldOfView;
        }

        var half = MathF.Tan(float.DegreesToRadians(fieldOfView) / 2f) * OriginalAspect / aspect;
        return float.RadiansToDegrees(2f * MathF.Atan(half));
    }

    public static View View(Vector3 position, Vector3 forward, Vector3 up, float fieldOfView, float aspect, float near, float far)
    {
        var view = Matrix4x4.CreateLookAt(position, position + forward, up);
        var projection = Matrix4x4.CreatePerspectiveFieldOfView(float.DegreesToRadians(fieldOfView), aspect, near, far);

        // The sky only turns with the camera: its rotation alone, inverted.
        var rotation = Matrix4x4.CreateLookAt(Vector3.Zero, forward, up);
        Matrix4x4.Invert(rotation * projection, out var clipToDirection);
        return new View(view * projection, clipToDirection, position, new ViewCamera(forward, up, projection, near));
    }

    /// <summary>Sniper mode's view (0x57428c): the focal length is 384 / zoom pixels of the 480-high
    /// screen, and the frustum is shifted down so that the scope's centre (y 279) is on the line of
    /// sight.
    /// <code>
    ///   y 0   ┌───────────┐  top    =  279 · near / focal
    ///         │     +     │  ◄── line of sight, y 279
    ///   y 480 └───────────┘  bottom = -201 · near / focal
    /// </code></summary>
    public static View ScopeView(Vector3 eye, Vector3 forward, float zoom, float aspect, float near, float far)
    {
        var up = FollowCamera.UpOf(forward);
        var view = Matrix4x4.CreateLookAt(eye, eye + forward, up);
        var projection = ScopeProjection(zoom, aspect, near, far);
        var rotation = Matrix4x4.CreateLookAt(Vector3.Zero, forward, up);
        Matrix4x4.Invert(rotation * projection, out var clipToDirection);
        return new View(view * projection, clipToDirection, eye, new ViewCamera(forward, up, projection, near));
    }

    /// <summary>The scope's off-centre projection for a window of <paramref name="aspect"/>.</summary>
    public static Matrix4x4 ScopeProjection(float zoom, float aspect, float near, float far)
    {
        var unit = near * zoom / Scope.Focal;
        var halfWidth = Scope.ScreenHeight / 2f * aspect * unit;
        var top = Scope.Centre.Y * unit;
        var bottom = -(Scope.ScreenHeight - Scope.Centre.Y) * unit;
        return Matrix4x4.CreatePerspectiveOffCenter(-halfWidth, halfWidth, bottom, top, near, far);
    }
}
