# The original engine

How MDK (Shiny Entertainment, 1997) works inside, reverse engineered from `MDKD3D.EXE` with
Ghidra. This page is an overview. The data files are in [formats.md](formats.md), and the port's
own structure is in [architecture.md](architecture.md).

## In short

The engine is native C. A small virtual machine runs the bytecode scripts that make the levels
live.

```
 ┌───────────────────────── native C (MDK*.EXE) ──────────────────────────┐
 │ main loop, game states, menus, the fall, the stream, statistics, video │
 │ Kurt: input, moves, sprite, weapons, items, rides (snowboard, bomber)  │
 │ objects: physics, collisions (BSP), paths, animation, doors, pickups   │
 │ renderer (software / Glide / Direct3D), sound (DirectSound)            │
 │                                                                        │
 │   script VM ◄── per object, every frame ── bytecode (LEVELn.CMI)       │
 │   "what to do": wait, chase, fire, spawn, open, explode                │
 └────────────────────────────────────────────────────────────────────────┘
```

The scripts decide *what* the aliens, doors, bosses and levels do. The C code decides *how*: it
moves, collides, animates and draws. Kurt himself is not scripted.

## The executables

- There are three builds of the same game code, one per renderer: `MDK95.EXE` (software),
  `MDK3DFX.EXE` (3dfx Glide) and `MDKD3D.EXE` (Direct3D). The port follows `MDKD3D.EXE`.
- The game was compiled with Watcom C, which passes arguments in registers (EAX, EDX, EBX, ECX).
  Ghidra needs a custom calling convention to read it.
- Assertion strings keep the source file names: `traverse.c` (walking the levels), `tr_alcmd.c`
  ("alien commands", the script interpreter, about 6000 lines), `fall_3d.c`, `endlev.c`,
  `savegame.c`, `options.c`…

## Main loop and game states

One loop, one state at a time:

```
 menu ─► briefing ─► fall ─► level ─► end of level ─► stream ─► statistics ─┐
   ▲     (state 6)  (2)     (3)      (tornado)       (5)        (6)        │
   │                                                                       │
   └─ end videos (8) ◄─ after level 5  ◄───────────── next level ◄─────────┘
```

| State | Mode |
| --- | --- |
| 0 | Menu |
| 2 | The fall: a minigame, Kurt falls onto the city with missiles and radars |
| 3 | A level ("traverse") |
| 5 | The stream: a generated tunnel to the next city |
| 6 | Statistics, debriefing, briefing |
| 8 | The ending videos |

- **Timing.** The logic runs in ticks of 1/30 s. Each frame is one step scaled by the frame time
  (smoothed, at most 4 ticks). A frame limiter caps the game at about 29 fps.
- **Graphics.** Everything is 8-bit indexed colour, with a 600 × 360 view. The Direct3D build
  converts the palette to textures; the software build draws into the palette directly.

## World: levels, arenas, corridors

```
 LEVEL3:   HMO_1 ══ CHMO_1 ══ HMO_2 ══ CHMO_2 ══ HMO_3 …
           arena    corridor  arena
                 ▲
                 connection: a doorway box with a direction
```

- **Arenas.** A level is a chain of arenas, the big spaces where the fighting happens. Corridors
  join them. Each arena has its own geometry and BSP tree, textures, 112 palette colours, models
  and music.
- **Connections.** Kurt changes arena only by crossing a connection's doorway, or by a teleport.
- **What's active.** Only Kurt's arena runs. During a crossing, the arena behind him runs too
  (two arenas at most). All other arenas are frozen: their objects don't update and they aren't
  drawn.
- **Streaming.** Arenas load in 32 KiB chunks as Kurt approaches, so a level never sits in memory
  whole.
- **The sky** is a 2D panorama scrolled with the view, not geometry. The "mirrors" are triangles
  filled with the same panorama.

## Rendering

- **No depth buffer.** Arena triangles come back to front out of a walk of the BSP tree. Objects
  are inserted into the tree's leaves, and their triangles are depth-sorted. This is also why glass
  needs no extra sorting.
