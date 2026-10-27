using System.Drawing;
using System.Globalization;
using System.Reflection;
using System.Runtime;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Game.Flow;

namespace Mdk.Game.DevTools;

/// <summary>How the program was compiled: ahead of time (the published exe) or just in time.</summary>
public enum Compilation { NativeAot, Jit }

/// <summary>The program's version, .NET's, how it's compiled, and SDL's version.</summary>
public readonly record struct BuildInfo(string Version, string Runtime, Compilation Compilation, string Sdl)
{
    public static BuildInfo Current()
    {
        var version = typeof(BuildInfo).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "";
        // "1.0.0-beta.1+commit": the commit stays out.
        var plus = version.IndexOf('+', StringComparison.Ordinal);
        // Native AOT compiles nothing at run time (IsDynamicCodeSupported is off in JIT builds too).
        var compilation = JitInfo.GetCompiledMethodCount() > 0 ? Compilation.Jit : Compilation.NativeAot;
        return new BuildInfo(plus < 0 ? version : version[..plus], Environment.Version.ToString(), compilation, Window.SdlVersion);
    }
}

/// <summary>The display and the frame: the desktop's mode, the window, the frame drawn and how it's shown.</summary>
public readonly record struct ScreenInfo(ScreenMode Desktop, Resolution Window, Fullscreen Fullscreen, Visibility Visibility,
    Resolution Target, string Swapchain, string Depth, int Samples, VSync VSync);

/// <summary>The technical lines logged at start (the display's again when it changes) and the
/// overlay's.
/// <code>
///   MDK SDL 1.0.0-beta.1 | .NET 10.0.1 Native AOT | SDL 3.4.0
///   GPU: NVIDIA GeForce RTX 3060 (Vulkan via SDL_GPU), driver 581.57
///   Display: 3440x1440 @ 144 Hz, window 1280x960 (windowed), render target 1280x960, swapchain
///            B8G8R8A8_UNORM (8 bits per channel, SDR), depth D32_FLOAT, MSAA 4x, present mode VSYNC
///   Look: Enhanced, mods: hd-textures
/// </code></summary>
public static class TechInfo
{
    public static string Build(BuildInfo build)
    {
        var compiled = build.Compilation == Compilation.NativeAot ? "Native AOT" : "JIT";
        return $"MDK SDL {build.Version} | .NET {build.Runtime} {compiled} | SDL {build.Sdl}";
    }

    public static string Gpu(GpuInfo gpu)
    {
        var device = gpu.Device.Length != 0 ? gpu.Device : "unknown";
        var driver = gpu.DriverVersion.Length != 0 ? $", driver {gpu.DriverVersion}" : "";
        return $"GPU: {device} ({GpuBackends.Name(gpu.Backend)} via SDL_GPU){driver}";
    }

    public static string Display(ScreenInfo screen)
    {
        var desktop = screen.Desktop;
        var refresh = desktop.Refresh.ToString("0.##", CultureInfo.InvariantCulture);
        return $"Display: {desktop.Size} @ {refresh} Hz, window {screen.Window} ({WindowMode(screen)}), render target {screen.Target}, "
            + $"swapchain {screen.Swapchain} (8 bits per channel, SDR), depth {screen.Depth}, MSAA {Msaa(screen.Samples)}, "
            + $"present mode {PresentModes.Name(screen.VSync)}";
    }

    /// <summary>What sizes the layout and the frame's cost: the density (window units an inch),
    /// the safe area (cut-outs), the render scale and the enhanced look's quality.
    /// <code>
    ///   Layout: 420 units/inch, safe area 0,0 2156x2344, render scale Auto (64 %), quality Mobile
    /// </code></summary>
    public static string Layout(float unitsPerMm, RectangleF safe, int scale, Resolution window, EffectQuality quality)
    {
        var density = (unitsPerMm * MmPerInch).ToString("0", CultureInfo.InvariantCulture);
        var scaleText = scale == Renderer.AutoScale ? $"Auto ({Renderer.AutoPercent((uint)window.Width, (uint)window.Height)} %)" : $"{scale} %";
        return $"Layout: {density} units/inch, safe area {safe.X:0},{safe.Y:0} {safe.Width:0}x{safe.Height:0}, "
            + $"render scale {scaleText}, quality {quality}";
    }

    private const float MmPerInch = 25.4f;

    public static string Look(Graphics graphics, IReadOnlyList<string> mods) =>
        $"Look: {graphics}, mods: {(mods.Count != 0 ? string.Join(", ", mods) : "none")}";

    /// <summary>The overlay's two lines: the GPU, and the window and frame.</summary>
    public static IReadOnlyList<string> Overlay(GpuInfo gpu, ScreenInfo screen) =>
    [
        $"GPU {(gpu.Device.Length != 0 ? gpu.Device : "unknown")}, {GpuBackends.Name(gpu.Backend)}",
        $"window {screen.Window} {WindowMode(screen)}, target {screen.Target}, {screen.Swapchain}, {PresentModes.Name(screen.VSync)}",
    ];

    private static string WindowMode(ScreenInfo screen) =>
        screen.Visibility == Visibility.Hidden ? "hidden"
        : screen.Fullscreen switch
        {
            Fullscreen.Desktop => "fullscreen",
            Fullscreen.Exclusive => "exclusive fullscreen",
            _ => "windowed",
        };

    private static string Msaa(int samples) => samples > 1 ? $"{samples}x" : "off";
}
