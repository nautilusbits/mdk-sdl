#pragma once

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "core/DotNet.h"
#include "core/Drawing.h"
#include "core/Numerics.h"
#include "engine/render/Renderer.h"

namespace Mdk::Formats
{
    class Bni;
    class MdkData;
    class Model;
    class Palette;
    class Texture;
    class TextureArchive;
}

namespace Mdk::Game::Menu
{
    // The Score-O-matic's heads (0x433268; stats_screen.gd): the XGHEAD model of STATS.BNI
    // (textures of STATS.MTI), at most 16, spinning at 157°/s, seen from the origin along +Y with a
    // focal length of 250 pixels; one row below 5, else two rows of (count + 1) / 2 (8 at most).
    // Drawn under the page's text and fades.
    //   row 0 (z −3):     ☻   ☻   ☻   ☻        x = 20 (k + 1) / (m + 1) − 10, y 8
    //   row 1 (z −4.5):     ☻   ☻   ☻
    class HeadsView
    {
    public:
        static constexpr int MaxHeads = 16;

        // The model through the page's palette (`pageRgb` turns the BNI's palette into the page's:
        // the system colours and the model's).
        HeadsView(Engine::Render::Renderer& renderer, const Formats::Bni& bni,
            const std::function<std::vector<byte>(std::span<const byte>)>& pageRgb, const Formats::MdkData& data);

        // Each frame: the shown heads turn.
        void Update(int shown, float delta);

        // Draws `shown` of `count` heads in the page's view (`area`, canvas units).
        void Draw(int shown, int count, RectangleF area);

        // Where the shown heads are: one row below 5 heads, else two rows.
        static std::vector<Vector3> Places(int shown, int count);

    private:
        static constexpr const char* ModelName = "XGHEAD";
        static constexpr const char* PaletteName = "PAL";
        static constexpr const char* Textures = "MISC/STATS.MTI";
        static constexpr float Spin = 157.f;
        static constexpr float Focal = 250.f;
        static constexpr float Scale = 0.8f;
        static constexpr float Near = 0.5f;
        static constexpr float Far = 1000.f;
        static constexpr float Distance = 8.f;
        static constexpr float RowWidth = 20.f;
        static constexpr float FirstRowZ = -3.f;
        static constexpr float SecondRowZ = -4.5f;
        // Up to 4 heads make one row.
        static constexpr int OneRow = 5;
        static constexpr int MaxPerRow = 8;
        static constexpr int PaletteBytes = 768;

        struct Batch
        {
            int First = 0;
            int Count = 0;
            Engine::Render::Material Material;
        };

        Engine::Render::Renderer& _renderer;
        int _mesh = -1;
        std::vector<Batch> _batches;
        std::array<float, MaxHeads> _angles{};

        // The model's rest pose, both faces, in batches by surface (textures or palette colours).
        int Build(const Formats::Model& model, const Formats::Palette& palette, const Formats::TextureArchive& archive);

        // Keyed by the texture (C#: by reference).
        std::optional<std::tuple<Engine::Render::Material, std::shared_ptr<Formats::Texture>>> Surface(int value,
            const Formats::Model& model, const Formats::Palette& palette, const Formats::TextureArchive& archive, int paletteId,
            std::unordered_map<const Formats::Texture*, int>& textures);
    };
}