- **Materials.** A triangle is a texture, a flat palette colour, or a special material: glass
  (translucent, its colour per level), mirror (sky panorama) or ripple (software only).
- **Kurt is a sprite.** His animations are run-length 2D frames scaled to the screen and drawn over
  the 3D scene; only the fall uses a 3D model of him.
- **Effects** are mostly palette tricks: flashes, fades, glass blending through lookup tables in
  the software build.

## Objects

Aliens, doors, lifts, pickups, bullets, explosions and debris are all **objects**: one 0x32E-byte
structure, kept in a list per arena.

An object holds:
- its model and animation, position, yaw, pitch and roll, and velocity;
- flags, health, and hit information;
- its script state (restart point, gosub stack, timers, 4 variables);
- its leader, priority and a linked object.

Each frame, each active object goes through:

```
 built-in behaviour (door, swing) ─► script ─► spline path ─► movement command
   ─► gravity ─► friction + move with collisions ─► pickups / projectiles
   ─► animation (with root motion) ─► carry Kurt if he stands on it
```

- **Flags** switch engine behaviours on: gravity, collisions with the arena, a platform Kurt stands
  on, rolling, bouncing, door, pickup, path modes.
- **Movement commands.** Scripts don't move objects themselves. They set a movement command, and
  the engine carries it out every frame: go to a point, chase the target, fly a formation around a
  leader, follow a path, ballistic jump, projectile. Paths are cubic Hermite splines stored in the
  CMI.
- **Pathfinding** is minimal. If the straight line to the goal is blocked, the engine tries detour
  points beside it. There's no navigation graph; aliens get around with waypoints and cover spots
  from the DTI.
- **Collisions.** A box is swept through the arena's BSP and slides along walls. Kurt uses the same
  sweep. Other objects don't block a moving object; only Kurt checks against objects.
- **Hits** set a hit event on the object: which part was hit and by what (chain gun, sniper round,
  grenade, blast). Scripts test and clear it. Bosses have **weak parts** with their own hit points.
- **Death** switches the object to its death script, or it explodes: its break-up model's parts
  fly off, with sparks, a flash and gore.

## Scripts

This is the virtual machine the levels are made of. It isn't one script per level that triggers
events. **Every object runs its own small program**, and the arena runs one too:

| Script | Runs | Does |
| --- | --- | --- |
| Arena script | every frame for Kurt's arena | the level's director: spawns pickups, aliens, waves; waits for Kurt to enter areas; opens the way on; boss phases |
| Object type script | once, when an object of the type is created | its setup: health, weak parts, flags, model, sounds |
| Alien script | every frame for each alien | its behaviour: patrol, see Kurt, chase, take cover, shoot, die |
| Spawned script | started by a `spawn`, `fire` or a command | bullets, effects, doors, lifts, children of a boss |

All of them live in `LEVELn.CMI`, as bytecode with 251 opcodes. The levels use 236 of them.

### How a script runs

The VM keeps no program counter between frames. Each object has a **restart point**. Every frame
the VM:

1. Sets the *target*: Kurt (or the decoy he threw).
2. If a `wait` is running, counts it down and stops for this frame.
3. Otherwise runs from the restart point until the frame ends: opcode `0xFF` (end of frame), an
   opcode that yields, or 1000 opcodes (an error that stops the script).

`set_restart` moves the restart point to the next instruction, and a `goto` moves it to its
target. A script is thus a set of states, each re-checking its conditions every frame
(simplified):

```
chase:  set_restart                       ; the state "chase" starts here
        move_to_target                    ; movement command: the engine moves us
        if_hit            goto hurt
        if_target_dist <, 40   goto attack
        end                               ; 0xFF: next frame starts again at "chase"

attack: set_restart
        anim_once  "XG_SHOOT"
        fire       "BULLET", …
        if_anim_done      goto chase
        end
```

- **Gosub** has a stack of 4 levels, and `return` restores both the return point and the restart
  point.
