#include "Mdk.Formats/Bni.h"

#include "Mdk.Core/Files.h"

namespace Mdk::Formats
{
    Bni::Bni(SharedBytes bytes) : _index(ArchiveIndex::Read(std::move(bytes), 12)) {}

    std::shared_ptr<Bni> Bni::Load(const std::string& path) { return std::shared_ptr<Bni>(new Bni(Share(File::ReadAllBytes(path)))); }

    void Bni::Add(const std::string& name, std::span<const byte> data)
    {
        _index->Add(name, data);
        _images.erase(name);
        _animations.erase(name);
    }

    std::shared_ptr<Texture> Bni::GetImage(const std::string& name)
    {
        auto& image = _images[name];
        if (!image)
        {
            image = Texture::Parse(name, *Bytes(), _index->Entries.At(name).Offset);
        }

        return image;
    }

    std::shared_ptr<SpriteAnimation> Bni::GetAnimation(const std::string& name)
    {
        auto& animation = _animations[name];
        if (!animation)
        {
            animation = SpriteAnimation::Parse(name, Bytes(), _index->Entries.At(name).Offset + 4);
        }

        return animation;
    }
}
