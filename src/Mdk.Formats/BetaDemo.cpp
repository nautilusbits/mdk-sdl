#include "Mdk.Formats/BetaDemo.h"

#include <algorithm>
#include <climits>
#include <optional>
#include <unordered_map>

#include "Mdk.Core/Files.h"
#include "Mdk.Formats/CaseInsensitivePath.h"
#include "Mdk.Formats/LocalPaths.h"

namespace Mdk::Formats
{
    namespace
    {
        // The folders the demo is usually unpacked to, looked for in and next to the game's
        // folder and the program's.
        const std::array<std::string, 2> Folders = {"BETA96", "MDK (1996-08-06) (beta demo)"};
        // Textures of the level archives whose index 0 is see-through (projectiles).
        const std::array<std::string, 2> SeeThrough = {"BULLET", "BOLT"};
        // BACK_3.LBB is a copy of the city's sky, in level 1's colours: drawn in the
        // nearest colours of the level's own palette.
        const std::unordered_map<int, int> SkyPalettes = {{3, 1}};
        // The effect textures: loose animated textures (u32 frames, u16 width, u16 height).
        const std::array<std::string, 6> EffectTextures = {
            "TRAVERSE/LEVEL1/TEXTURES/TRAIL.LBA", "TRAVERSE/SPRITES/SLIME/SL_BIG.LBA", "TRAVERSE/SPRITES/SLIME/SL_MED.LBA",
            "TRAVERSE/SPRITES/SLIME/SL_SMA.LBA", "TRAVERSE/SPRITES/SLIME/SB_MED.LBA", "TRAVERSE/SPRITES/SLIME/SB_SMA.LBA",
        };
        // Kurt's sprites and the HUD's images the demo has its own versions of: file to
        // TRAVSPRT.BNI entry (*.ABB RLE animations, *.LBB plain images).
        const std::array<std::string, 20> SpriteFiles = {
            "SPRITES/K_STILL.ABB", "SPRITES/K_IDLE.ABB", "SPRITES/K_RUN.ABB", "SPRITES/K_SIDE.ABB", "SPRITES/K_JUMP.ABB",
            "SPRITES/K_RJMP.ABB", "SPRITES/K_CHUTE.ABB", "SPRITES/K_SHOT.ABB", "SPRITES/K_RUNFIR.ABB", "SPRITES/K_HANG.ABB",
            "SPRITES/K_LOOKU.ABB", "SPRITES/CROSS.ABB", "SPRITES/K_ROLLL.ABB", "SPRITES/K_ROLLR.ABB", "SPRITES/K_HELM.ABB",
            "SPRITES/K_BCKUP.ABB", "SPRITES/K_LOOKD.ABB", "SPRITES/K_FIRE_M.ABB", "SPRITES/HUD/SC_STAT.LBB", "SPRITES/HUD/SC_BSTAT.LBB",
        };
    }

    std::shared_ptr<BetaDemo> BetaDemo::Find(const std::shared_ptr<MdkData>& data)
    {
        std::vector<std::string> candidates;
        auto env = GetEnv(EnvironmentVariable);
        if (env && !env->empty())
        {
            candidates.push_back(*env);
        }

        auto local = LocalPaths::Get(LocalPaths::Beta);
        if (!local.empty())
        {
            candidates.push_back(local);
        }

        for (auto& root : {data ? data->Dir() : std::string(), Path::BaseDirectory()})
        {
            if (root.empty())
            {
                continue;
            }

            for (auto& folder : Folders)
            {
                candidates.push_back(Path::Combine(root, folder));
                candidates.push_back(Path::GetFullPath(Path::Combine(root, "..", folder)));
            }
        }

        for (auto& candidate : candidates)
        {
            if (File::Exists(CaseInsensitivePath::Resolve(candidate, Marker)))
            {
                return std::shared_ptr<BetaDemo>(new BetaDemo(candidate));
            }
        }

        return nullptr;
    }

    std::string BetaDemo::PathOf(const std::string& relative) const { return CaseInsensitivePath::Resolve(_dir, relative); }

    std::vector<byte> BetaDemo::Read(const std::string& relative) const
    {
        auto path = PathOf(relative);
        return File::Exists(path) ? File::ReadAllBytes(path) : std::vector<byte>();
    }

    std::string BetaDemo::LevelDir(int level) { return std::format("TRAVERSE/LEVEL{}/", level); }

    std::vector<byte> BetaDemo::LoadingScreen(int level) const
    {
        auto found = std::find(Levels.begin(), Levels.end(), level);
        auto index = found == Levels.end() ? -1 : static_cast<int>(found - Levels.begin());
        return Read(std::format("DEMO/SCREEN{}.LBB", index + 1));
    }

