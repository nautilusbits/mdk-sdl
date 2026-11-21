# MDK file formats

The original game's data files (MDK, Shiny Entertainment, 1997), reverse engineered from the data
and from `MDKD3D.EXE`. Each section names the parser in `src/formats`. How the engine uses
the data is in [original-engine.md](original-engine.md).

Conventions:
- Little-endian. `u8/u16/u32/s16/s32` are integers, `f32` an IEEE float.
- Names are ASCII, NUL-padded to a fixed length; `pstr` is `u8 length` (with the NUL), then the
  characters.
- World coordinates are Z up, angles in degrees (yaw 0 = +X, 90 = +Y).
- 🟡 marks fields that aren't fully understood.

## Where the files are

```
TRAVERSE/
  TRAVSPRT.BNI          Kurt's sprite animations, HUD and sniper sprites
  TRAVERSE.SNI          shared sounds
  LEVELn/               n = 3..8
    LEVELn.DTI          level settings, arenas and their records, palette, sky
    LEVELnO.MTO         arenas: textures, models, animations, sounds, geometry
    LEVELnS.MTI         level textures (aliens, corridors, effects)
    LEVELn.CMI          scripts, global models, arena music
    LEVELnO.SNI         arena music, corridor geometry
    LEVELnS.SNI         level sounds, extra Kurt animations
FALL3D/                 the fall before each level
STREAM/                 the tunnel between levels
MISC/                   menus, fonts and texts, statistics, loading screens, videos
```

The levels are played in the order 7, 6, 3, 4, 8, 5.

## Common header

Most files start with:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0x00 | u32 | file size − 4 |
| 0x04 | char[12] | internal name (`LEVEL3O.MAT`, `LEVEL3.CMD`…) |
| 0x10 | u32 | file size − 16 |

The game loads files without their first 4 bytes, so most offsets stored in files count from file
offset 4.

## Palettes (`Palette`)

All graphics are 8-bit, palette-indexed. An arena's 256 colours (RGB, 8 bits each) are put
together from three sources:

```
 0..63    system colours (SYS_PAL in MDKFONT.FTI): HUD, fonts, Kurt
 64..175  the arena's 112 colours (MTO)
 176..255 the level's colours (DTI)
```

Index 0 is black, and transparent in sprites.

## DTI: level data (`Dti`)

After the header, 5 `u32` block offsets.

| Block | Contents |
| --- | --- |
| 0 | Settings: the start arena, Kurt's start position and yaw; the sky (fill colours above and below, horizon row, offset, width for 360°, height); a second panorama's fill colours (levels 5, 6, seen only in mirrors); `GLASS1`–`GLASS4` colours (R, G, B, alpha) |
| 1 | `u32 count`, 24-byte entries 🟡 |
| 2 | Arenas: `u32 count`, then `char[8] name, u32 records offset, f32 camera pitch` |
| 3 | `u32 112`, then 256 × RGB: the level palette |
| 4 | The sky panorama(s): `height` rows of `width + 4` pixels |

**Arenas and corridors.** A level is a chain of arenas (`HMO_1`, `MEAT_3`, `DANT_7`…), the big
playable spaces, and corridors (`CHMO_1`…, the same name with a `C`) between them. Each has a list
of 36-byte records:

```
u32 type, s32 id, f32 angle, f32 x, y, z, char[12] name
```

| Type | Record |
| --- | --- |
| 2 | An alien placed in the arena; `name` is its script in the CMI |
| 5 | A cover spot, used by aliens hiding from Kurt |
| 6 | A connection: a doorway between an arena and its corridor |
| 8 | A waypoint aliens jump between |
| 1, 3, 7 | 🟡 |

Connections come in pairs with the same id (1000+): one in the arena, one in the corridor. In a
connection, `angle` holds an integer, the direction Kurt crosses to leave (0 −x, 1 +x, 2 −y, 3 +y,
6 −z, 7 +z). The doorway's two corners are `x, y, z` and the 12 name bytes read as 3 floats.

## MTO: arenas (`Mto`, `Arena`)

`LEVELnO.MTO`: `u32 count`, then `char[8] name, u32 offset` per arena. At each offset, `u32 size`
and a block:

| Offset | Contents |
| --- | --- |
| 0x00 | offset of the models section |
| 0x04 | offset of the palette (112 × RGB) |
| 0x08 | offset of the world section |
| 0x0C | size of the texture archive |
| 0x10 | the arena's texture archive (`HMO_n.MAT`, see MTI) |

