#include "Mdk.Game/SniperView.h"

#include "Mdk.Core/DotNet.h"
#include "Mdk.Formats/SniperScreen.h"
#include "Mdk.Game/Angles.h"
#include "Mdk.Game/FrameBlend.h"
#include "Mdk.Game/Hud/HudView.h"
#include "Mdk.Game/Kurt/FollowCamera.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Kurt/Scope.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/AirStrike.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Mdk.Game/Scripts/StrikeScene.h"

namespace Mdk::Game
{
    using Engine::Render::InsetContent;
    using Engine::Render::InsetLayer;
    using Formats::SniperScreen;
    using Hud::SniperHud;
    using Hud::SniperOverlay;
    using Hud::StrikeAim;
    using Kurt::CameraMath;
    using Kurt::FollowCamera;
    using Kurt::Scope;
    using Objects::MdkObject;
    using Objects::ObjectView;
    using Scripts::ScriptRuntime;
    using Scripts::SniperRounds;
    using Scripts::StrikeScene;

    SniperView::SniperView(Engine::Render::Renderer& renderer, ObjectView& objects, Level::LevelData& level, Hud::HudView& hud)
        : _renderer(renderer), _objects(objects), _level(level), _hud(hud),
          _clip{ std::make_shared<MdkObject>(), std::make_shared<MdkObject>(), std::make_shared<MdkObject>() }
    {
    }

    void SniperView::Draw(Kurt::Kurt& kurt, ScriptRuntime& scripts, FrameBlend& blend, float delta)
    {
        for (const auto& round : scripts.SniperRounds->Visuals())
        {
            _objects.Draw(*round, LookOf(round->Arena), scripts.SniperRounds->TransformOf(*round, blend.Tick()));
        }

        if (!kurt.Sniping)
        {
            _hud.Sniper->Update(delta, std::nullopt);
            return;
        }

        auto& cameras = _cameras;
        cameras.clear();
        for (auto i = 0; i < SniperRounds::Slots; i++)
        {
            cameras.push_back(scripts.SniperRounds->Camera(i, blend.Tick()));
        }

        auto ammo = kurt.Inventory->Ammo();
        _hud.Sniper->Update(delta, SniperHud{ blend.Zoom, std::vector<int>(ammo.begin(), ammo.end()), kurt.Inventory->SelectedAmmo, cameras,
            Aim(kurt, scripts) });
        auto [scale, left] = SniperOverlay::Placement(_renderer.CanvasWidth());
        for (size_t i = 0; i < cameras.size(); i++)
        {
            DrawRoundCamera(cameras[i], OnCanvas(SniperOverlay::RoundViews[i], scale, left));
        }

        DrawClip(kurt, scripts, blend.ClipTime, OnCanvas(RectangleF(0.f, 0.f, SniperScreen::ViewWidth, SniperScreen::ViewHeight), scale, left));
    }

    StrikeAim SniperView::Aim(Kurt::Kurt& kurt, ScriptRuntime& scripts)
    {
        if (kurt.Inventory->SelectedAmmo != Scope::Strike || kurt.Scope->ClipTime > 0.f)
        {
            return StrikeAim::NotChecked;
        }

        return scripts.AirStrike->FindTarget(kurt.SniperEye(), kurt.SniperForward()).has_value() ? StrikeAim::Target : StrikeAim::None;
    }

    RectangleF SniperView::OnCanvas(RectangleF inView, float scale, float left)
    {
        return RectangleF(left + (SniperOverlay::View.X + inView.X) * scale, (SniperOverlay::View.Y + inView.Y) * scale, inView.Width * scale,
            inView.Height * scale);
    }

