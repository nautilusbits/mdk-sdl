#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Dictionary.h"
#include "formats/Arena.h"
#include "formats/BetaDemo.h"
#include "formats/MdkData.h"
#include "formats/Sni.h"
#include "formats/SpriteAnimation.h"
#include "game/audio/SoundMixer.h"

namespace Mdk::Game::Audio
{
    // A level's sounds by name, from its SNI archives (TRAVERSE.SNI, LEVELnS.SNI,
    // LEVELnO.SNI with the music), converted at the level's load (Preload). Later archives win.
    // Then the sounds the arenas of LEVELnO.MTO carry themselves (ArenaSounds), for the
    // names no archive has: LEVEL7's barricade soldiers (DANT_2, DANT_6) sing SING and TAUNT1 from there.
    class SoundBank
    {
    public:
        explicit SoundBank(std::vector<std::shared_ptr<Formats::Sni>> archives,
            Mdk::Dictionary<std::string, std::vector<byte>> arenaSounds = {});

        static std::shared_ptr<SoundBank> ForLevel(const Formats::MdkData& data, int level,
            const std::vector<std::shared_ptr<Formats::Arena>>& arenas = {});

        // The arenas' own sounds by name (the first arena's when two share a name).
        static Mdk::Dictionary<std::string, std::vector<byte>> ArenaSounds(const std::vector<std::shared_ptr<Formats::Arena>>& arenas);

        // A level of the 1996 demo: its own archives only (sounds it lacks stay silent).
        static std::shared_ptr<SoundBank> ForBeta(const Formats::BetaDemo& beta, int number);

        std::optional<SoundMixer::Entry> Get(const std::string& name);

        // Converts every sound of the archives now (a level's load: none while playing).
        void Preload();

        // Kurt's sprite animations stored with the sounds (the board's K_SURF), or null.
        std::shared_ptr<Formats::SpriteAnimation> Animation(const std::string& name) const;

    private:
        std::vector<std::shared_ptr<Formats::Sni>> _archives;
        std::unordered_map<std::string, std::optional<SoundMixer::Entry>> _cache;
        Mdk::Dictionary<std::string, std::vector<byte>> _arenaSounds;

        std::optional<SoundMixer::Entry> Load(const std::string& name) const;
    };
}
