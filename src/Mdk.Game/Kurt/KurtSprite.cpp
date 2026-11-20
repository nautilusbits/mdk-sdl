#include "Mdk.Game/Kurt/KurtSprite.h"

#include <algorithm>
#include <array>

#include "Mdk.Game/Angles.h"
#include "Mdk.Game/Mods/ModImages.h"

namespace Mdk::Game::Kurt
{
    using Engine::Render::Material;
    using Engine::Render::Pass;
    using Engine::Render::Shading;
    using Engine::Render::Vertex;

    namespace
    {
        // Appends name unless it's there already (LINQ Distinct: first appearance order).
        void AddDistinct(std::vector<std::string>& names, const std::string& name)
        {
            if (std::find(names.begin(), names.end(), name) == names.end())
            {
                names.push_back(name);
            }
        }
    }

    const Mdk::Dictionary<Kurt::State, KurtSprite::StateAnimation>& KurtSprite::Animations()
    {
        static const Mdk::Dictionary<Kurt::State, StateAnimation> animations = []
        {
            Mdk::Dictionary<Kurt::State, StateAnimation> d;
            d[Kurt::State::Still] = {"K_STILL", Repeat::Once};
            d[Kurt::State::Idle] = {"K_IDLE", Repeat::Once};
            d[Kurt::State::Run] = {"K_RUN", Repeat::Loop};
            d[Kurt::State::Side] = {"K_SIDE", Repeat::Loop};
            d[Kurt::State::Turn] = {"K_TRN45", Repeat::Loop};
            d[Kurt::State::Jump] = {"K_JUMP", Repeat::Once};
            d[Kurt::State::Jump] = {"K_JUMP", Repeat::Once};
            d[Kurt::State::RunJump] = {"K_RJMP", Repeat::Once};
            d[Kurt::State::Fall] = {"K_FALL", Repeat::Loop};
            d[Kurt::State::Chute] = {ChuteOpening, Repeat::Once};
            d[Kurt::State::Land] = {"K_LAND", Repeat::Once};
            d[Kurt::State::Shot] = {"K_SHOT", Repeat::Loop};
            d[Kurt::State::RunFire] = {"K_RUNFIR", Repeat::Loop};
            d[Kurt::State::Throw] = {"K_SPWEP", Repeat::Once};
            d[Kurt::State::Knocked] = {"K_BANG", Repeat::Once};
            d[Kurt::State::GetUp] = {"K_BFLIP", Repeat::Once};
            d[Kurt::State::Dead] = {"K_BANG", Repeat::Once};
            d[Kurt::State::Slip] = {"K_SLIP", Repeat::Once};
            d[Kurt::State::Slide] = {"K_SLIDE", Repeat::Loop};
            d[Kurt::State::SlideFast] = {"K_FSLIDE", Repeat::Loop};
            // Braking holds K_BSLIDE's last frame (0x4646a4, state 0x32a).
            d[Kurt::State::SlideBrake] = {"K_BSLIDE", Repeat::Once};
            d[Kurt::State::Hang] = {"K_HANG", Repeat::Once};
            d[Kurt::State::HardLand] = {"K_CRASHL", Repeat::Once};
            d[Kurt::State::RollLeft] = {"K_ROLLL", Repeat::Once};
            d[Kurt::State::RollRight] = {"K_ROLLR", Repeat::Once};
            d[Kurt::State::HelmetOn] = {Helmet, Repeat::Once};
            d[Kurt::State::HelmetOff] = {Helmet, Repeat::Once};
            return d;
        }();
        return animations;
    }

    // Kurt's frames kept in the level's archives: the snowboard's (LEVEL4S.SNI), the
    // slide's (LEVEL6S.SNI).
    const std::vector<std::string>& KurtSprite::LevelAnimations()
    {
        static const std::vector<std::string> names = {"K_SURF", "K_SURFJ", "K_SLIP", "K_SLIDE", "K_FSLIDE", "K_BSLIDE"};
        return names;
    }

