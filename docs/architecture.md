# Architecture

C# (.NET 10) and SDL3 (ppy.SDL3-CS) port of MDK (1997), following the Godot port
(`../godot-mdk`), whose `docs/` hold the reverse-engineering notes (formats, engine, script
opcodes). Overviews: [formats.md](formats.md), [original-engine.md](original-engine.md). Requires
the original game data.

## Layers

Each layer talks only to the one below it.

```
 Mdk.App      command line ──► Game.Run, Game.UpscaleTextures, Game.ExportAssets
 Mdk.Android  SDLActivity, first start: folder picker ──► MdkData.Import ──► Game.Run
    │
 Mdk.Game     Flow: Game (screens), GameState, Settings, SaveGames, LevelFlow
    │         Menu: main menu, pause menu, options (Display, Graphics, Audio, Controls, Game, Mods), loading, statistics, save prompt, videos
    │         Stream: the tube between levels (generator, Kurt's flight, drawing)
    │         Fall: the fall before a level (FallSim, FallView, FallHud)
    │         Viewer (a level): level, camera, collision (BSP), sound mixer, scripts and objects, Kurt
    │         DevTools: the console, its commands, the debug overlay
    │         HdTextures: the enhanced look's upscaled textures (export, generator: a mod)
    │         Mods: mods/ (catalogue, images, models from glTF, the exports)
    │   └──────────────► Mdk.Formats  (parsers: DTI, MTO, MTI, SNI, FTI, BNI, models, PNG, glTF binary; the 1996 demo's)
 Mdk.Engine   Render (Renderer: meshes, index textures, palettes, panorama; GPU backends, VSync)
    │         Audio (AudioDevice: software mixer of voices, music and effects buses, streamed voices, master limiter)
    │         Platform (Window and its icon, display modes, frame limiter, Input: game keys (rebindable), menu keys, raw keys, pointer, text, cursor, SDL3;
    │                   TouchControls: on-screen stick and buttons of a touch screen;
    │                   HttpDownload: the OS's HTTP library, WinHTTP or libcurl)
    │         Diagnostics (Profiler: frame sections; LogRing, LogWriter: the output's last lines)
    │         Upscale (RealEsrgan: the upscaler's download and process)
 SDL3         SDL_GPU (Direct3D 12 / Vulkan / Metal), events
```

- `Mdk.Formats` has no dependencies: bytes in, data out.
- `Mdk.Engine` hides SDL: the game sees meshes, materials, keys. It also drives the HD textures'
  upscaler (`Upscale/`: download, checksum, process).
- Shaders (`shaders/*.hlsl`) are compiled at build time to each format whose tool is found, and
  embedded; the renderer creates its device for the backend asked for (Options, Display;
  `--gpu`: `SDL_CreateGPUDeviceWithProperties` with its driver name and shader format), else
  (failing, not on the OS, or Auto) with the first embedded format a GPU driver takes.
- Display (`Platform/Display.cs`, `Window.Apply`): windowed at a size, fullscreen on the desktop,
  or exclusive (`SDL_SetWindowFullscreenMode` with the display's mode of that size at its highest
  refresh); applied on a change only, never to a hidden window (tests: always 1280 x 960).
  The renderer draws at the render scale of the swapchain's size and blits (linear when scaled;
  Auto, Android's default: about 1080p's pixels, 50-100 %, `Renderer.AutoPercent`);
  VSync is the swapchain's present mode (VSYNC, IMMEDIATE, MAILBOX; falls back to one the window
  supports); the frame limiter sleeps with `SDL_DelayPrecise`.
