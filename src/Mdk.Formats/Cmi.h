#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Formats/BinReader.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/ModelAnimation.h"
#include "Mdk.Formats/Scripts/ScriptDecoder.h"

namespace Mdk::Formats
{
    // LEVELn.CMI: level scripts and global models. After the header, 4 directories,
    // each u32 count then u8 length, char name[length], u32 offset (from 4, 0 = none):
    //   0  alien instance scripts   HMO_1$XG_0
    //   1  global models            (0 = look it up in the arena)
    //   2  object type scripts      HMO_1$XH1_DOOR
    //   3  arenas                   record: pascal NONE?, pascal music, u32 script
    //
    // The 1996 demo's LEVELn.CMI (BetaDemo): u32 size, the four
    // directories of the retail file (offsets from 4, names in lower case), the scripts (an earlier
    // bytecode, ScriptDialect::Beta1996) and their paths, the models (the retail format)
    // and the animations, which the scripts point at. An arena's record is two strings: its music and
    // its ambient loop; arenas have no scripts yet.
    class Cmi
    {
    public:
        const SharedBytes& Bytes() const { return _bytes; }
        // The bytecode of the scripts: the retail one, or the 1996 demo's (BetaDemo).
        Scripts::ScriptDialect Dialect() const { return _dialect; }
        // Name to absolute file offset (0 = none), per directory.
        Dictionary<std::string, int> AlienScripts;
        Dictionary<std::string, int> ModelOffsets;
        Dictionary<std::string, int> ObjectScripts;
        Dictionary<std::string, int> ArenaScripts;
        // Arena to its music (a sound of LEVELnO.SNI, or NONE).
        Dictionary<std::string, std::string> ArenaMusic;

        // Arena to its ambient loop, played under the music (the demo's own).
        Dictionary<std::string, std::string> ArenaAmbience;
        // The demo's paths turned into retail spline records at the end of Bytes:
        // the old offset to the new.
        Dictionary<int, int> BetaPaths;

        // Where scripts start: every alien, object and arena script, in directory order (a
        // name repeated in a directory counts once per entry).
        const std::vector<int>& EntryPoints() const { return _entries; }

        static std::shared_ptr<Cmi> Load(const std::string& path);

        // A global model, or null when it is an arena's (or unknown).
        std::shared_ptr<Model> GetModel(const std::string& name);

        // A file of the 1996 demo.
        static std::shared_ptr<Cmi> ParseBeta(SharedBytes bytes);

        // An animation of the demo's file, which scripts point at directly.
        std::shared_ptr<ModelAnimation> GetBetaAnimation(int offset);

    private:
        static constexpr int DirectoryOffset = 0x14;
        static constexpr int FileBase = 4;

        static constexpr int BetaDirectoryOffset = 4;
        // A path of the demo (0x32ff8): u32 count, the first position, then
        // count - 1 steps of 3 f32, one per frame.
        static constexpr int PathStepSize = 12;
        // A retail spline key: s32 frame, the position, the incoming and outgoing tangents.
        static constexpr int PathKeySize = 40;
        static constexpr int PathCountSize = 4;

        std::unordered_map<std::string, std::shared_ptr<Model>> _models;
        std::vector<int> _entries;
        SharedBytes _bytes;
        Scripts::ScriptDialect _dialect;
        std::unordered_map<int, std::shared_ptr<ModelAnimation>> _betaAnimations;

        explicit Cmi(SharedBytes bytes, Scripts::ScriptDialect dialect = Scripts::ScriptDialect::Retail);

        static int Absolute(uint offset);

        void ParseBeta();

        // Turns the paths the scripts follow (found by walking them) into retail spline
        // records appended to the file.
        void ConvertPaths();

        // A demo path as a spline with a key per frame whose tangents are the steps to its
        // neighbours (straight lines between them); it starts again from its first position after the
        // last frame.
        static std::vector<byte> SplineOf(std::span<const byte> source, int path);
    };
}
