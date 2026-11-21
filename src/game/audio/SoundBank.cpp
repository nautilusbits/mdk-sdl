#include "game/audio/SoundBank.h"

#include <format>

#include "formats/Wav.h"

namespace Mdk::Game::Audio
{
    using Engine::Audio::Looping;
    using Engine::Audio::Sound;

    SoundBank::SoundBank(std::vector<std::shared_ptr<Formats::Sni>> archives,
        Mdk::Dictionary<std::string, std::vector<byte>> arenaSounds)
        : _archives(std::move(archives)), _arenaSounds(std::move(arenaSounds))
    {
    }

    std::shared_ptr<SoundBank> SoundBank::ForLevel(const Formats::MdkData& data, int level,
        const std::vector<std::shared_ptr<Formats::Arena>>& arenas)
    {
        auto dir = std::format("TRAVERSE/LEVEL{}/LEVEL{}", level, level);
        return std::make_shared<SoundBank>(std::vector<std::shared_ptr<Formats::Sni>>{
            Formats::Sni::Load(data.PathOf("TRAVERSE/TRAVERSE.SNI")),
            Formats::Sni::Load(data.PathOf(dir + "S.SNI")),
            Formats::Sni::Load(data.PathOf(dir + "O.SNI")),
        }, ArenaSounds(arenas));
    }

    Mdk::Dictionary<std::string, std::vector<byte>> SoundBank::ArenaSounds(const std::vector<std::shared_ptr<Formats::Arena>>& arenas)
    {
        Mdk::Dictionary<std::string, std::vector<byte>> sounds;
        for (const auto& arena : arenas)
        {
            for (const auto& [name, wav] : arena->Sounds)
            {
                sounds.TryAdd(name, wav);
            }
        }

        return sounds;
    }

    std::shared_ptr<SoundBank> SoundBank::ForBeta(const Formats::BetaDemo& beta, int number)
    {
        return std::make_shared<SoundBank>(beta.LoadSounds(Formats::BetaDemo::LevelOf(number)));
    }

    std::optional<SoundMixer::Entry> SoundBank::Get(const std::string& name)
    {
        auto it = _cache.find(name);
        if (it != _cache.end())
        {
            return it->second;
        }

        return _cache[name] = Load(name);
    }

    void SoundBank::Preload()
    {
        for (const auto& archive : _archives)
        {
            for (const auto& [name, entry] : archive->Entries)
            {
                if (archive->IsSound(entry))
                {
                    Get(name);
                }
            }
        }

        for (const auto& [name, bytes] : _arenaSounds)
        {
            Get(name);
        }
    }

    std::shared_ptr<Formats::SpriteAnimation> SoundBank::Animation(const std::string& name) const
    {
        for (auto i = static_cast<int>(_archives.size()) - 1; i >= 0; i--)
        {
            if (auto animation = _archives[i]->GetAnimation(name))
            {
                return animation;
            }
        }

        return nullptr;
    }

    std::optional<SoundMixer::Entry> SoundBank::Load(const std::string& name) const
    {
        for (auto i = static_cast<int>(_archives.size()) - 1; i >= 0; i--)
        {
            const auto& archive = _archives[i];
            const Formats::Sni::Entry* found = nullptr;
            for (const auto& [key, entry] : archive->Entries)
            {
                if (key == name)
                {
                    found = &entry;
                    break;
                }
            }

            if (found == nullptr || !archive->IsSound(*found))
            {
                continue;
            }

            auto wav = Formats::Wav::Parse(archive->GetBytes(*found));
            if (!wav.has_value())
            {
                return std::nullopt;
            }

            auto looping = (found->Flags & Formats::Sni::FlagLoop) != 0 ? Looping::Forever : Looping::Once;
            auto sound = Sound::FromPcm(wav->Data, wav->Channels, wav->SampleRate, wav->BitsPerSample, looping);
            return SoundMixer::Entry{sound, found->Volume};
        }

        // An arena's own sound: a plain WAV, played once at full volume (as the archives' effects are).
        const auto* bytes = _arenaSounds.Find(name);
        if (bytes == nullptr)
        {
            return std::nullopt;
        }

        auto arenaWav = Formats::Wav::Parse(*bytes);
        if (!arenaWav.has_value())
        {
            return std::nullopt;
        }

        auto sound = Sound::FromPcm(arenaWav->Data, arenaWav->Channels, arenaWav->SampleRate, arenaWav->BitsPerSample, Looping::Once);
        return SoundMixer::Entry{sound, SoundMixer::FullVolume};
    }
}