    std::shared_ptr<Dti> BetaDemo::LoadDti(int level) const
    {
        auto dir = LevelDir(level);
        auto dti = ParseSettings(Text(dir + std::format("LEVEL{}.SET", level)));
        dti->Palette = Palette::FromRgb(Read(dir + std::format("L{}_PAL.LBP", level)));
        std::vector<byte> sky((SkyWidth + SkyWrapExtra) * SkyHeight);
        auto file = Read(std::format("TRAVERSE/SCREENS/BACK_{}.LBB", level));
        std::copy_n(file.begin(), Math::Min(file.size(), sky.size()), sky.begin());
        auto skyLevel = SkyPalettes.find(level);
        if (skyLevel != SkyPalettes.end())
        {
            sky = Remap(sky, *Palette::FromRgb(Read(LevelDir(skyLevel->second) + std::format("L{}_PAL.LBP", skyLevel->second))), *dti->Palette);
        }

        dti->SkyWrapWidth = SkyWidth;
        dti->Sky = std::make_shared<Texture>();
        dti->Sky->Name = "SKY";
        dti->Sky->Width = SkyWidth + SkyWrapExtra;
        dti->Sky->Height = SkyHeight;
        dti->Sky->Indices = std::move(sky);
        dti->MirrorSky = dti->Sky;
        auto names = ArenaNames(level);
        for (auto& name : names)
        {
            dti->Arenas.push_back(Dti::ArenaEntry{name, CameraPitch, ParseRecords(Text(dir + std::format("ARENAS/{}.HOT", name)), names)});
        }

        return dti;
    }

    std::shared_ptr<Dti> BetaDemo::ParseSettings(const std::string& text)
    {
        constexpr int StartWords = 5;
        constexpr int FirstGlassLine = 2;
        auto lines = Lines(text);
        auto& start = lines.at(0);
        auto dti = std::make_shared<Dti>();
        dti->StartArena = ToInt(Number(start.at(0)));
        dti->StartPosition = Vector3(Number(start.at(1)), Number(start.at(2)), Number(start.at(3)));
        dti->StartAngle = start.size() >= StartWords ? Number(start[4]) : DefaultStartAngle;
        dti->SkyTopColor = ToInt(Number(lines.at(1).at(0)));
        dti->SkyBottomColor = ToInt(Number(lines.at(1).at(1)));
        dti->SkyHorizonRow = ToInt(Number(lines.at(1).at(2)));
        for (auto i = 0; i < Dti::GlassCount; i++)
        {
            std::vector<uint> glass;
            auto& words = lines.at(FirstGlassLine + i);
            for (size_t w = 0; w < words.size() && w < 4; w++)
            {
                glass.push_back(SatCast<uint>(Number(words[w])));
            }

            dti->Glass.push_back(std::move(glass));
        }

        return dti;
    }

    std::vector<std::string> BetaDemo::ArenaNames(int level) const { return ParseArenaNames(Text(LevelDir(level) + std::format("LEVEL{}.CON", level))); }

    std::vector<std::string> BetaDemo::ParseArenaNames(const std::string& text)
    {
        auto lines = Lines(text);
        std::vector<std::string> names;
        size_t line = 1;
        for (auto i = 0; i < ToInt(Number(lines.at(0).at(0))) && line < lines.size(); i++)
        {
            names.push_back(Str::ToUpper(lines[line].at(0)));
            line += 1 + ToInt(Number(lines[line].at(1)));
        }

        return names;
    }

    std::vector<Dti::Record> BetaDemo::ParseRecords(const std::string& text, const std::vector<std::string>& arenaNames)
    {
        std::vector<Dti::Record> records;
        for (auto& words : Lines(text))
        {
            auto keyword = Str::ToUpper(words[0]);
            auto named = keyword != "HIDEPT" && keyword != "CONNECT" && keyword != "MSWAP";
            auto name = named && words.size() > 1 ? Str::ToUpper(words[1]) : "";
            std::vector<float> n;
            for (size_t i = named ? 2 : 1; i < words.size(); i++)
            {
                n.push_back(Number(words[i]));
            }

            std::optional<Dti::Record> record;
            if (keyword == "ASHOW")
            {
                auto found = std::find(arenaNames.begin(), arenaNames.end(), name);
                auto index = found == arenaNames.end() ? -1 : static_cast<int>(found - arenaNames.begin());
                record = Dti::Record{RecordShow, index, 0.0f, Vector3(n.at(0), n.at(1), 0.0f), Vector3(n.at(2), n.at(3), 0.0f), name};
            }
            else if (keyword == "ALIEN")
            {
                record = Dti::Record{RecordAlien, ToInt(n.at(0)), 0.0f, Vector3(n.at(2), n.at(3), n.at(4)), Vector3::Zero(), name};
            }
            else if (keyword == "PICKUP")
            {
                record = Dti::Record{RecordPickup, -1, 0.0f, Vector3(n.at(0), n.at(1), n.at(2)), Vector3::Zero(), name};
            }
            else if (keyword == "HIDEPT")
            {
                record = Dti::Record{RecordHidePoint, ToInt(n.at(0)), 0.0f, Vector3(n.at(1), n.at(2), n.at(3)), Vector3::Zero(), ""};
            }
            else if (keyword == "CONNECT")
            {
                record = Dti::Record{RecordConnection, ToInt(n.at(0)), 0.0f, Vector3(n.at(1), n.at(3), n.at(5)), Vector3(n.at(2), n.at(4), n.at(6)), ""};
            }
            else if (keyword == "MSWAP")
            {
                record = Dti::Record{RecordMaterialSwap, -1, 0.0f, Vector3::Zero(), Vector3::Zero(), ""};
            }

            if (record)
            {
                records.push_back(*record);
            }
        }

        return records;
    }