- `Mdk.Formats` finds the game: `MDK_DATA_DIR`, `mdk` in `mdk_paths.cfg` next to the program
  (`LocalPaths`, the Godot port's file), the folders above the program, GOG and Steam folders.
- `Mdk.Formats` finds data files whatever their case (`CaseInsensitivePath`: the GOG installation
  has `MISC/mdkfont.fti`, the game asks for `MISC/MDKFONT.FTI`).

## Platforms

One native executable per platform (Native AOT) with the shaders embedded, and SDL3's library
for its runtime next to it (`SDL3.dll`, `libSDL3.so`, `libSDL3.dylib`).

```
 shaders/*.hlsl ──dxc──────────► DXIL ───► Direct3D 12 (Windows)
        │
        └──────dxc -spirv──────► SPIR-V ─► Vulkan (Linux, Android; Windows with --gpu=vulkan)
                                   │
                                   └──spirv-cross──► MSL ─► Metal (macOS)
```

- Bindings (SDL_GPU's rules, `shaders/bindings.hlsli`): HLSL registers in spaces 1-3 for DXIL;
  SPIR-V combines each texture with its sampler at set 2, binding = register; spirv-cross keeps
  the SPIR-V bindings as Metal indices (`--msl-decoration-binding`).
- A missing tool skips its format; CI requires its platform's (`-p:RequiredShaders`).
- CI (`.github/workflows/build.yml`): Windows (DXIL, SPIR-V), Linux (SPIR-V), macOS (SPIR-V,
  MSL) build, test without the game data (`[DataFact]` tests are skipped) and publish; Android
  (SPIR-V) builds the APK; a `v*` tag releases them. Shader tools: the DirectXShaderCompiler
  release (Windows, Linux), the Vulkan SDK (macOS).

### Android

`src/Mdk.Android` (net10.0-android, Mono) is a head like `Mdk.App`: SDL's activity (Java side and
`libSDL3.so` from ppy.SDL3-CS's Android flavour) runs `Game` on SDL's thread. The desktop
projects are built without a runtime identifier for it (`GlobalPropertiesToRemove`). SDL_GPU uses
Vulkan with the SPIR-V shaders.

```
 start ─► StorageChoice.Plan ─InPlace─► Game (data and MDK_USER_DIR = the chosen folder)
   │  (app files/data-folder.txt,
   │   AllFilesAccess, MdkData.Imported)
   ├─Lost─► "can't read it" ─► forget ─► plan again
   ├─Copied─► Game (data: app files/mdk; MDK_USER_DIR = app files)
   ├─AskAccess─► dialog ─allow─► All files access ─► ACTION_OPEN_DOCUMENT_TREE ─► TreePaths.Resolve
   │                │declined, refused, backed out       MdkData.At, writable ─► data-folder.txt ─► InPlace
   │                ▼
   └─CopyFlow─► ACTION_OPEN_DOCUMENT_TREE ─► DocumentTree (IDataTree) ─MdkData.FindIn
                     app files/mdk ◄─MdkData.Import─┘ (TRAVERSE, FALL3D, STREAM, MISC + stamp) ─► Copied
```

- In place (the default APK): with All files access (`MANAGE_EXTERNAL_STORAGE`, Android 11 on;
  the storage permission and `requestLegacyExternalStorage` before) the picked folder's tree URI
  becomes a path (`TreePaths`: `primary:MDK` ─► `/storage/emulated/0/MDK`, SD cards, raw
  Downloads; clouds have none) and the game reads it as on a PC, its settings, saves, mods and
  `mdk.log` there. `app files/data-folder.txt` remembers it; each start checks the access, the
  game files and that the folder can be written, else says so, forgets it and plans again (the
  copy if there is one, else asks again).
- Copied (declined, a store build, an install from before): shared storage is read through
  content URIs and copied once; `.imported` is written last (an interrupted copy starts over).
  An earlier copy keeps being played. Options, "Import from folder" copies again, or switches to
  a folder in place from the next start (settings and saves copied there, files there kept).
- Store build: `-p:StoreBuild=true` defines `STORE_BUILD`: no storage permissions in the manifest,
  `AllFilesAccess` says `Unavailable`, the files are always copied.
- Setup dialogs (`Dialogs`): platform Material alerts (`Theme.Material.Dialog.Alert`, no
  AndroidX) in the icon's teal and orange (`Resources/values/styles.xml`), titled; the copy shows
  a progress bar. `Backdrop`, a view over SDL's still black surface, draws a starry sky, the icon
  and the name on the left and moves the dialogs right, undimmed; it's removed before the game
  starts. In game (Options, "Import from folder") they stay centred over the game, dimmed.
- `Window` on Android: landscape, full screen, back button trapped (Esc; the manifest turns
  predictive back off), no text input (it would open the keyboard). While the mouse is captured
  (play), fingers drive `TouchControls` (`Input.Touch`: keys of their own, which tests' holds
  don't release) and SDL's mouse events made of touches are dropped; otherwise touches are clicks.
  `TouchView` (Game) draws the controls in the renderer's overlay.
- The controls' sizes are physical (`ScreenDensity`: SDL's display scale over Android's 160 dpi
  → window units per mm): 11 mm buttons, 3 mm apart, at most the 15 % of the height a phone has;
  inside the window's safe area. `TouchView` stretches rounded masks (9 parts: corners kept),
  a see-through fill (brighter while held), a thin outline, the label scaled to the button.
- `TouchLayout` (Engine) holds the defaults and the player's places (`TouchPlace`: gaps to the
  safe area's nearer edges as parts of it, size in mm; `touch.Fire=Right,0.0113,Bottom,0.2,9.8,9.8`
  in settings.cfg), fitted to any display (inside the safe area, buttons 7-25 mm, the stick's
  zone at least a full push). `TouchEditor` (Game, logic; unit tested) and `TouchLayoutPage`
  (pointer, keys, drawing) edit them from Options, Touch; `--touch-layout` opens it on the
  desktop. A finger keeps the role it landed with: looks don't press buttons, buttons don't look.
- The menus' rows grow on a touch screen towards 10 mm (at most × 1.4, as far as the page fits;
  `MenuItems.RowFor`, `Enlargement`): a phone's 6.5 mm rows become 9 mm; tablets' are big enough.

