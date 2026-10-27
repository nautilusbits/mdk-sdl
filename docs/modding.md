# Modding

Mods replace the game's textures, 2D images (HUD, menus, fonts) and models with your own files.
They apply to the **enhanced look only**; the original look always draws the game's own data,
pixel for pixel. (Reason: the original look is the 1997 renderer's palette pipeline; RGBA images
of any size and new meshes don't fit it, and a faithful mode should stay faithful.)

## Where mods live

```
mdk_sdl.exe
mods/
  my-mod/                 one folder (or zip) per mod (names starting with "." are ignored)
    mod.txt               optional: name, author, version, description, priority
    textures/             3D textures: arenas, objects, Kurt's sprite frames
      WALL1.png           every level
      LEVEL3/WALL1.png    LEVEL3 only
    images/               2D images: HUD, sniper screen, menus, loading screens, fonts
      SC_STAT.png
      LEVEL3/SC_STAT.png
    models/               object models (glTF binary)
      XG.glb              every level
      LEVEL3/XG.glb       LEVEL3 only
```

`mods/` is next to the program (the user folder: `MDK_USER_DIR` overrides it; on Android the
app's `files` folder, filled by Options, Mods, "Import from folder" from a `mods` folder next to the
game files, zips too).

`mod.txt` (`name=value` lines, `#` comments, all optional):

```
name=Clean walls
author=Me
version=1.0
description=Cleaner walls for LEVEL3
priority=10
```

### Zipped mods

`mods/my-mod.zip` works as the folder `mods/my-mod/` would: same name in the Mods page and
`settings.cfg`, same files. `mod.txt` sits at the zip's root, or in its one top folder
(`my-mod/mod.txt`, `my-mod/textures/...`: a zipped folder). Paths ignore case.

- A folder and a zip of one name: the **folder wins**; the output says
  `Mod my-mod: the folder wins over my-mod.zip`.
- Read in place, never unpacked to disk. Ignored: entries leaving the mod (`..`, absolute paths,
  drive letters, backslashes); entries over 256 MB unpacked or unpacking over 100 times their
  packed size (above 1 MB: zip bombs). A zip of over 100,000 entries or 2 GB unpacked, or a
  broken one, is no mod. Each is named in the output.
- The game keeps a zip open after reading it; Options, Mods lets go of it.
- In any mod, a PNG over 8192 pixels wide or tall is skipped (its original stays).

## Switching mods on and off

- Options, **Mods**: every mod found, On/Off. A new mod is on. Saved in `settings.cfg` as
  `mod.<folder>=Off`. Textures and models take effect from the next level, 2D images on the next
  screen.
- `mdk --mod=a,b`: only these mods (folder names) for this run (tests).
- Console: `mods` lists the mods (on/off, priority) and, in a level, what it replaced. F3 shows
  `mods N: X images, Y models` in a level.

## Which file wins

1. Mods by **priority**, highest first (default 0; ties by folder name). The first mod with a file
   for something wins.
2. Within a mod: the exact view (`NAME@<key>.png`, see HD textures), then the level's folder
   (`LEVEL3/`), then the mod's top folder (all levels).
3. Nothing found: the game's own (the original texture through its palette).

The HD textures (below) are a mod with priority -100: any other mod overrides them.

Names are the game's, case ignored. Start from an export.

## Getting the originals: `--export-assets`

```
mdk --export-assets=C:\mdk-export
```

Writes, from your own game files (about 40 MB, 10 s):

```
textures/LEVELn/NAME.png       each level's textures (first arena's palette that shows them)
                               animated: NAME_0.png, NAME_1.png...   Kurt: K_RUN_0.png...
images/LEVELn/NAME.png         the level's HUD, sniper screen, bomber sight (its palette)
images/NAME.png                menus (MDKOPT), loading screens (LOAD_3...), the falls' HUD, fonts
models/LEVELn/NAME.glb         each level's models: parts as named nodes, textures embedded
```

Copy what you change into your mod, keeping the names. Never export into a folder you publish
as-is: see the legal note.

## Textures and images

- PNG, 8-bit RGB or RGBA (grey works), not interlaced. Any resolution; keep the original's aspect
  ratio (UVs are unchanged: a 64 x 32 texture should stay 2:1, e.g. 512 x 256).
- Alpha is honoured: 3D surfaces and sprites cut it at half cover; 2D images blend it.
- Animated textures: either one PNG per frame (`NAME_0.png` ... `NAME_<n-1>.png`, all one size), or
  one strip of all frames stacked downwards (`NAME.png`, height = frames x frame height).
- 2D images are drawn at the window's real resolution: a 4x image is 4x sharper on a big window.
- Fonts: `images/FONTBIG.png`, `images/FONTSML.png` replace the whole glyph atlas (the export's
  layout, scaled: the glyphs must stay where they are).
- Shadows and the depth pass still cut along the original texture's transparent texels.
- Exact view, `NAME@<key>.png` (any subfolder): replaces the texture only as seen through one
  palette (`<key>`: a hash of the texture's size, indices and the colours it uses). The HD
  textures use this; hand-made mods rarely need it.

Not replaceable: the sky, effects' sprites (explosions, sparks), the fall's and the stream's 3D
views, videos, the statistics pages are by name only (not exported).

### Example: replace a floor texture (LEVEL3's start)

```
mdk --export-assets=C:\mdk-export
mkdir mods\clean-walls\textures\LEVEL3
copy C:\mdk-export\textures\LEVEL3\H1_FLR.png mods\clean-walls\textures\LEVEL3\
(edit it, e.g. upscale to 4x and repaint)
mdk --level=3 --enhanced
```

## Models

One format: **glTF 2.0 binary (`.glb`)**, the subset below (Blender exports it: File, Export,
glTF 2.0, format "glTF Binary").

### How MDK models work

A model has **parts** (named meshes: `XG1_HEAD`, `XG1_ARML`...); animations move each part's
vertices (rigid or deforming), scripts hide parts, hits find the part struck. A mod replaces a
part's **look** only:

- Each mesh **node named as a part** (case ignored) replaces that part. A model of one unnamed
  part: name the node as the model (`BARREL`).
- Parts without a node keep the original. Replace just the head, keep the rest.
- A replaced part follows its original through every animation: each frame, the original part's
  move from its rest pose is fitted as one transform (exact for rigid parts, approximate for
  deforming ones) and applied to your mesh.
- The original geometry still collides, is hit and is what the scripts see (yours is drawn and
  casts the shadows). Keep your part about the original's size and place.
- Hidden parts hide yours too.

### Units and axes

- 1 glTF unit = 1 MDK unit (a grunt is about 8.5 units tall).
- MDK is Z up; glTF is Y up. The export converts (MDK x, y, z = glTF x, -z, y), so Blender shows
  the model upright. Model in the model's own space, rest pose (as exported); node transforms
  (including parents') are applied.

### The subset read

- Nodes with meshes; primitives of triangles; `POSITION`, `TEXCOORD_0` (float, or normalized
  unsigned), indices (8, 16, 32-bit). Normals are ignored: the enhanced look shades flat.
- Materials: `baseColorTexture` with an **embedded** PNG (bufferView, `image/png`), else
  `baseColorFactor`. A material without an image **named as an original material** (`XG_BOD`,
  `PEN_16`, `GLASS1`) uses the game's (its texture, animated or not, glass, mirrors). Exported
  models embed their textures: remove the image to keep the original's.
- Not read: skins, morph targets, animations, cameras, lights, external files, `.gltf` with
  separate files, sparse accessors, KTX/JPEG images.

### Example: replace the grunt's head

1. Export, open `models/LEVEL3/XG.glb` in Blender.
2. Replace the mesh of the node `XG1_HEAD` (keep the node's name); delete every other node.
3. Export as glTF Binary to `mods/big-heads/models/XG.glb` (+Y up: Blender's default).
4. `mdk --level=3 --enhanced`; the console's `mods` lists `models: XG`.

## Checking a mod

- `mods` in the console (the key left of 1): mods, on/off, priority; in a level, the images and
  models replaced.
- The program's output: `Mods: mods 1: 96 images, 1 models; textures 123 of 123`, and why a file
  was skipped (`Mod image ...`, `Mod model ...: no part TAIL in XG`).
- F3: `mods N: X images, Y models`.
- A screenshot without a window: `mdk --level=3 --enhanced --mod=my-mod --hidden --screenshot=shot.bmp`.

## HD textures

Options, Mods, "Make HD textures" (desktop; `mdk --upscale-textures`) makes the mod
`mods/hd-textures/`: the levels' textures, Kurt's frames and the 2D images (not the fonts) upscaled
by Real-ESRGAN from your files, each named `NAME@<key>.png` (only the exact view it was made
from). Switch it on the Mods page. Older builds' `textures-hd/` is moved into it at start.

The upscaler's archive comes from `settings.cfg` (empty: Real-ESRGAN v0.2.5.0 for the platform):

```
upscaler_url=https://example.org/realesrgan-ncnn-vulkan-windows.zip
upscaler_sha256=<the archive's SHA-256, hex>
```

A URL needs its checksum; a local path or file URL works offline.

## Legal

Mods must not redistribute the game's files or anything made from them (exports, upscaled
textures): share only your own artwork. Everyone makes their exports and HD textures from their
own copy of MDK.
