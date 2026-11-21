#include "game/scripts/StrikeScene.h"

#include <algorithm>

#include "core/DotNet.h"
#include "formats/Model.h"
#include "formats/ModelAnimation.h"
#include "game/kurt/Kurt.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/Items.h"
#include "game/scripts/ScriptMath.h"
#include "game/scripts/ScriptRuntime.h"

namespace Mdk::Game::Scripts
{
    using Objects::MdkObject;

    const float StrikeScene::FieldOfView = ScriptMath::ToDegrees(2.0f * MathF::Atan(ViewHeight / 2.0f / Focal));

    StrikeScene::StrikeScene()
        : Subject(std::make_shared<MdkObject>())
    {
    }

    StrikeScene::View StrikeScene::Camera() const
    {
        auto turn = Matrix4x4::CreateRotationZ(ScriptMath::ToRadians(_yaw));
        return {Vector3::Transform(ReferencePoint(CameraPoint), turn), Vector3::Transform(ReferencePoint(LookPoint), turn)};
    }

    void StrikeScene::Play(ScriptRuntime& runtime, Kind kind, Plane plane)
    {
        auto arena = runtime.CurrentArena;
        std::string name = kind == Kind::Bones ? "X_STRIKB" : "X_STRIKD";
        auto model = runtime.FindModel(arena, name);
        _animation = kind == Kind::Bones ? runtime.Items->GetAnimation(name) : runtime.FindArenaAnimation(arena, name);
        if (model == nullptr || _animation == nullptr || _animation->FrameCount == 0 || static_cast<int>(model->ReferencePoints.size()) < NeededPoints)
        {
            if (Finished)
            {
                Finished();
            }

            return;
        }

        Subject->TypeName = name;
        Subject->Model = model;
        Subject->Arena = arena;
        Subject->Animation = _animation;
        Subject->HiddenParts = plane == Plane::Only ? HiddenParts(*model) : 0;
        Active = true;
        Show();
    }

    int StrikeScene::HiddenParts(const Formats::Model& model)
    {
        auto hidden = 0;
        for (auto i = 0; i < static_cast<int>(model.PartList.size()); i++)
        {
            auto upper = Str::ToUpper(model.PartList[i]->Name);
            if (std::find(PlaneParts.begin(), PlaneParts.end(), upper) == PlaneParts.end())
            {
                hidden |= 1 << (i & 31);
            }
        }

        return hidden;
    }

    void StrikeScene::Update(float delta)
    {
        if (!Active)
        {
            return;
        }

        _frame += delta * Game::Kurt::Kurt::Ticks * _animation->Speed;
        _yaw += TurnRate * delta;
        if (_frame >= _animation->FrameCount)
        {
            Skip();
            return;
        }

        Show();
    }

    void StrikeScene::Skip()
    {
        if (!Active)
        {
            return;
        }

        Active = false;
        if (Finished)
        {
            Finished();
        }
    }

    void StrikeScene::Show()
    {
        Subject->AnimationFrame = ToInt(_frame);
        Subject->Yaw = _yaw;
    }

    Vector3 StrikeScene::ReferencePoint(int index) const
    {
        if (_animation != nullptr && index < static_cast<int>(_animation->ReferencePoints.size()))
        {
            return _animation->ReferencePoints[index][Math::Min(ToInt(_frame), _animation->FrameCount - 1)];
        }

        return Subject->Model != nullptr ? Subject->Model->ReferencePoints[index] : Vector3::Zero();
    }
}
