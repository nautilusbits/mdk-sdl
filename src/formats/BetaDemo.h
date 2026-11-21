#pragma once

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Numerics.h"
#include "formats/Arena.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "formats/Dti.h"
#include "formats/MdkData.h"
#include "formats/Mto.h"
#include "formats/Palette.h"
#include "formats/Sni.h"
#include "formats/TextureArchive.h"

namespace Mdk::Formats
{
    // The beta demo of 6 August 1996 (MDKDEMO.EXE, godot-mdk docs/beta96.md, its
    // mdk_beta.gd): TRAVERSE/LEVEL1 (the city), LEVEL3 (HMO_1) and
    // LEVEL6 (OLYM_1), in loose files and earlier versions of the retail formats, read
    // into the retail loaders' objects (Dti, Mto, Cmi...), so
    // the game runs them like its own levels, numbered 961, 963 and 966.
    //   LEVELn.SET, LEVELn.CON, ARENAS/*.HOT ──► Dti      ARENAS/*.BSP, LEVELnO.MTO ──► Mto
    //   LEVELnS.MTI, *.LBA ──► TextureArchive            LEVELn.CMI ──► Cmi (Beta1996 scripts)
    //   *.SNI ──► Sni                                    SPRITES/*.ABB, *.LBB ──► TRAVSPRT.BNI entries
    class BetaDemo
    {
    public:
        // The demo's TRAVERSE/LEVELn folders.
        static constexpr std::array<int, 3> Levels = {1, 3, 6};
        // Added to a demo level's number to tell it from the retail levels.
        static constexpr int NumberBase = 960;
        // A triangle flag of the .BSP files: simple shapes around the detailed ones,
        // solid but not drawn (0x15d90) and left out of the triangle tests of rays.
        static constexpr uint NotDrawn = 2;

        // The folder with TRAVERSE and MDKDEMO.EXE.
        const std::string& Dir() const { return _dir; }

        static bool IsBeta(int number) { return number > NumberBase; }

        // The demo's level (1, 3, 6) of a port's number (961, 963, 966).
        static int LevelOf(int number) { return number - NumberBase; }

        static int NumberOf(int level) { return NumberBase + level; }

        // The demo, or null: MDK_BETA_DIR, beta in LocalPaths, or a
        // BETA96 or MDK (1996-08-06) (beta demo) folder in or next to the game's folder or
        // the program's.
        static std::shared_ptr<BetaDemo> Find(const std::shared_ptr<MdkData>& data);

        // The level's loading screen (DEMO/SCREENn.LBB, in the demo's order; the retail
        // LOAD_n.LBB layout), or none.
        std::vector<byte> LoadingScreen(int level) const;

        // The level's settings as the retail LEVELn.DTI has them.
        std::shared_ptr<Dti> LoadDti(int level) const;

        // LEVELn.SET: the start arena's index and Kurt's position (and angle), the sky's
        // fill colours (top, bottom) and horizon row, then the four glass colours r g b opacity.
        static std::shared_ptr<Dti> ParseSettings(const std::string& text);

        // LEVELn.CON: the number of arenas, then per arena its name, a number of portals
        // and the portals (C<arena index> side z plane from to, not used).
        static std::vector<std::string> ParseArenaNames(const std::string& text);

        // An arena's .HOT file as DTI records, one per line:
        //   ASHOW arena x1 y1 x2 y2          walking into the rectangle shows that arena (NONE: none)
        //   ALIEN type id n x y z            an alien, run by the script ARENA$TYPE_id
        //   PICKUP type x y z                a pickup
        //   HIDEPT id x y z                  a cover spot
        //   CONNECT id x1 x2 y1 y2 z1 z2     a doorway between an arena and a corridor, in pairs
        //   MSWAP six numbers                a material swap (in no file)
        static std::vector<Dti::Record> ParseRecords(const std::string& text, const std::vector<std::string>& arenaNames);

        // The demo's teleports, TRAVERSE/TELEPORT.TXT: a line arena x y z per digit
        // (the first is 0; 0x440c4).
        std::vector<std::pair<std::string, Vector3>> LoadTeleports() const;

