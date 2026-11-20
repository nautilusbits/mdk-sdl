#include "Mdk.Game/Menu/Slideshow.h"

#include <format>

#include "Mdk.Core/Files.h"
#include "Mdk.Formats/Gif.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Game/Menu/Ui.h"

namespace Mdk::Game::Menu
{
    void Slideshow::Start()
    {
        _last = _video.Still();
        _running = true;
        _time = 0.f;
        if (_images.empty())
        {
            Load();
        }
    }

    void Slideshow::Update(float delta)
    {
        if (!_running || _images.empty())
        {
            return;
        }

        _time += delta;
        if (_time >= Duration())
        {
            Next();
        }
    }

    void Slideshow::Reset()
    {
        _time = _image == Bare ? Duration() : 0.f;
    }

    float Slideshow::Duration() const
    {
        switch (_image)
        {
            case 0:
                return FirstDelay;
            case Bare:
                return FirstTime;
            default:
                return NextTime;
        }
    }

    void Slideshow::Next()
    {
        _time = 0.f;
        _image = (_image + 1) % (static_cast<int>(_images.size()) + 1);
        if (_image == 0)
        {
            if (_last.has_value())
            {
                const auto& last = *_last;
                _video.ShowStill(last.Width, last.Height, last.Indices, last.Palette);
            }

            return;
        }

        const auto& gif = _images[_image - 1];
        _video.ShowStill(gif->Width(), gif->Height(), gif->Indices(), gif->Palette);
    }

    void Slideshow::Load()
    {
        for (auto i = 1;; i++)
        {
            auto path = _ui.Data()->PathOf(std::vformat(ImagePath, std::make_format_args(i)));
            auto gif = File::Exists(path) ? Formats::Gif::Load(path) : nullptr;
            if (gif == nullptr)
            {
                return;
            }

            _images.push_back(gif);
        }
    }
}