## Rendering (original look)

```
 Arena triangles ──Layers (depth layers)───► batches per surface ──► Renderer
                                                                     │
 surface = index texture (R8) + palette (256x1) │ flat colour        ▼
                                                                offscreen colour + depth
                                                                     │ blit
                                                                  swapchain
```

- Palette index 0 is transparent.
- Coplanar details (`Level/Layers.cs`): a triangle overlapping bigger ones of its plane is a layer
  above them; layers and outlines are drawn pulled towards the eye (`DepthPull`), the same place on
  screen, nearer in depth: no flicker, no cracks. The original draws back to front, without depth.
- Two drawn arenas (`DepthBands`, `ArenaBand`): the camera's arena, its objects, Kurt and his board
  take the front half of the depth range (viewport min/max depth), the other arena the back half,
  so the camera's covers it as in the original (0x41df35). The screen passes map depth back.
- Vertex colours (white unless given) multiply the surface, Gouraud-blended: the stream's tube.
- Glass (`GLASS1-4`) blends after the opaque surfaces; mirrors show the panorama where the sky
  behind them would be (shifted by MIRRLOW...MIRRHIGH).
- Insets (`Renderer.Insets.cs`): 3D views in canvas rectangles, under or over the canvas (sniper
  mode's round cameras and clip).
- The sky is a screen-filling triangle drawn first, scrolled by yaw and pitch like the original's
  2D backdrop.
- Anti-aliasing (`Renderer.AntiAliasing`, 2x or 4x MSAA): the scene is drawn into a multisampled
  target resolved into the frame; off, nothing changes.

## Rendering (enhanced look)

The game picks the look (`Settings.Graphics`, `Level/EnhancedLook.cs`, the Godot port's
`Level._enhance`): materials carry a `Shading` (arenas and objects `Lit`, Kurt and effects
`Sprite`, glass and mirrors `Original`), the sky a `Sampling`, and the renderer gets a `Lighting`
per arena (`Level/LevelLight.cs`, switched as the camera's arena changes: sun, sky and ground
light, exposure, shadows, glow, haze in the colour below the sky panorama) and the frame's
`PointLights`. The 2D canvas is filtered too (`Renderer.CanvasSampling`).

```
 sky panorama (SkyLight) ─► mean colour above / below the horizon ─► sun, sky, ground ─┐
 arena faces (ArenaShape) ─► covered? (ceilings ≥ ½ floors) mean up, mean sun facing ──┴─► Lighting
 Kurt's muzzle, sniper rounds, explosions, FIRE boxes (LightSources) ─► PointLights (64 ─► nearest 16)
```

- Per arena (`EnhancedLook.Lighting`): open arenas get a sun (tinted by the sky, stronger under
  a brighter one) with shadows, the sky's light from above and the ground's from below; covered
  ones no sun, no shadow map, more light all around. The exposure evens the arena's mean light
  (faces by area, sun on 70% of them) to 1.15: the original's unlit textures, plus the roll-off.

```
 sun's depth (Lit casters) ──► shadow map 2048² ─────────┐
 camera's depth (opaque) ───────────────────────────────┐ │
 scene: filtered sky, enhanced.hlsl (MSAA) ──► scene ───┼─┴─mips─► post.hlsl ─► frame ─► canvas, insets
                                                        └ ambient occlusion    (occlusion × colour, glow)
```

- Colour textures (`Renderer.Colours.cs`, `ColourMips`): surfaces and sprites sample each index
  texture expanded through its palette to RGBA8 (premultiplied, index 0 clear), with box-filtered
  mips and a 2D array layer per animated frame, trilinear and 16x anisotropic. Asked for as the
  level's surfaces resolve (`Renderer.Prepare`) and made together before its first frame (55-75
  MB a level, expanded on every core, through one upload buffer); rewritten in place when their
  indices or palette change (bullet holes).