    std::vector<std::pair<std::string, Vector3>> BetaDemo::LoadTeleports() const { return ParseTeleports(Text("TRAVERSE/TELEPORT.TXT")); }

    std::vector<std::pair<std::string, Vector3>> BetaDemo::ParseTeleports(const std::string& text)
    {
        constexpr size_t Words = 4;
        std::vector<std::pair<std::string, Vector3>> teleports;
        for (auto& w : Lines(text))
        {
            if (w.size() < Words)
            {
                continue;
            }

            teleports.emplace_back(Str::ToUpper(w[0]), Vector3(Number(w[1]), Number(w[2]), Number(w[3])));
        }

        return teleports;
    }

    std::shared_ptr<Mto> BetaDemo::LoadMto(int level) const
    {
        constexpr int NameLength = 8;
        auto dir = LevelDir(level);
        auto bytes = Read(dir + std::format("LEVEL{}O.MTO", level));
        std::unordered_map<std::string, int> textures;
        BinReader r(bytes);
        auto count = bytes.empty() ? 0 : r.U32();
        for (uint i = 0; i < count; i++)
        {
            auto name = Str::ToUpper(r.Name(NameLength));
            textures[name] = static_cast<int>(r.U32()) + static_cast<int>(sizeof(uint));
        }

        auto mto = std::make_shared<Mto>();
        for (auto& name : ArenaNames(level))
        {
            if (IsCorridor(name))
            {
                continue;
            }

            auto arena = LoadWorld(level, name);
            if (!arena)
            {
                continue;
            }

            auto offset = textures.find(name);
            if (offset != textures.end())
            {
                arena->Textures = TextureArchive::Parse(bytes, offset->second, 0, TextureArchive::Names::Upper);
                MakeOpaque(*arena->Textures);
            }

            auto palette = Read(dir + std::format("ARENAS/{}.LBP", name));
            if (palette.size() >= PaletteBytes)
            {
                arena->PaletteRgb = Bin::Slice(palette, Palette::ArenaFirstIndex * 3, Arena::PaletteColors * 3);
            }

            mto->Add(arena);
        }

        return mto;
    }

    bool BetaDemo::IsCorridor(const std::string& name) { return !name.empty() && name.front() == 'C'; }

    std::vector<std::shared_ptr<Arena>> BetaDemo::LoadCorridors(int level) const
    {
        std::vector<std::shared_ptr<Arena>> corridors;
        for (auto& name : ArenaNames(level))
        {
            if (!IsCorridor(name))
            {
                continue;
            }

            if (auto arena = LoadWorld(level, name))
            {
                corridors.push_back(arena);
            }
        }

        return corridors;
    }

    std::shared_ptr<Arena> BetaDemo::LoadWorld(int level, const std::string& name) const
    {
        auto bytes = Read(LevelDir(level) + std::format("ARENAS/{}.BSP", name));
        if (bytes.empty())
        {
            return nullptr;
        }

        auto arena = Arena::ParseBetaWorld(name, bytes);
        arena->ClipFlag = NotDrawn;
        return arena;
    }

