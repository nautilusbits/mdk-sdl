using System.Drawing;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Game.DevTools;
using Mdk.Game.Flow;

namespace Mdk.Game.Tests;

/// <summary>The technical lines logged at start (and the display's again when it changes) and shown
/// by the overlay.</summary>
public class TechInfoTests
{
    private static readonly GpuInfo Gpu = new(GpuBackend.Vulkan, "NVIDIA GeForce RTX 3060", "581.57");

    private static readonly ScreenInfo Screen = new(new ScreenMode(3440, 1440, 144f), new Resolution(1280, 960), Fullscreen.Off,
        Visibility.Shown, new Resolution(1920, 1440), "B8G8R8A8_UNORM", "D32_FLOAT", 4, VSync.On);

    [Fact]
    public void BuildLine() =>
        Assert.Equal("MDK SDL 1.0.0-beta.1 | .NET 10.0.1 Native AOT | SDL 3.4.0",
            TechInfo.Build(new BuildInfo("1.0.0-beta.1", "10.0.1", Compilation.NativeAot, "3.4.0")));

    [Fact]
    public void GpuLine() =>
        Assert.Equal("GPU: NVIDIA GeForce RTX 3060 (Vulkan via SDL_GPU), driver 581.57", TechInfo.Gpu(Gpu));

    [Fact]
    public void GpuLineWithoutDriverDetails() =>
        Assert.Equal("GPU: unknown (Direct3D 12 via SDL_GPU)", TechInfo.Gpu(new GpuInfo(GpuBackend.Direct3D12, "", "")));

    [Fact]
    public void DisplayLine() =>
        Assert.Equal("Display: 3440x1440 @ 144 Hz, window 1280x960 (windowed), render target 1920x1440, "
            + "swapchain B8G8R8A8_UNORM (8 bits per channel, SDR), depth D32_FLOAT, MSAA 4x, present mode VSYNC",
            TechInfo.Display(Screen));

    [Fact]
    public void DisplayLineNamesTheModes()
    {
        var line = TechInfo.Display(Screen with { Fullscreen = Fullscreen.Exclusive, Samples = 1, VSync = VSync.Adaptive });

        Assert.Contains("(exclusive fullscreen)", line);
        Assert.Contains("MSAA off", line);
        Assert.Contains("present mode MAILBOX", line);
        Assert.Contains("(hidden)", TechInfo.Display(Screen with { Visibility = Visibility.Hidden }));
    }

    [Fact]
    public void LookLine()
    {
        Assert.Equal("Look: Enhanced, mods: hd-textures, gltf", TechInfo.Look(Graphics.Enhanced, ["hd-textures", "gltf"]));
        Assert.Equal("Look: Original, mods: none", TechInfo.Look(Graphics.Original, []));
    }

    [Fact]
    public void LayoutLine()
    {
        // A foldable's inner display at 420 dpi (2.625 units a dp, 160 dp an inch).
        var safe = new RectangleF(0f, 0f, 2156f, 2344f);

        Assert.Equal("Layout: 420 units/inch, safe area 0,0 2156x2344, render scale Auto (64 %), quality Mobile",
            TechInfo.Layout(420f / 25.4f, safe, Renderer.AutoScale, new Resolution(2156, 2344), EffectQuality.Mobile));
        Assert.Contains("render scale 150 %, quality Full", TechInfo.Layout(0f, safe, 150, new Resolution(1280, 960), EffectQuality.Full));
    }

    [Fact]
    public void OverlayLines() =>
        Assert.Equal(["GPU NVIDIA GeForce RTX 3060, Vulkan", "window 1280x960 windowed, target 1920x1440, B8G8R8A8_UNORM, VSYNC"],
            TechInfo.Overlay(Gpu, Screen));
}