- `palette_filtered.hlsli` (the canvas): bilinear by hand, each of the four texels through the
  palette; index 0 transparent; animated textures keep to their frame.
- `enhanced.hlsl`: flat normals from the world position's screen derivatives (the triangle's
  plane, turned to the camera); light in linear colour: albedo × exposure × (hemisphere + sun ×
  N·L × shadow + point lights), tone mapped (`Tonemap`: kept up to linear 0.6, then rolled off
  towards white), then the haze (1 − e^(−density × distance)), dithered (`dither.hlsli`).
  Point lights come in a second uniform buffer pushed once per frame; they fade as (1 − d²/r²)².
  Sprites and explosions: filtered, unlit, edges cut at half cover.
- Shadows (`SunShadow`): an orthographic view along the sunlight, centred on the camera and
  snapped to whole texels; 2 × 2 compared texels, slope and normal offsets against acne.
- `occlusion.hlsl`: Alchemy ambient occlusion from the camera's depth, fading in the haze, 12
  samples turned in a 4 x 4 ordered pattern, into its own target (`screen.hlsli` shared).
- `post.hlsl`: the occlusion blurred over 4 x 4 pixels of the same plane (no grain, no shade
  across edges); glow from the scene's blurred mips, screen-blended; dithered.
- Cost (`EffectQuality`, Android's Mobile; `--quality`): Mobile draws a 1024² shadow map and the
  occlusion at half the frame's sides (texel centres snapped to pixels), blurred over 4 x 4 of
  its texels. Measured (RTX 4050, level 3, HD textures, frame / GPU wait ms): 2880 x 1800 Full
  11.4 / 5.1, Mobile 7.3 / 3.2; 1814 x 1134 (Auto's 63 %) Full 4.0 / 2.0, Mobile 3.3 / 1.4. At
  2880 x 1800 the occlusion is half the GPU's time (its samples spread over more pixels), the
  camera's depth pass 2.5 ms, post 1, the scene 1.5, shadows 0.5, mips 0.3.

## HD textures

The enhanced look's textures and 2D images upscaled by Real-ESRGAN, made on the player's computer
from the game's files (`HdTextures/`) into a mod (`mods/hd-textures/`, priority -100; see Mods),
never distributed. The tool is the engine's (`Upscale/RealEsrgan.cs`: download, SHA-256, process;
the download through `Platform/HttpDownload.cs`: WinHTTP on Windows, libcurl on Linux and macOS);
the game decides what and how.

```
 make (HdGenerator: --upscale-textures, Options, Mods "Make HD textures" via HdJob)
 ─────────────────────────────────────────────────────────────────────────────────
 LevelData, CMI ─► TextureExport (arenas, corridors, models × arena palettes) ─► HdSource (key = HdKey)
 TRAVSPRT.BNI, level SNI ─► TextureExport.Kurt (Kurt's frames × the level's palette, soft alpha) ─┤
 CanvasExport (the HUD × level palette; menus, loading screens, falls' HUD; no fonts) ────────────┘
   ─► UpscaleImages.Input (bleed, wrap 8) ─► PNG ─► realesrgan-ncnn-vulkan (x4plus 4x | animevideov3 2x)
   ─► UpscaleImages.Output (crop, box to 2x, the source's alpha: hard, soft for sprites and 2D)
   ─► mods/hd-textures/{textures,images}/LEVELn/NAME@key.png + manifest.txt + mod.txt

 use: as any mod (ModImages: the key matches the texture through that palette only)
```

- The key hashes the size, frames, indices and the colours of the used indices: arenas whose
  palettes differ elsewhere share an image; changed game files find none (the original is used).
- A level's `ModImages` forgets its decoded files once the level is made (`Trim`), and after its
  first frame one compacting collection frees them (`Viewer.Settle`): level 3 with HD textures,
  GC heap 331 ─► 127 MB, live 315 ─► 100 MB. The menus' images stay decoded (screens remake them).
- `Renderer.Replace` keeps the image until it's on the GPU, then only its size; a texture or
  palette change (a bullet hole) drops it and expands the original again. UVs are unchanged (same
  aspect).
- The manifest records the format, the model and the scale; another format is ignored, another
  model or scale makes everything again. The loader ignores it (the generator's).
- An older build's `textures-hd/` is moved into the mod at start (`HdGenerator.Migrate`; its
  `textures=Hd` setting becomes `mod.hd-textures=On`).

## Mods

The user folder's `mods/<name>/` (`docs/modding.md`): textures, 2D images and models replacing the
game's in the enhanced look; the original look never reads them. `Mods/`:

```
 ModCatalog (mods/: mod.txt, priority order; Settings mod.<folder>=Off; --mod)
   ─► ModImages (a level's: textures/ and images/ by key, LEVELn/, name; PNG decoded, premultiplied)
   │    ├─► MaterialResolver (Lit textures) ─► Renderer.Replace ─► colour texture
   │    ├─► KurtSprite (frames ANIM_n) ─► Renderer.Replace
   │    └─► CanvasImages (HUD, sniper, bomber, fall, stream, menus, fonts) ─► Renderer.ReplaceImage
   └─► ModModels (models/: Glb ─► ModelSwap per model: parts by node name, glTF Y up ─► MDK Z up)
         ─► ObjectView: replaced parts' own batches; each draw PartFit (the original part's pose as
            one affine fit: rigid exact) moves them; collisions, hits, scripts keep the original
 --export-assets: AssetExport (TextureExport, CanvasExport, ModelExport ─► Glb) ─► the same layout
```

- First enabled mod with a file wins; within one: `NAME@key` (exact view), `LEVELn/NAME`, `NAME`.
- `Renderer.ReplaceImage`: a canvas image (index texture × palette) drawn from an RGBA image of any
  size in the enhanced canvas (`MODE_IMAGE`: filtered, mipmapped, clamped: sharp at the window's
  resolution); kept through palette changes. The original look's canvas ignores it.
- A replaced part's images become surfaces of their own (`MaterialResolver.Image`: a 1 x 1 index
  texture standing for the RGBA image); a material named as an original one uses it; else a flat
  colour. Parts are shaded flat (normals ignored).