    // The animations Kurt is drawn with: TRAVSPRT.BNI's, then the levels' (the HD textures' export).
    std::vector<std::string> KurtSprite::Drawn()
    {
        std::vector<std::string> names;
        for (const auto& [state, animation] : Animations())
        {
            AddDistinct(names, animation.Name);
        }

        AddDistinct(names, ChuteSway);
        AddDistinct(names, MuzzleFlash);
        AddDistinct(names, BackUp);
        for (const std::string& name : LevelAnimations())
        {
            AddDistinct(names, name);
        }

        return names;
    }

    KurtSprite::KurtSprite(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Bni> sprites, std::shared_ptr<Formats::Palette> palette,
        Shading shading, std::shared_ptr<Mods::ModImages> mods)
        : _renderer(renderer), _sprites(std::move(sprites)), _shading(shading), _colours(std::move(palette)), _mods(std::move(mods))
    {
        _palette = renderer.CreatePalette(_colours->Rgba);
        _mesh = renderer.CreateDynamicMesh(FrameVertices);
        _muzzleMesh = renderer.CreateDynamicMesh(QuadVertices);
        std::vector<std::string> names;
        for (const auto& [state, animation] : Animations())
        {
            AddDistinct(names, animation.Name);
        }

        AddDistinct(names, ChuteSway);
        AddDistinct(names, MuzzleFlash);
        AddDistinct(names, BackUp);
        std::vector<std::shared_ptr<Formats::SpriteAnimation>> animations;
        for (const std::string& name : names)
        {
            if (_sprites->Has(name))
            {
                animations.push_back(_sprites->GetAnimation(name));
            }
        }

        Prefetch(animations);
        for (const auto& animation : animations)
        {
            Preload(*animation);
        }
    }

    // Decodes the frames' mod images on every core before they're asked for one by one
    // (the HD textures' Kurt: 360 files a level's load).
    void KurtSprite::Prefetch(const std::vector<std::shared_ptr<Formats::SpriteAnimation>>& animations)
    {
        if (_mods == nullptr || _shading != Shading::Sprite)
        {
            return;
        }

        std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>> frames;
        for (const auto& animation : animations)
        {
            for (int frame = 0; frame < animation->FrameCount(); frame++)
            {
                frames.emplace_back(FrameName(animation->Name(), frame), animation->GetFrame(frame).Image);
            }
        }

        _mods->Prefetch(frames, *_colours);
    }

    // Decodes an animation's frames and puts them on the GPU, HD when made (a level's load).
    void KurtSprite::Preload(Formats::SpriteAnimation& animation)
    {
        for (int frame = 0; frame < animation.FrameCount(); frame++)
        {
            std::shared_ptr<Formats::Texture> image = animation.GetFrame(frame).Image;
            Material material = MaterialOf(animation.Name(), frame, *image);
            if (std::optional<HdTextures::HdImage> hd = Upscaled(_mods.get(), _shading, FrameName(animation.Name(), frame), *image, *_colours))
            {
                _renderer.Replace(material, hd->Width, hd->Height, std::move(hd->Rgba));
            }
        }
    }

    // A frame's mod image (HD textures too): the enhanced look's sprites only.
    std::optional<HdTextures::HdImage> KurtSprite::Upscaled(Mods::ModImages* mods, Shading shading, const std::string& name,
        const Formats::Texture& image, const Formats::Palette& palette)
    {
        if (shading != Shading::Sprite || mods == nullptr)
        {
            return std::nullopt;
        }

        return mods->Texture(name, image, palette);
    }

    // A frame's name in mods: the animation's, then its number (K_RUN_0).
    std::string KurtSprite::FrameName(const std::string& animation, int frame)
    {
        return animation + "_" + std::to_string(frame);
    }

    Material KurtSprite::MaterialOf(const std::string& name, int frame, const Formats::Texture& image)
    {
        Material material(Texture(name, frame, image), _palette, Vector4::One(), 1, Pass::DoubleSided, 0.f, _shading);
        _renderer.Prepare(material);
        return material;
    }

    int KurtSprite::FrameCount(Kurt::State state) const
    {
        return FrameCount(Animations().At(state).Name);
    }