- **Conditions** (`if_*`) end with a branch action: goto, gosub, gosub then/else, or return. They
  can also compare a value (a literal or a variable of the global, arena, own or linked object)
  with `<`, `>`, `≈`, ranges.
- **Variables.** Each object, each arena and the game have 4 floats. Arena variables carry a
  level's state, for example a boss's progress shown in the boss bar.
- **Errors** (unknown opcode, stack overflow, the 1000-opcode limit) only stop that script.

### What the opcodes do

| Group | Examples |
| --- | --- |
| Flow | `set_restart`, `goto` (with a random choice among targets), `gosub`, `return`, `wait`, `switch_gosub`, `stop_script` |
| Senses | `if_sees_kurt`, `if_kurt_looks_at_me`, `if_kurt_in_rect`, `if_target_dist`, `if_hit_part`, `if_timer`, `if_chance` |
| Motion | `move_to`, `move_to_target`, `follow_path`, `turn_and_jump_to_dest`, `pick_waypoint8`, `find_cover_spot`, `set_max_speed`, `set_gravity` |
| Animation | `anim_once`, `anim_loop`, `wait_anim_frame`, `if_anim_done` |
| Combat | `fire`, `aim_target`, `lob_to_kurt`, `hurt_kurt`, `touch_damage`, `explode`, `set_health`, `set_weak_parts`, `blow_off_parts` |
| Objects | `spawn`, `spawn_flagged` (pickups), `delete_self`, `attach_to`, `command_objects` |
| World | `shatter_group` (breaking walls and glass), `group_set_texture`, `arena_texture_frame`, fans, conveyors, doors, `teleport_player` |
| Presentation | `play_sound`, `hud_message`, `boss_bar`, `camera_track`, `screen_flash`, `special_event` (cutscenes) |

`command_objects` lets scripts give orders. An alien can tell others of a type, or those near it,
to join its formation, go somewhere, or jump to a part of their script. A **priority** decides who
obeys whom.

### Quirks

The original survives a few bugs in the data:
- An arena script of level 3 starts with an opcode that has no handler, so it never runs.
- Some boxes in level 7 have an empty Z range, so their tests are never true.
- `switch_goto` always takes its first entry (no level uses it).

## Kurt

Kurt is native code, not a script. His states follow his sprite animations: stand, run, strafe,
jump, fall, chute, hang from a ledge, slide, sniper mode, knocked down. Each frame the game:
- reads the input and accelerates or brakes him (turbo doubles the speed);
- sweeps his box through the BSP;
- lands him on floors or on objects flagged as platforms, which carry him along.
  Some platforms (flag 0x800000) keep him: he moves with them through walls and can't walk off.
- lifts him in a fan's updraft: his arena's fans, else the second arena's (a corridor's shaft fan
  lifts him on into the arena above).

The chain gun, sniper mode (a second camera with zoom and guided rounds) and the items (grenades,
the decoy, the "world's smallest nuclear explosion", the air strike…) are native too. So are the
rides: the snowboard in level 4 and the bomber in level 7.

## Sound

- **Voices.** A thin layer over DirectSound plays 64 voices of up to 127 samples. There's no
  hardware 3D: once per frame the game computes each 3D voice's volume (linear in decibels over
  25 dB), pan and Doppler frequency itself.
- **Sniper listening.** In sniper mode a sound is as loud as its place in the scope's view.
- **Music.** Each arena has its own track, streamed in when Kurt nears it. Corridors play the
  level's `CORRIDOR` track. Tracks cross-fade when Kurt changes arena.

## Where to look

- The parsers: `src/formats` (one class per format).
- The opcode table: `src/formats/scripts/ScriptOpcodes.g.cpp` (operands, names).
- The VM: `src/game/scripts/ScriptVm.cpp`, `ScriptRuntime.cpp`.
- The object motion: `ObjectMotion.cpp`.
- The full reverse-engineering notes are in the Godot port's `docs/`
  ([nemo22/mdk-godot](https://github.com/nemo22/mdk-godot/tree/main/docs)). They give the function
  addresses, object fields, constants and every opcode in detail.