- The console's `mods` and F3 report what a level replaced (`ModReport`).

## Loading and frames

A level loads everything it draws and plays, so its frames make nothing: no GPU resource, no
texture expansion, no decoding, (almost) no managed allocation, so no garbage collection.

```
 load (Viewer)                                          a frame
 ─────────────                                          ───────
 every archive's index textures, palettes ─► GPU        draws of existing meshes and textures
 arenas' meshes, colour textures (enhanced) ─► GPU      posed models ─► Renderer.Stream (one buffer)
 every model × animation that moves it: poses, boxes    effects, canvas ─► dynamic meshes (kept)
 models' layouts (by surface), Kurt's sprite frames     uploads ─► one kept staging buffer (cycled)
 every sound converted, every script decoded            lists, pools: kept from frame to frame
```

- Objects (`ObjectView`): each model is laid out once per palette (batches by surface: part,
  vertex, UV per corner); every frame its pose (shared, baked at load: `ScriptRuntime.PreparePoses`,
  `ModelAnimation.Animates`) is written into the renderer's per-frame vertex stream.
- Kept, not made per frame: debris pieces (a pool of 600), sound voices (both mixers), insets,
  the ticks' object copies (`ListCopy`), scratch lists. No LINQ, no `foreach` over interfaces
  and no lambda capturing a parameter (made at the method's entry) on a frame's path.
- Still allocated: objects spawned by the scripts (`MdkObject`, about 1 KB each: shots, fire
  sprites, explosions) and the first use of something the load missed (a level model drawn in
  another arena's palette); `tests/alloc_test.sh` keeps a level's frames under 512 bytes.
- Hidden, frames are paced by the last frame's fence (a shown window by its swapchain): no
  frames pile up in memory. `--perf` waits for the GPU each frame and prints the frame costs.

## Game flow

`Flow/Game.cs` runs one screen (`IScreen`) at a time; each frame returns an `Event` and the game
picks the next screen. A screen's textures and meshes are freed when it ends (`Renderer.Mark`,
`Renderer.Release`); the 2D screens draw the original's 600 x 360 view fitted on the canvas
(`Menu/Ui.cs`, `ScreenView`): inside the window's safe area (`Renderer.CanvasSafeArea`: no
cut-out or rounded corner over it), whole, with black bars; only the loading screen fills a 4:3
canvas's height as before (its sides cut). The 3D cameras keep the 4:3 view's width on narrower
screens (`CameraMath.KeepWidth`: Vert-, a near-square foldable sees more above and below) and
widen on wider ones (Hor+); sniper mode's scope keeps its 2D-aligned projection
(`tests/aspect_test.sh`).

```
 splash ─► main menu ─new game─► briefing ──fall──► loading ─► level (Viewer)
              ▲   └─continue / saved game──────────► loading      │ tornado (EndLevel)
              │                                                    ▼
              │                           index 0-3: stream ─► statistics ─► save prompt ─► briefing ──fall──► next level
              │                           index 4:   Gunter stream ─► save prompt ─► LEVEL5
              ├── Kurt died (LASTGAME) ◄──────────────────────────┘
              └── end movies (event 81) ◄── LEVEL5
```