The original streams arenas in 32 KiB chunks as Kurt nears them.

### Models section (`Model`)

`u32 animation count, u32 model count, u32 sound count`, then `char[8] name, u32 offset` per
animation and model, then 24-byte sound entries (like SNI's).

A model:

```
u32 flags                           0: one part, 1: named parts
u32 material count, char[16] × count
[flags] u32 part count
per part:
  [flags] char[12] name, f32 pivot[3]
  u32 vertex count, f32[3] × count
  u32 triangle count, 36-byte triangles (as in the world)
  [flags] f32 box[6]
f32 box[6]                          xmin, xmax, ymin, ymax, zmin, zmax
u32 reference point count (≤ 8), f32[3] × count
```

Part vertices are in model space; every part shares the object's transform. Reference points mark
places such as gun muzzles or camera spots.

### Animations (`ModelAnimation`)

```
f32 speed                           1.0 = 30 frames a second
u32 track count T, u32 frame count F
u32 track offsets[T]
f32 root motion[F][3]               the object's move per frame
u32 reference point count R, f32[R][F][3]
```

A track animates the model part of the same name (case-insensitive). It comes in two kinds:
- **Delta tracks** deform vertices. A base pose, then per frame `s16 frame` and an `s8` delta per
  vertex, scaled by the track's `f32` scale. Frames must be applied in order.
- **Matrix tracks** move rigid parts. The scale's bits are 0, and each frame stores an `s16` 3 × 4
  matrix in fixed point.

### World section (`Arena`)

1. Material names: `u32 count`, `char[10]` each, padded to 4 bytes.
2. BSP nodes: `u32 count`, 44 bytes each (plane, children, triangle lists).
3. Triangles: `u32 count`, 36 bytes each:
   `s16 v0, v1, v2, s16 material, f32 u0, v0, u1, v1, u2, v2, u32 flags`.
4. Vertices: `u32 count`, `f32 x, y, z` in world coordinates.
5. BSP leaf data.

The triangle fields:
- `material` ≥ 0 indexes the names. A negative value is a palette colour (`−material`) or, from
  256 up, a special material (MTI).
- UVs are in texels.
- `flags`:
  - The top byte is the triangle group, which scripts break, hide or animate.
  - 0x2: hidden at the arena's start.
  - Bit 23 outlines the triangle along the edges of bits 20–22 (glass frames).

## MTI / MAT: textures (`TextureArchive`, `Texture`)

`char[12] name, u32 size, u32 count`, then 24-byte entries
`char[8] name, u32 kind, u32 value, f32 —, u32 offset`.

| `kind` | Entry |
| --- | --- |
| 0, 2 | Texture: `u16 width, u16 height`, palette indices row by row |
| 1 (bit 0) | Same, with index 0 transparent (`EXPLODE`, `TRAIL`…) |
| 0x10000, 0x10001, 0x20000 | Animated: `u32 frames, u16 width, u16 height`, then frames; 0x20000 stores deltas from a base frame (`AnimatedTextures`) |
| 0xFFFFFFFF | No image: a palette colour or a special material (`value`) |

Special materials:

| Value | Names | Drawn as |
| --- | --- | --- |
| < 256 | `BLACK`, `PEN_n`… | a flat palette colour |
| 256 | `NONE` | (only on hidden triangles) |
| 990–1010 | `MIRRLOW/MED/HIGH`, `PEN_990`… | a "mirror": the sky panorama in screen space, shifted by the value; not a reflection |
| 1024–1027 | `GLASS1`–`GLASS4` | translucent colour from the DTI |
| 1028 | `RIPPLE` | water that wobbles the background (software renderer only) |

## BNI: sprites and misc (`Bni`, `SpriteAnimation`)

`u32 size, u32 count`, then `char[12] name, u32 offset` per entry. An entry's size is the
distance to the next offset; its content depends on the name.
- **Images**: `u16 width, u16 height`, then pixels. Menu backgrounds put a 768-byte palette first.
- **Sprite animations** (Kurt's `K_*`, `PICKUPS`): `u32 size, u32 frames, u32 offsets[frames]`.
  Each frame is `u16 width, height, s16 hotspot x, y`, then run-length rows:

  | Byte | Meaning |
  | --- | --- |
  | 0x00–0x7F | n + 1 literal pixels follow |
  | 0x80–0xFD | the next pixel repeats n − 0x7C times |
  | 0xFE | end of row |
  | 0xFF | end of frame |

  Index 0 is transparent.
- Sounds (RIFF WAV), models and palettes in some archives (`STATS.BNI`, `FALL3D.BNI`).

Kurt is a 2D sprite, not a model: the game draws these frames over the 3D view.

## SNI: sounds (`Sni`, `Wav`)

Header, `u32 count`, then `char[12] name, u16 flags, u16 volume, u32 offset, u32 length`.
- Entries are RIFF WAV: PCM, mono, 8 or 16 bits.
- Flags: 1 loops; 3 marks music (an arena's theme, `CORRIDOR`).
- `volume` is the default, 0–0x7FFF.
- Some headers are sloppy, so only the `fmt ` and `data` chunks are read.

In `LEVELnO.SNI`, the entries named like corridors (`CHMO_1`…) aren't sounds. They hold the
corridor's geometry in the arena world layout. `LEVELnS.SNI` also holds Kurt's extra animations for
the level: snowboard (`K_SURF`), slides.

## CMI: scripts (`Cmi`, `Scripts/ScriptDecoder`)

`LEVELn.CMI` (internal name `LEVELn.CMD`). After the header come 4 directories, each
`u32 count` then `u8 length, char name[length], u32 offset`:

| # | Directory | Example |
| --- | --- | --- |
| 0 | alien instance scripts | `HMO_1$XG_0` (alien `XG` #0 of arena `HMO_1`) |
| 1 | global models (offset 0: look in the arena) | `XG` |
| 2 | object type scripts, run when an object is created | `HMO_1$XH1_DOOR` |
| 3 | arenas: `pstr`, `pstr music`, `u32 script` | `HMO_1` |

The rest is bytecode for the script VM ([original-engine.md](original-engine.md#scripts)), plus the
data it points to:
- **Animation references**: `u32 0`, then a name looked up in the arena.
- **Spline paths**: `u32 key count`, then 40-byte keys:
  `s32 frame, f32 position[3], f32 in tangent[3], f32 out tangent[3]` (cubic Hermite).

The 1996 beta demo uses an earlier bytecode with the same structure (`Cmi.Beta`,
`ScriptDecoder.Beta`).

## FTI: texts and fonts (`Fti`, `Font`)

`MISC/MDKFONT.FTI`: `u32 size, u32 count`, then `char[8] name, u32 offset`.
- **Texts**, NUL-terminated: menu strings (`OPT*`, `OM_*`), key names (`KM_*`), HUD messages,
  debriefings and briefings (`DEB*`, `BRIEF*`). These use layout codes (`\c` centre, `\20n` …),
  drawn as the text types out.
- **`SYS_PAL`**: the 64 system colours.
- **`FONTBIG`, `FONTSML`**: 256 `u32` glyph offsets (0 = none), then per glyph
  `s8 ascent, s8 descent, u8 width` and its pixels. `FONTSML` has key-name glyphs (`Esc`, `F1`,
  arrows) at control codes.
- **`F8`**: an 8 × 8 bitmap font.

## Other files

| File | Contents | Parser |
| --- | --- | --- |
| `MISC/OPTIONS.BNI` | `MDKOPT` menu background, `MAINSONG` menu music, `INTRO1A` the splash (RLE) | `Bni` |
| `MISC/STATS.BNI`, `STATS.MTI` | statistics screens: maps (`L1_MAP`…), Score-O-matic's alien head model, sounds | `Bni`, `TextureArchive` |
| `MISC/LOAD_n.LBB` | loading picture: 768-byte palette, `u16 w, h`, pixels | |
| `FALL3D/FALL3D.BNI` | the fall's models (Kurt, missiles, pickups) without the leading flags, animations, palettes, haze frames, pickup lists | `Fall3d` |
| `FALL3D/FALL3D_n.MTI` | the ground seen from above (1024²), the minecrawler | `TextureArchive` |
| `STREAM/STREAM.BNI`, `STREAM.MTI` | the tunnel's textures, light sprites, planet | `Bni`, `TextureArchive` |
| `TRAVSPRT.BNI`: `SNIPERS1`, `SNIPERS2` | sniper mode's 640 × 480 frame (no header) and its mask with holes for the scope and the round cameras (run-length `u16` words) | `SniperScreen` |
| `MISC/FLIC/*.FLC` | Autodesk FLC videos, 600 × 360 × 8 bit (menu background, ending) | `Flc` |
| `MISC/MDKS_00n.GIF` | menu slideshow stills | `Gif` |
| `MISC/FLIC/MDKBZK.MVE` | Interplay MVE, 432 × 320, 8 × 8 block codec, DPCM stereo sound (the ending) | `Mve` |