    int BetaDemo::CheckNodes(const Arena& arena)
    {
        constexpr float Tolerance = 0.1f;
        constexpr int EmptyLeaf = -1;
        auto bad = 0;
        for (auto& node : arena.Nodes)
        {
            auto off = false;
            for (auto [first, count] : {std::pair{node.FrontFirst, node.FrontCount}, std::pair{node.BackFirst, node.BackCount}})
            {
                if (count <= 0)
                {
                    continue;
                }

                if (first < 0 || first + count > arena.TriangleCount())
                {
                    off = true;
                    break;
                }

                for (auto t = first; t < first + count && !off; t++)
                {
                    for (auto k = 0; k < 3; k++)
                    {
                        if (MathF::Abs(node.Distance(arena.Vertices.at(arena.TriangleIndices.at(t * 3 + k)))) > Tolerance)
                        {
                            off = true;
                            break;
                        }
                    }
                }

                if (off)
                {
                    break;
                }
            }

            auto nodeCount = static_cast<int>(arena.Nodes.size());
            auto children = node.Negative < EmptyLeaf || node.Negative >= nodeCount || node.Positive < EmptyLeaf || node.Positive >= nodeCount;
            if (off || children)
            {
                bad++;
            }
        }

        return bad;
    }

    std::shared_ptr<TextureArchive> BetaDemo::LoadTextures(int level) const
    {
        auto archive = TextureArchive::Parse(Read(LevelDir(level) + std::format("LEVEL{}S.MTI", level)), static_cast<int>(sizeof(uint)), 0, TextureArchive::Names::Upper);
        MakeOpaque(*archive);
        for (auto& file : EffectTextures)
        {
            auto bytes = Read(file);
            auto name = Path::GetFileNameWithoutExtension(file);
            if (!bytes.empty() && !archive->Textures.ContainsKey(name))
            {
                archive->Textures[name] = Texture::ParseAnimated(name, bytes, 0, 0);
            }
        }

        return archive;
    }

    void BetaDemo::MakeOpaque(TextureArchive& archive)
    {
        for (auto& [name, texture] : archive.Textures)
        {
            if (texture->FrameCount != 1 || std::find(SeeThrough.begin(), SeeThrough.end(), texture->Name) != SeeThrough.end())
            {
                continue;
            }

            std::replace(texture->Indices.begin(), texture->Indices.end(), byte{0}, OpaqueBlack);
        }
    }

    std::vector<std::shared_ptr<Sni>> BetaDemo::LoadSounds(int level) const
    {
        auto dir = LevelDir(level);
        std::vector<std::shared_ptr<Sni>> sounds;
        for (auto& file : {std::string("TRAVERSE/TRAVERSE.SNI"), dir + std::format("LEVEL{}S.SNI", level), dir + std::format("LEVEL{}O.SNI", level)})
        {
            auto bytes = Read(file);
            if (bytes.empty())
            {
                continue;
            }

            sounds.push_back(Sni::ParseBeta(Share(std::move(bytes))));
        }

        return sounds;
    }

    void BetaDemo::AddSprites(Bni& sprites) const
    {
        for (auto& file : SpriteFiles)
        {
            auto bytes = Read("TRAVERSE/" + file);
            if (!bytes.empty())
            {
                sprites.Add(Path::GetFileNameWithoutExtension(file), bytes);
            }
        }
    }

    std::shared_ptr<Cmi> BetaDemo::LoadCmi(int level) const { return Cmi::ParseBeta(Share(Read(LevelDir(level) + std::format("LEVEL{}.CMI", level)))); }

    std::vector<byte> BetaDemo::Remap(const std::vector<byte>& indices, const Palette& from, const Palette& to)
    {
        std::array<byte, Palette::Size> table{};
        for (auto i = 0; i < Palette::Size; i++)
        {
            auto best = 0;
            auto bestDistance = INT_MAX;
            for (auto k = 0; k < Palette::Size; k++)
            {
                auto distance = 0;
                for (auto c = 0; c < 3; c++)
                {
                    auto d = from.Rgba[i * 4 + c] - to.Rgba[k * 4 + c];
                    distance += d * d;
                }

                if (distance < bestDistance)
                {
                    best = k;
                    bestDistance = distance;
                }
            }

            table[i] = static_cast<byte>(best);
        }

        std::vector<byte> remapped(indices.size());
        std::transform(indices.begin(), indices.end(), remapped.begin(), [&](byte i) { return table[i]; });
        return remapped;
    }

    std::string BetaDemo::Text(const std::string& relative) const { return Bin::AsciiString(Read(relative)); }

    std::vector<std::vector<std::string>> BetaDemo::Lines(const std::string& text)
    {
        std::vector<std::vector<std::string>> lines;
        for (auto& line : Str::Split(text, '\n'))
        {
            auto words = Str::Split(Str::Split(line, ';')[0], " \t\r", Str::SplitOptions::RemoveEmpty);
            if (!words.empty())
            {
                lines.push_back(std::move(words));
            }
        }

        return lines;
    }

    float BetaDemo::Number(const std::string& word)
    {
        float value = 0;
        return Str::TryParseFloat(word, value) ? value : 0.0f;
    }
}