## Developer tools

The console (Grave, the key left of 1 by its scancode) and the debug overlay (F3) of every screen
(`DevTools/`; `DevUi` and `GameCommands` in the `Game`, `LevelCommands` in the `Viewer`):

```
 Console.WriteLine ──► LogWriter ──► stdout / stderr (unchanged; tests parse them)
                               └──► LogRing (500 lines) ──► ConsoleView
 Input (RawKey, typed text) ──► DevConsole ──Enter──► CommandRegistry ──► ConsoleCommands (parse, check)
                                   │ Tab, Up/Down        (help, clear)          │
                                   ▼                                            ▼
                             CommandHistory                     ICommandTarget: GameCommands (settings, saves,
                                                                session god/noclip, time scale; TakeNext:
                                                                map, load, quit) ──► ILevelTarget?: the
                                                                Viewer's LevelCommands (Kurt, scripts)
 Profiler (scene, render, physics, scripts, audio), Renderer.Stats (GPU wait), GC,
 TechInfo.Overlay (GPU, window, frame), IScreen.Status (the screen's lines) ──► OverlayText ──► OverlayView
```

- `TechInfo` formats the start's log lines (build, GPU and backend, look) and the display's
  (desktop mode, window, render target, swapchain and depth formats, MSAA, present mode), logged
  again by `Game.WatchScreen` when they change (compared each frame without allocating).

- One hook for every screen: `Game.Run` updates `DevUi` before each screen's frame, and
  `Renderer.Overlay` draws the overlay and the console on the canvas inside any `Present` (the
  screens', the stream's and fall's direct ones, the loading screen's).
- While the console is open (and on Grave's frame) `Input.Withhold` empties the frame's input:
  the screen gets no presses, typing, clicks, mouse or held keys (the tests' held keys still
  apply), so typing reaches neither Kurt, the menus nor the cheats.
- The level's commands (`tp`, `pos`, `give`, `health`, `kill`, `save`) answer "Not in a level"
  elsewhere. God and noclip live in `DevSession`: every new level's Kurt gets them. `timescale`
  scales every screen's frame time.
- The engine's parts are generic: `RawKey` (fixed keys by place, auto-repeat included),
  `Profiler` sections, `RenderStats` (GPU draw calls and triangles of the last frame), the log.
- `Kurt.Mortality` (god: no damage, no death by falling out) and `Kurt.Clipping` (noclip: the
  walking keys move him freely). `ArenaStops` finds a floor of an arena, no other arena's above it (the soak tour, `tp`,
  `map`); `GameState.StartArena` carries `map`'s arena to the next `Viewer`.
- `--console="..."` opens the console on the first screen and runs the lines; in a level once
  Kurt's arena is known (after `--delay`) (`tests/console_test.sh`).

## The 1996 beta demo

The three levels of `MDKDEMO.EXE` (6 August 1996; godot-mdk `docs/beta96.md`, `mdk_beta.gd`,
`beta_script_decoder.gd`) are numbered 961, 963 and 966. `BetaDemo` (found by `MDK_BETA_DIR`, `beta`
in `mdk_paths.cfg` or a `BETA96` folder) reads their loose files into the retail loaders' objects,
so the game runs them unchanged:

```
 LEVELn.SET, .CON, ARENAS/*.HOT ──► Dti         ARENAS/*.BSP (16-char names, 36-byte nodes) ──► Arena
 LEVELnO.MTO, LEVELnS.MTI, *.LBA ──► TextureArchive (no header, index 0 made black)
 LEVELn.CMI ──► Cmi (Dialect Beta1996: paths ──► retail splines, animations ──► ParseBeta)
 *.SNI ──► Sni         SPRITES/*.ABB, HUD/*.LBB ──► over TRAVSPRT.BNI's entries
```

- Scripts: `ScriptDecoder.For(cmi)` decodes the demo's bytecode into retail instructions
  (`ScriptDecoder.Beta.cs`); its own opcodes (`BetaOpcodes`: follow_path, fire, the alarm-ended
  condition) run in `ScriptVm`.
- Triangles flagged 2 (`Arena.ClipFlag`) aren't drawn and stop Kurt, not the scripts' rays
  (`Bsp.Clip`).
