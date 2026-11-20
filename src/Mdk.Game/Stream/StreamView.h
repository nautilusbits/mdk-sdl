#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Mdk.Core/Drawing.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/ModelAnimation.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/TextureArchive.h"
#include "Mdk.Game/Level/MaterialResolver.h"
#include "Mdk.Game/Stream/Flight.h"
#include "Mdk.Game/Stream/StreamTube.h"

namespace Mdk::Game::Mods
{
    class ModImages;
}

namespace Mdk::Game::Stream
{
    // Draws the stream (0x436b00, 0x438bfc): the scrolling background far behind, the models,
    // the translucent tube far to near, the lights and the planet, the health box and the fade.
    //   background quad (far) ─► Kurt, Bones, bonus / Gunter ─► tube (blended, far to near) ─► lights
    //   ─► canvas: health box, fade (red added, white, black)
    // The tube's vertices carry their RGBA (ramp colour, level alpha), Gouraud-blended as the
    // original's direct-colour triangles.
    class StreamView
    {
    public:
        StreamView(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Bni> bni, std::shared_ptr<Formats::Palette> palette,
            std::shared_ptr<Formats::TextureArchive> archive, std::shared_ptr<Mods::ModImages> mods = nullptr);

        // The camera's view for the window's aspect: 250-pixel focal length on the 360-high view.
        Engine::Render::View ViewOf(const Flight& flight) const;

        // Queues the whole frame.
        void Draw(const Flight& flight, const std::string& kurtAnimation, const std::string& otherModel, const std::string& otherAnimation);

        // An animation of STREAM.BNI (KURTANIM, BONESANIM, SWHANM, GUNTANIM).
        std::shared_ptr<Formats::ModelAnimation> AnimationOf(const std::string& name);

    private:
        static constexpr float ViewHeight = 360.f;
        static constexpr float Focal = 250.f;
        static constexpr float Near = 0.05f;
        static constexpr float Far = 1000.f;
        // The background quad stays this far ahead, behind everything in the tube.
        static constexpr float BackgroundDistance = 900.f;
        // Sprites are drawn a quarter of their size (lights), the planet half.
        static constexpr float LightScale = 0.25f;
        static constexpr float PlanetScale = 0.5f;
        static constexpr int SpriteCapacity = 256;
        static constexpr int QuadVertices = 6;
        static constexpr int TubeCapacity = StreamTube::Rings * StreamTube::Points * 2 * 3;
        // The health box (0x420830): SC_STAT at the bottom right, SNIP_TXT digits, blinking at 20 or less.
        static constexpr float PanelRight = 16.f;
        static constexpr float PanelBottom = 10.f;
        static constexpr int DigitWidth = 8;
        static constexpr int MaxNumber = 999;
        static constexpr int LowHealth = 20;
        static constexpr int BlinkOn = 16;

        struct MeshBatch
        {
            int First = 0;
            int Count = 0;
            Engine::Render::Material Material;
        };

        struct PoseMesh
        {
            int Mesh = 0;
            std::vector<MeshBatch> Batches;
        };

        struct Image
        {
            int Texture = 0;
            Vector2 Size{};
        };

        using Parts = std::vector<std::vector<Vector3>>;

        Engine::Render::Renderer& _renderer;
        std::shared_ptr<Formats::Bni> _bni;
        std::shared_ptr<Formats::Palette> _palette;
        std::vector<std::shared_ptr<Formats::TextureArchive>> _archives;
        std::shared_ptr<Level::MaterialResolver> _resolver;
        std::map<std::tuple<std::string, std::optional<std::string>, int>, std::optional<PoseMesh>> _meshes;
        std::unordered_map<std::string, std::shared_ptr<Formats::Model>> _models;
        std::unordered_map<std::string, std::shared_ptr<Formats::ModelAnimation>> _animations;
        std::map<std::pair<std::string, std::string>, std::vector<Parts>> _baked;
        std::shared_ptr<Mods::ModImages> _mods;
        int _paletteId = 0;
        int _tubeMesh = 0;
        int _spriteMesh = 0;
        int _backMesh = 0;
        Image _background;
        Image _light;
        Image _planet;
        Image _panel;
        Image _digits;
        std::vector<TubeVertex> _tubeVertices;
        std::vector<Engine::Render::Vertex> _vertices;

        // An image of the archive; a mod's of its name replaces it on the canvas (the panel, the digits).
        Image Load(const std::string& name);

        // The background, one BG pixel per canvas unit, as a quad far ahead filling the view.
        void DrawBackground(const Flight& flight);

        // A quad from its top-left corner clockwise, textured from uv0 (top left) to uv1.
        void AddQuad(Vector3 topLeft, Vector3 topRight, Vector3 bottomRight, Vector3 bottomLeft, Vector2 uv0, Vector2 uv1);

        // The tube's segments far to near in one draw, each vertex with its RGBA.
        void DrawTube(const Flight& flight);

        // The planet and the lights: billboards facing the camera.
        void DrawSprites(const Flight& flight);

        // Adds the sprites of a kind; returns their vertex count.
        int AddSprites(const Flight& flight, StreamTube::SpawnKind kind, float scale);

        // A model in a pose: a mesh per (model, animation, frame), built once.
        void DrawModel(const std::string& name, const std::string& animation, int frame, Matrix4x4 transform);

        std::shared_ptr<Formats::Model> ModelOf(const std::string& name);

        const Parts& Pose(const std::string& model, const std::string& animation, int frame);

        std::optional<PoseMesh> Build(const Formats::Model& model, const Parts& pose);

        // The health box at the bottom right; the number blinks at 20 or less.
        void DrawHealth(int health, int blink);

        void DrawImage(const Image& image, RectangleF source, RectangleF target);

        // The fade over everything: red added (0x4352ac: R + red, green and blue kept), then
        // towards white, then darkened.
        void DrawFade(const Fade& fade);
    };
}