    void SniperView::DrawRoundCamera(const SniperRounds::RoundCamera& camera, RectangleF area)
    {
        if (camera.Shot != SniperRounds::Shot::Watching)
        {
            return;
        }

        auto aspect = area.Width / area.Height;
        auto fieldOfView = RadiansToDegrees(2.f * MathF::Atan(MathF::Tan(DegreesToRadians(RoundFieldWidth) / 2.f) / aspect));
        auto view = CameraMath::View(camera.Position, camera.Forward, FollowCamera::UpOf(camera.Forward), fieldOfView, aspect, RoundNear, Far);
        _renderer.BeginInset(view, area, InsetContent::Scene, InsetLayer::UnderCanvas);
        _renderer.EndInset();
    }

    void SniperView::DrawClip(Kurt::Kurt& kurt, ScriptRuntime& scripts, float clip, RectangleF area)
    {
        auto model = scripts.FindModel(scripts.CurrentArena, RoundModels[kurt.Inventory->SelectedAmmo]);
        if (model == nullptr)
        {
            return;
        }

        auto fieldOfView = RadiansToDegrees(2.f * MathF::Atan(SniperScreen::ViewHeight / 2.f / ClipFocal));
        auto view = CameraMath::View(Vector3::Zero(), -Vector3::UnitY(), -Vector3::UnitZ(), fieldOfView, area.Width / area.Height, ClipNear, ClipFar);
        auto look = LookOf(scripts.CurrentArena);
        auto open = false;
        for (auto i = 0; i < Scope::ClipSize; i++)
        {
            auto t = clip + i;
            if (i >= kurt.Scope->ClipRounds || t == 0.f || t >= ClipKeys.size() - 1)
            {
                continue;
            }

            if (!open)
            {
                _renderer.BeginInset(view, area, InsetContent::Own, InsetLayer::OverCanvas);
                open = true;
            }

            auto& round = *_clip[i];
            round.Model = model;
            round.Flags = MdkObject::FlagRolling;
            std::tie(round.RollingBasis, round.Position, round.Scale) = ClipPlace(t);
            _objects.Draw(round, look);
        }

        if (open)
        {
            _renderer.EndInset();
        }
    }

    std::tuple<Matrix4x4, Vector3, float> SniperView::ClipPlace(float t)
    {
        auto k = ToInt(t);
        auto f = t - k;
        const auto& a = ClipKeys[k];
        const auto& b = ClipKeys[k + 1];
        auto angles = Vector3::Lerp(a.Angles, b.Angles, f);
        auto rotation = Matrix4x4::CreateRotationX(DegreesToRadians(angles.Z))
            * Matrix4x4::CreateRotationY(-DegreesToRadians(angles.Y))
            * Matrix4x4::CreateRotationZ(DegreesToRadians(angles.X));
        return { rotation, Vector3::Lerp(a.Position, b.Position, f), MathF::Lerp(a.Scale, b.Scale, f) };
    }

    std::optional<Engine::Render::View> SniperView::DrawStrike(ScriptRuntime& scripts, StrikeSkip skip, float delta)
    {
        auto strike = scripts.Strike;
        if (strike == nullptr || !strike->Active)
        {
            return std::nullopt;
        }

        if (skip == StrikeSkip::Now)
        {
            strike->Skip();
            return std::nullopt;
        }

        strike->Update(delta);
        if (!strike->Active)
        {
            return std::nullopt;
        }

        auto [eye, target] = strike->Camera();
        auto forward = Vector3::Normalize(target - eye);
        _objects.Draw(*strike->Subject, LookOf(strike->Subject->Arena));
        return CameraMath::View(eye, forward, FollowCamera::UpOf(forward), CameraMath::KeepWidth(StrikeScene::FieldOfView, _renderer.AspectRatio()),
            _renderer.AspectRatio(), RoundNear, Far);
    }

    ObjectView::Look SniperView::LookOf(const std::string& name)
    {
        auto found = _looks.find(name);
        if (found != _looks.end())
        {
            return found->second;
        }

        auto arena = _level.ArenaNamed(name);
        return _looks[name] = arena != nullptr
            ? ObjectView::Look{ _level.PaletteOf(*arena), _level.ArchivesOf(*arena) }
            : ObjectView::Look{ _level.Dti->Palette, { _level.LevelTextures } };
    }
}