    // Adds an animation of the level's (null: none there).
    void KurtSprite::Add(const std::shared_ptr<Formats::SpriteAnimation>& animation)
    {
        if (animation != nullptr)
        {
            _extra[animation->Name()] = animation;
            Prefetch({animation});
            Preload(*animation);
        }
    }

    // Frames of an animation of the level's or of TRAVSPRT.BNI (0 when missing).
    int KurtSprite::FrameCount(const std::string& name) const
    {
        auto found = _extra.find(name);
        if (found != _extra.end())
        {
            return found->second->FrameCount();
        }

        return _sprites->Has(name) ? _sprites->GetAnimation(name)->FrameCount() : 0;
    }

    std::shared_ptr<Formats::SpriteAnimation> KurtSprite::Animation(const std::string& name) const
    {
        auto found = _extra.find(name);
        return found != _extra.end() ? found->second : _sprites->GetAnimation(name);
    }

    // Queues Kurt's frame for the camera at eye looking along
    // forward with up, the view fieldOfView
    // degrees high, his feet at feet (a frame between steps); the
    // space's floors he stands above don't hide him.
    void KurtSprite::Draw(const Kurt& kurt, Vector3 feet, Vector3 eye, Vector3 forward, Vector3 up, float fieldOfView, Collision::ArenaSpace& space)
    {
        // World units per sprite pixel at the feet's depth.
        float depth = Vector3::Dot(feet - eye, forward);
        if (!kurt.Visible || kurt.Sniping || depth <= 0.f)
        {
            return;
        }

        float pixel = 2.f * depth * MathF::Tan(DegreesToRadians(fieldOfView) / 2.f) / ViewHeight;
        Vector3 right = Vector3::Normalize(Vector3::Cross(forward, up));
        auto [name, frame] = Pick(kurt);
        Quad quad{feet, right * (pixel * Mirror(kurt)), up * pixel};
        float rise = FeetRise / pixel;
        std::optional<float> floor = FloorDepth(space, eye, forward, quad, name, frame, rise);
        float body = InFront(depth, floor);
        DrawFrame(_mesh, name, frame, quad, 0, 0, Pull{eye, body, Nearer(depth, floor), rise});

        // The flash's hotspot is at Kurt's hotspot plus its offset; it comes nearer with him.
        if (kurt.Muzzle.has_value())
        {
            const Kurt::MuzzleFlash& muzzle = *kurt.Muzzle;
            Quad behind = quad;
            behind.Feet = feet + forward * MuzzleBehind;
            DrawFrame(_muzzleMesh, MuzzleFlash, muzzle.Frame, behind, muzzle.X, muzzle.Y, Pull{eye, body, body, 0.f});
        }
    }

    // Draws a frame with its hotspot 101 pixels above the feet, moved by (dx, dy) pixels (y down),
    // brought nearer by the pull.
    void KurtSprite::DrawFrame(int mesh, const std::string& name, int frame, const Quad& at, int dx, int dy, const Pull& pull)
    {
        Formats::SpriteAnimation::Frame image = Animation(name)->GetFrame(frame);
        int w = image.Image->Width;
        int h = image.Image->Height;
        int ax = image.HotspotX - dx;
        int ay = image.HotspotY + FeetOffset - dy;

        std::array<Vertex, FrameVertices> vertices{};
        float split = Math::Clamp<float>(ay - pull.Rise, 0.f, static_cast<float>(h));
        int count = Rows(vertices, at, static_cast<float>(ax), static_cast<float>(ay), w, h, 0.f, split, pull.Eye, pull.Body);
        count += Rows(std::span<Vertex>(vertices).subspan(count), at, static_cast<float>(ax), static_cast<float>(ay), w, h, split,
            static_cast<float>(h), pull.Eye, pull.Feet);

        _renderer.UpdateMesh(mesh, std::span<const Vertex>(vertices.data(), count));
        _renderer.Draw(mesh, 0, count, MaterialOf(name, frame, *image.Image));
    }

