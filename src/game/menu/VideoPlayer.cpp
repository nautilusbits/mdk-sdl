#include "game/menu/VideoPlayer.h"

#include "core/Files.h"
#include "engine/audio/AudioDevice.h"
#include "formats/Flc.h"
#include "formats/MdkData.h"
#include "formats/Mve.h"

namespace Mdk::Game::Menu
{
    using Formats::Flc;
    using Formats::Mve;

    bool VideoPlayer::Play(const std::string& path)
    {
        auto full = _ui.Data()->PathOf(path);
        _flc = File::Exists(full) ? Flc::Load(full) : nullptr;
        if (_flc == nullptr)
        {
            return false;
        }

        _view = ScreenView::Size;
        _time = 0.f;
        ShowNext();
        return true;
    }

    bool VideoPlayer::PlayMovie(const std::string& path)
    {
        auto full = _ui.Data()->PathOf(path);
        _mve = File::Exists(full) ? Mve::Load(full) : nullptr;
        if (_mve == nullptr)
        {
            return false;
        }

        _view = MovieScreen;
        _time = 0.f;
        if (!NextMovieFrame())
        {
            _mve = nullptr;
            return false;
        }

        _soundVoice = _ui.Audio().PlayStream(_mve->SampleRate(), 1.f);
        PushSound();
        return true;
    }

    void VideoPlayer::Brighten(float amount)
    {
        _brighten = amount;
        if (_image != nullptr)
        {
            _image->SetPalette(Brightened(_palette));
        }
    }

    void VideoPlayer::ShowStill(int width, int height, std::span<const byte> indices, std::span<const byte> palette)
    {
        if (_image == nullptr || _image->Width != width || _image->Height != height)
        {
            _image = std::make_shared<PalettedImage>(_ui.Renderer(), width, height, indices, palette);
        }
        else
        {
            _image->SetIndices(indices);
        }

        _palette.assign(palette.begin(), palette.end());
        _image->SetPalette(Brightened(_palette));
        _still = StillImage{width, height, std::vector<byte>(indices.begin(), indices.end()), _palette};
    }

    void VideoPlayer::Stop()
    {
        _flc = nullptr;
        _mve = nullptr;
        _ui.Audio().Stop(_soundVoice);
        _soundVoice = 0;
    }

    void VideoPlayer::Update(float delta)
    {
        if (_mve != nullptr)
        {
            UpdateMovie(delta);
            return;
        }

        if (_flc == nullptr)
        {
            return;
        }

        if (_hold > 0.f)
        {
            _hold -= delta;
            return;
        }

        _time += delta;
        while (_flc != nullptr && _hold <= 0.f && _time >= FrameSeconds)
        {
            _time -= FrameSeconds;
            ShowNext();
        }
    }

    void VideoPlayer::Draw()
    {
        if (_image == nullptr)
        {
            return;
        }

        auto& view = *_ui.View;
        view.Layout(ScreenView::Fit::Inside, _view);
        _image->Draw(view, (_view - _image->Size()) / 2.f);
        view.Layout(ScreenView::Fit::Inside);
    }

    void VideoPlayer::ShowNext()
    {
        if (!_flc->NextFrame())
        {
            Finish();
            return;
        }

        ShowStill(_flc->Width(), _flc->Height(), _flc->Indices, _flc->Palette);
        if (FrameShown)
        {
            FrameShown(_flc->Frame());
        }
    }

    void VideoPlayer::UpdateMovie(float delta)
    {
        _time += delta * MicrosecondsPerSecond;
        while (_mve != nullptr && (_time >= static_cast<float>(_mve->FrameTime()) || _ui.Audio().Queued(_soundVoice) < SoundLow))
        {
            _time = MathF::Max(_time - static_cast<float>(_mve->FrameTime()), 0.f);
            if (!NextMovieFrame())
            {
                Finish();
                return;
            }

            PushSound();
        }
    }

    bool VideoPlayer::NextMovieFrame()
    {
        if (!_mve->NextFrame())
        {
            return false;
        }

        ShowStill(_mve->Width(), _mve->Height(), _mve->Indices(), _mve->Palette);
        if (FrameShown)
        {
            FrameShown(_mve->Frame());
        }

        return true;
    }

    void VideoPlayer::PushSound()
    {
        if (_mve == nullptr || _mve->Audio.empty())
        {
            return;
        }

        std::vector<std::pair<float, float>> frames;
        frames.reserve(_mve->Audio.size());
        for (const auto& a : _mve->Audio)
        {
            frames.emplace_back(a.X, a.Y);
        }

        _ui.Audio().Push(_soundVoice, frames);
    }

    std::vector<byte> VideoPlayer::Brightened(std::span<const byte> palette) const
    {
        if (_brighten <= 0.f)
        {
            return std::vector<byte>(palette.begin(), palette.end());
        }

        auto raise = ToInt(MathF::Round(_brighten * MaxColour));
        std::vector<byte> raised;
        raised.reserve(palette.size());
        for (auto c : palette)
        {
            raised.push_back(static_cast<byte>(Math::Min(c + raise, MaxColour)));
        }

        return raised;
    }

    void VideoPlayer::Finish()
    {
        _flc = nullptr;
        _mve = nullptr;

        // Copy first: the handler may reassign Finished while running.
        auto finished = Finished;
        if (finished)
        {
            finished();
        }
    }
}