        static std::vector<std::pair<std::string, Vector3>> ParseTeleports(const std::string& text);

        // The level's arenas. LEVELnO.MTO only has their textures: u32 count, per
        // arena char[8] name, u32 offset; at the offset u32 size, then a texture archive
        // without the retail header (offsets from its count).
        std::shared_ptr<Mto> LoadMto(int level) const;

        std::vector<std::shared_ptr<Arena>> LoadCorridors(int level) const;

        // The nodes whose listed triangles don't lie on their plane: none when the demo's node
        // layout is read right (it's taken as the retail one; not checked against the demo).
        static int CheckNodes(const Arena& arena);

        // LEVELnS.MTI (u32 size, then an archive without the retail header) and the
        // loose effect textures.
        std::shared_ptr<TextureArchive> LoadTextures(int level) const;

        // The demo's walls and models use palette index 0 as black, where the retail game keeps
        // it for the see-through parts of effects: it becomes OpaqueBlack, except in
        // animated textures and SeeThrough.
        static void MakeOpaque(TextureArchive& archive);

        // The sound archives, later ones winning: TRAVERSE.SNI, LEVELnS.SNI,
        // LEVELnO.SNI (u32 size, u32 count, then the retail entries); missing ones are left out.
        std::vector<std::shared_ptr<Sni>> LoadSounds(int level) const;

        // Puts the demo's versions of Kurt's sprites and the HUD's images over the retail ones.
        void AddSprites(Bni& sprites) const;

        // LEVELn.CMI, its paths converted (Cmi::ParseBeta).
        std::shared_ptr<Cmi> LoadCmi(int level) const;

        // Palette indices drawn with `from` as the nearest colours of `to`.
        static std::vector<byte> Remap(const std::vector<byte>& indices, const Palette& from, const Palette& to);

    private:
        static constexpr const char* Marker = "TRAVERSE/LEVEL1/LEVEL1.SET";
        static constexpr const char* EnvironmentVariable = "MDK_BETA_DIR";
        static constexpr int SkyWidth = 1800;
        static constexpr int SkyHeight = 360;
        static constexpr int SkyWrapExtra = 4;
        static constexpr int PaletteBytes = Palette::Size * 3;
        // The camera pitch the retail levels usually give their arenas (degrees).
        static constexpr float CameraPitch = 4.0f;
        // LEVEL3.SET has no start angle; 90 faces +Y, into the arena.
        static constexpr float DefaultStartAngle = 90.0f;
        // Black in every palette of the demo (the first colour after the 16 system colours).
        static constexpr byte OpaqueBlack = 16;

        // Record types of the .HOT files, the retail DTI numbers.
        static constexpr uint RecordShow = 1;
        static constexpr uint RecordAlien = 2;
        static constexpr uint RecordPickup = 4;
        static constexpr uint RecordHidePoint = 5;
        static constexpr uint RecordConnection = 6;
        // MSWAP (3 in the demo) isn't a retail type 3 record.
        static constexpr uint RecordMaterialSwap = 103;

        std::string _dir;

        explicit BetaDemo(std::string dir) : _dir(std::move(dir)) {}

        std::string PathOf(const std::string& relative) const;

        // A file's bytes, or none when it's missing.
        std::vector<byte> Read(const std::string& relative) const;

        static std::string LevelDir(int level);

        // The level's arenas and corridors in the order of LEVELn.CON (the start arena
        // is an index in it), in upper case.
        std::vector<std::string> ArenaNames(int level) const;

        // Corridors (CORR_n; level 3's CHMO_1 is a copy of the city's CORR_1)
        // have no textures or palette of their own, like the retail ones.
        static bool IsCorridor(const std::string& name);

        // An arena's geometry, ARENAS/name.BSP, or null; its NotDrawn
        // triangles only stop Kurt.
        std::shared_ptr<Arena> LoadWorld(int level, const std::string& name) const;

        std::string Text(const std::string& relative) const;

        // The words of a text file's lines, without comments (;) and empty lines.
        static std::vector<std::vector<std::string>> Lines(const std::string& text);

        static float Number(const std::string& word);
    };
}