    // The nearest depth of a floor the eye's rays to a frame's corners and middle (its
    // top, rise pixels above the feet, its bottom) cross, among those the feet
    // stand above (drawn before Kurt in the original), or null.
    std::optional<float> KurtSprite::FloorDepth(Collision::ArenaSpace& space, Vector3 eye, Vector3 forward, const Quad& at, const std::string& name,
        int frame, float rise)
    {
        Formats::SpriteAnimation::Frame image = Animation(name)->GetFrame(frame);
        int w = image.Image->Width;
        int h = image.Image->Height;
        int ax = image.HotspotX;
        int ay = image.HotspotY + FeetOffset;
        const std::array<float, 3> columns = {0.f, static_cast<float>(ax), static_cast<float>(w)};
        const std::array<float, 3> rows = {0.f, Math::Clamp<float>(ay - rise, 0.f, static_cast<float>(h)), static_cast<float>(h)};
        std::optional<float> nearest;
        for (size_t i = 0; i < columns.size() * rows.size(); i++)
        {
            Vector3 point = Corner(at, static_cast<float>(ax), static_cast<float>(ay), columns[i % columns.size()], rows[i / columns.size()]);
            std::optional<Collision::ArenaSpace::Surface> floor = space.FloorCrossing(eye, point);
            if (!floor.has_value() || Vector3::Dot(at.Feet - floor->Point, floor->Normal()) < -FloorBelow)
            {
                continue;
            }

            float depth = Vector3::Dot(floor->Point - eye, forward);
            nearest = MathF::Min(nearest.value_or(depth), depth);
        }

        return nearest;
    }

    // The quad of a frame's rows top to bottom, scaled
    // towards the eye. Returns its vertex count (0 when empty).
    int KurtSprite::Rows(std::span<Vertex> into, const Quad& at, float ax, float ay, int w, int h, float top, float bottom, Vector3 eye, float scale)
    {
        if (bottom <= top)
        {
            return 0;
        }

        auto V = [&](float px, float py)
        {
            return Vertex(eye + (Corner(at, ax, ay, px, py) - eye) * scale, Vector2(px / w, py / h));
        };
        into[0] = V(0.f, top);
        into[1] = V(static_cast<float>(w), top);
        into[2] = V(static_cast<float>(w), bottom);
        into[3] = V(0.f, top);
        into[4] = V(static_cast<float>(w), bottom);
        into[5] = V(0.f, bottom);
        return QuadVertices;
    }

    // The animation and frame of Kurt's state; the chute opens, then sways.
    std::pair<std::string, int> KurtSprite::Pick(const Kurt& kurt) const
    {
        // A ride's or the end's frame; past the last it loops (K_FLOATC).
        if (kurt.Pose.has_value())
        {
            const Kurt::PoseFrame& pose = *kurt.Pose;
            int poseFrames = FrameCount(pose.Name);
            if (poseFrames > 0)
            {
                return {pose.Name, ((pose.Frame % poseFrames) + poseFrames) % poseFrames};
            }
        }

        if (kurt.ChuteClosing)
        {
            return {ChuteOpening, ChuteClosedFrame};
        }

        const StateAnimation& animation = Animations().At(kurt.Current);
        std::string name = animation.Name;
        Repeat repeat = animation.Repeat;
        if (kurt.BackingUp() && FrameCount(BackUp) > 0)
        {
            name = BackUp;
        }

        int count = Animation(name)->FrameCount();
        int frame = ToInt(MathF::Floor(kurt.AnimationFrame));
        if (kurt.Current == Kurt::State::Chute && frame >= ChuteOpenedFrame)
        {
            int last = _sprites->GetAnimation(ChuteSway)->FrameCount() - 1;
            int k = (frame - ChuteOpenedFrame) % (last * 2);
            return {ChuteSway, k <= last ? k : last * 2 - k};
        }

        frame = repeat == Repeat::Loop ? ((frame % count) + count) % count : Math::Clamp(frame, 0, count - 1);
        return {name, kurt.Current == Kurt::State::HelmetOff ? count - 1 - frame : frame};
    }

    int KurtSprite::Texture(const std::string& name, int frame, const Formats::Texture& image)
    {
        auto key = std::make_pair(name, frame);
        auto found = _textures.find(key);
        if (found != _textures.end())
        {
            return found->second;
        }

        int id = _renderer.CreateIndexTexture(image.Width, image.Height, image.Indices);
        _textures[key] = id;
        return id;
    }
}