- `ScriptRuntime.Beta.cs`: no town timer, Kurt's arena from the arenas' boxes, the part the chain gun
  hits. `Kurt.Beta.cs`: rolls (Z, C), the helmet, backing up. `Viewer`: the teleports (T or Alt and a
  digit), the demo's sprites, `LevelMusic.Ambience`. `MainMenu`: the "Beta Levels" page.
- Not checked against the demo (it isn't on the development machine): the BSP nodes' last four
  `s16` are taken as padding after the retail six (`BetaDemo.CheckNodes` warns when nodes don't hold
  their triangles); the rest follows the Godot port, tested on synthetic files.

## Open questions

None open. Answered (Ghidra): which objects Kurt walks into or stands on
(`MdkObject.Footing`).

- Wall: neither 0x10 nor 0x800 (`damp_collide_move` 0x465e34 skips `flags & 0x810`; XY only).
- Floor: 0x100 and not 0x10 (`damp_platform_floor` 0x41d2c4).
- 0x800000 decides neither: landed on such a platform, Kurt keeps it without the floor scan
  (`damp_gravity` 0x469efc sets 0x573b8c): each step his XY is its XY, whatever the walls, and he
  can't walk (`Kurt.Keeper.cs`). E.g. level 3's hover ship XGS flies him through HMO_9's opening.

  | flags             | walls | floor | e.g. (level)                               |
  |-------------------|-------|-------|--------------------------------------------|
  | none              | yes   | no    | grunts, doors, turrets                     |
  | 0x100 (+0x800000) | yes   | yes   | XPGUN (3), XBGUN, XTR (6), XTANK (7)       |
  | 0x800 + 0x100     | no    | yes   | XWINCH (3), XSNOWB 0x800900 (4)            |
  | 0x10, or 0x800    | no    | no    | pickups, bolts, X4_TOWER (5), XFORK (8)    |

## Roadmap

1. ✅ Formats, level viewer (arenas, corridors, textures, glass).
2. ✅ Sky panorama, mirrors, triangle groups (state kept in the triangle flags, as the original).
   Animated textures (`Scripts/AnimatedTextures.cs`, opcode 133), the scripts' sky modes
   (`Level/SkyModes.cs`, the renderer's `Backdrop`), the glass panes' outlines (`Level/Outlines.cs`,
   line primitives; the enhanced look skips flagged edges inside a flat pane), ropes (`Objects/RopeView.cs`), bullet holes (`Scripts/BulletHoles.cs`,
   re-uploaded textures).
3. ✅ Collision: the original's BSP (`Collision/Bsp.cs`, from godot-mdk `docs/bsp.md`).
4. ✅ Sound mixer (SDL audio stream, the original's volume, distance, Doppler and pan laws), the
   arenas' music and its fades (`Audio/LevelMusic.cs`).
5. ✅ Script decoder (every script of the game decodes like the reference disassembler).
6. 🟡 Kurt: walking, turning, jumping, chute, falls through the BSP (`damp_collide_move`,
   `damp_gravity`), solid objects and platforms (`Collision/Solids.cs`), follow camera, sprite,
   chain gun, muzzle flash, hits, knock-down, death, items (`Kurt/Kurt.Combat.cs`,
   `Kurt/Inventory.cs`), the fans' updrafts (`Kurt/Kurt.Updraft.cs`), sniper mode
   (`Kurt/Kurt.Sniper.cs`, `Kurt/Scope.cs`: look, zoom, clip, the scope's off-centre projection).
   The camera's climb term (−40 × the smoothed rise per tick), the rides (`Scripts/Rides.cs`,
   `Snowboard.cs`, `Bomber.cs`, `Kurt/Kurt.Ride.cs`, `Hud/BomberOverlay.cs`; Kurt's moves report
   the objects and triangle groups they touch; the board steers with the turn or strafe keys or
   the mouse, and goes into the arena Kurt rides into, flag 0x80000). Ledges (`Kurt/Kurt.Ledge.cs`), sliding in the
   wind tunnels (`Kurt/Kurt.Slide.cs`), hard landings and falling out of the arena, the camera's
   roll (`Kurt/CameraRoll.cs`), clearance and shake (`Kurt/FollowCamera.cs`,
   `Collision/ArenaSpace.Probes.cs`). Each step the camera follows Kurt after the scripts (a
   platform has carried him: `game_frame`'s order), and the XD2 and the XE are put with Kurt
   (`Rides.Follow`), so nothing is drawn a step apart (`tests/carry_test.sh`, `--trace`).
   Frames between steps (screens above 60 Hz, `FrameBlend.cs`): Kurt and the follow camera are
   drawn between the last two steps, the objects and the cutscene camera between the last two
   ticks, by the time left over; Kurt is drawn with what carries him; teleports, arena changes
   and cuts snap. The mouse turns Kurt and the camera (and the scope) every frame. Animations
   are drawn between their frames (a loop goes on to its first; restarts snap), the flying
   pieces and sprite effects between ticks (their sprites' frames and the ropes stay at the
   tick rate). In sniper mode the zoom, the tilt and the clip are drawn between steps, the
   rounds and their cameras between ticks; tests' frames are steps, unblended.
7. 🟡 Script VM and objects (`Scripts/`: ports of `script_vm.gd`, `script_runtime.gd`,
   `object_motion.gd`, `object_behaviors.gd`; 30 ticks per second, object moves are BSP box sweeps;
   the arenas' own scripts run after the objects, as in `game_frame`).
   Kurt's items and blasts are ported (`Items.cs`, `Twister.cs`; twisters drawn as ribbons, `Ribbon.cs`), so are the
   effects (`Effects.cs`: wounds, slime drops, bubbles, smoke trails), the flying pieces
   (`Debris.cs`: sparks, shattered groups, break-ups) and the fans (`Fans.cs`), drawn by
   `Objects/EffectsView.cs`, the sniper rounds, their target lock and `bomb_follow_path`
   (`SniperRounds.cs`), Bones' air strike (`AirStrike.cs`) and the full-screen strike
   (`StrikeScene.cs`), the end of a level (`EndLevel.cs`: the torn triangles are drawn by
   `LevelView.DrawEnd`), full saves (`Snapshot.cs`, `ScriptRuntime.Snapshot.cs`, `Kurt.Snapshot.cs`:
   JSON in the save, objects' public fields by reflection).
8. 🟡 Weapons, items, HUD, sniper mode: the chain gun, the items and the HUD (`Hud/`: health panel,
   inventory, messages in the original fonts, health bar, flashes, drawn on the renderer's 2D
   canvas) are done, and sniper mode's screen (`Hud/SniperOverlay.cs`; the round cameras and the
   loaded rounds are renderer insets, `SniperView.cs`), bullet holes on textures (`special_130`).
   Cutscenes look through `CutsceneCamera.cs`.
9. 🟡 Menus, saves, level flow, videos (`Flow/`, `Menu/`; ports of `main_menu.gd`,
   `menu_items.gd`, `pause_menu.gd`, `save_prompt.gd`, `stats_screen.gd`, `slideshow.gd`,
   `intro_splash.gd`, `loading_screen.gd`, `game_state.gd`, `settings.gd`, `video.gd`,
   `end_movie.gd`): the splash, the main menu over `MDK12.FLC` and the slideshow, options and key
   bindings, saved games (the Godot port's JSON), "Continue" after a death, the pause menu, the
   loading screen, the end of a level (`EndLevel`: Kurt rises, the white flash), the
   intermission, debriefing, Score-O-matic (its heads: `Menu/HeadsView.cs`), save prompt and
   briefing, the end movies, F2's full saves, the original's mouse cursor (`Menu/MenuCursor.cs`,
   `Window.SetCursor`), the `SEETHEWHOLEGAME` debug keys.
10. 🟡 Fall, stream, bomber, snowboard sequences. The rides are done (see 6). The stream is done (`Stream/`; ports of
    `stream.gd`, `stream_tube.gd`): the tube from Watcom's `rand()` (`--stream` uses seed 1, as the
    Godot reference test), Kurt's steering, drift and walls, the lights, the bonus, Bones' rescue,
    the Gunter tube and the planet, the fades. Unknown: the lights' blend (added). The fall is done
    (`Fall/`; ports of `fall.gd`, `fall_missile.gd`): the intro in space, steering, the camera, the ground with the crawler's
    track and the haze (quads far below the camera), radars, missiles, pickups, Bones, the palette
    effects, the HUD; health and pickups go on to the level (`GameState.Carry`). Pickups are only
    tested under their chute, as the original (0x41275c, as fall.gd).
    Approximations: the radar's colours, the smoke trails.
11. ✅ Enhanced look (`Renderer.Enhanced.cs`, `shaders/enhanced.hlsl`, `post.hlsl`, `depth.hlsl`):
    filtered textures, sky, sprites and 2D screens, each arena lit from its level's sky (sun with
    shadows outdoors, hemisphere light, exposure, tone curve), point lights (muzzle flashes,
    explosions, fires), ambient occlusion, glow, haze, dithering; mipmapped, anisotropic textures;
    anti-aliasing in both looks, HD textures (Real-ESRGAN, made locally). Still to do: level lamps,
    occlusion and haze in the insets (sniper mode).
