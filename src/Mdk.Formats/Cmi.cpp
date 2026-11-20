#include "Mdk.Formats/Cmi.h"

#include "Mdk.Core/Files.h"

namespace Mdk::Formats
{
    Cmi::Cmi(SharedBytes bytes, Scripts::ScriptDialect dialect) : _bytes(std::move(bytes)), _dialect(dialect)
    {
        if (dialect == Scripts::ScriptDialect::Beta1996)
        {
            ParseBeta();
            return;
        }

        BinReader r(*_bytes, DirectoryOffset);
        for (auto* directory : {&AlienScripts, &ModelOffsets, &ObjectScripts, &ArenaScripts})
        {
            auto count = r.U32();
            for (uint i = 0; i < count; i++)
            {
                auto name = r.Name(r.U8());
                auto offset = Absolute(r.U32());
                (*directory)[name] = offset;
                if (offset != 0 && (directory == &AlienScripts || directory == &ObjectScripts))
                {
                    _entries.push_back(offset);
                }
            }
        }

        std::vector<std::string> arenas;
        for (auto& [arena, offset] : ArenaScripts)
        {
            arenas.push_back(arena);
        }

        for (auto& arena : arenas)
        {
            BinReader record(*_bytes, ArenaScripts[arena]);
            record.Name(record.U8());
            ArenaMusic[arena] = record.Name(record.U8());
            ArenaScripts[arena] = Absolute(record.U32());
            if (ArenaScripts[arena] != 0)
            {
                _entries.push_back(ArenaScripts[arena]);
            }
        }
    }

    int Cmi::Absolute(uint offset) { return offset == 0 ? 0 : FileBase + static_cast<int>(offset); }

    std::shared_ptr<Cmi> Cmi::Load(const std::string& path) { return std::shared_ptr<Cmi>(new Cmi(Share(File::ReadAllBytes(path)))); }

    std::shared_ptr<Model> Cmi::GetModel(const std::string& name)
    {
        auto* offset = ModelOffsets.Find(name);
        if (offset == nullptr || *offset == 0)
        {
            return nullptr;
        }

        auto& model = _models[name];
        if (!model)
        {
            model = Model::Parse(name, *_bytes, *offset);
        }

        return model;
    }
}
