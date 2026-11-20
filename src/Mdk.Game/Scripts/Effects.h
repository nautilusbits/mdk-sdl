#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Game/Scripts/Debris.h"
#include "Mdk.Game/Scripts/PairHash.h"
#include "Mdk.Game/Scripts/RayHit.h"

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    // Sprite effects of the arenas (a port of godot-mdk's effects.gd; the original's
    // pool of 96 effects, arena+0x5c): slime bleeding from wounds (attach_effect 128), slime drops
    // (spawn_debris 136), bubbles (spawn_effect 132) and the smoke trails of sparks and pieces.
    // Sprites are animated textures drawn as billboards width × scale / 256 units wide (0x407048);
    // frame F - 1 - (life × speed mod F), so they play forwards as their life runs out.
    //
    //   wound ──(bursts)──► drops ─► fall, bounce ─► gone
    //   bubble ─► rise, grow ─► pop (BUBB_POP) ─► gone
    //   trail ─► fall ─► gone (11 ticks)
    class Effects
    {
    public:
        enum class Kind { Wound, Drop, Bubble, Pop, Trail };

        // Most effects alive at once.
        static constexpr int MaxEffects = 96;
        // Sprites are scale / 256 units per texel.
        static constexpr float TexelsPerUnit = 256.0f;

    private:
        // Gravity of drops and trails, units per tick² (about 64 units/s²).
        static constexpr float Gravity = Debris::Gravity;
        static constexpr float Bounce = 1.4f;
        static constexpr int BounceTicks = 20;
        // A wound spits drops for this many ticks at a time, at half a unit per tick.
        static constexpr float BurstTicks = 30.0f;
        static constexpr float BurstSpeed = 0.5f;
        static constexpr float WoundDropScale = 4.0f;
        // A wound's chance of a burst (out of 32768), weaker by 128-255 every loop.
        static constexpr int FullIntensity = 0x7fff;
        static constexpr int IntensityLossMin = 0x80;
        static constexpr int IntensityLossMask = 0x7f;
        static constexpr int RandRange = 32768;
        static constexpr int RandHalf = 0x4000;
        static constexpr float JitterScale = 1.0f / 65536.0f;
        // Bubbles: scale 10 ± 3.3, 64 ticks, growing 4 a second, drifting ±1.6 u/s² sideways
        // and rising at 0.5 u/s².
        static constexpr float BubbleScale = 10.0f;
        static constexpr float BubbleScaleSpread = 0.0002f;
        static constexpr float BubbleLife = 64.0f;
        static constexpr float BubbleGrowth = 4.0f;
        static constexpr float BubbleDrift = 1e-4f;
        static constexpr float BubbleRise = 0.5f;
        // Trails: scale 4, one frame per tick for 11 ticks.
        static constexpr float TrailScale = 4.0f;
        static constexpr float TrailLife = 11.0f;
        static constexpr float TicksPerSecond = 30.0f;
        static constexpr float ContactBackoff = 0.01f;
        static constexpr std::string_view WoundTexture = "SL_BIG";
        static constexpr std::string_view BigDrop = "SL_MED";
        static constexpr std::string_view SmallDrop = "SL_SMA";
        static constexpr std::string_view TrailTexture = "TRAIL";
        static constexpr std::string_view BubbleTexture = "BUBB";
        static constexpr std::string_view PopTexture = "BUBB_POP";

    public:
        class Effect
        {
        public:
            Effects::Kind Kind = Effects::Kind::Wound;
            // The arena whose pool holds it, and its sprite's texture there.
            std::string Arena;
            std::string Texture;
            int Frames = 1;
            Vector3 Position;
            // Units per tick.
            Vector3 Velocity;
            float Scale = 1.0f;
            // Frames per tick.
            float Speed = 0.5f;
            float Life = 0.0f;
            // Wounds: the object, the point it bleeds at, the point the drops fly towards, the chance
            // of a burst (out of 32768) and the burst's time left and jitter.
            std::shared_ptr<Objects::MdkObject> Owner;
            int Slot = 0;
            int Towards = 0;
            int Intensity = FullIntensity;
            float Burst = 0.0f;
            Vector3 Jitter;
            bool Done = false;

            // A tick starts: frames until the next are drawn from here to the tick's.
            void Remember() { _ticked = Position; }

            // The place alpha of the way from the last tick's start to now
            // (a frame between ticks); a wound on an object that jumped is where it is.
            Vector3 PositionAt(float alpha) const;

            // The sprite's frame now.
            int Frame() const;

        private:
            std::optional<Vector3> _ticked;
        };

        explicit Effects(std::shared_ptr<Random> rng) : _rng(std::move(rng)) {}

        // The frame count of an arena's texture, 0 when it has none (arena, name).
        std::function<int(const std::string&, const std::string&)> FrameCount;
        // The arena's nearest hit along a segment: (arena, from, to).
        std::function<std::optional<RayHit>(const std::string&, Vector3, Vector3)> Ray;

        const std::vector<std::shared_ptr<Effect>>& All() const { return _effects; }

        // Starts the bleeding of a wound at the object's reference point slot
        // (0x4067b8): an SL_BIG blob looping at 15 fps that squirts drops towards towards.
        void Attach(Objects::MdkObject& obj, int slot, int towards);

        // Stops the bleeding at a reference point (attach_effect "OFF", 0x405250).
        void Detach(Objects::MdkObject& obj, int slot);

        // A slime drop (0x406b3c): SL_MED or SL_SMA, playing once over 4 seconds.
        void SpawnDrop(const std::string& arena, Vector3 point, Vector3 velocity, float scale);

        // A puff of the smoke trail of sparks and pieces (0x406070): TRAIL, one frame per tick
        // for 11 ticks, falling.
        void SpawnTrail(const std::string& arena, Vector3 point);

        // A bubble (0x406434): BUBB, rising, growing and wobbling, then popping.
        void SpawnBubble(const std::string& arena, Vector3 point);

        // Whether an arena has any effect.
        bool HasEffects(const std::string& arena) const;

        // Updates the effects by ticks.
        void Update(float ticks);

    private:
        std::shared_ptr<Random> _rng;
        std::vector<std::shared_ptr<Effect>> _effects;
        Dictionary<std::pair<Objects::MdkObject*, int>, std::shared_ptr<Effect>, PairHash> _wounds;

        std::shared_ptr<Effect> Create(const std::string& arena, std::string_view texture, Kind kind, Vector3 point, float scale,
            float speed);

        void Remove(Effect& effect);

        // A bubble drifts, rises and grows; it pops (BUBB_POP) when it hits something or its
        // life runs out.
        void UpdateBubble(Effect& effect, float ticks);

        // A wound (0x40690c): the blob loops forever, weaker every loop; now and then it squirts
        // a burst of drops (one per update for 30 ticks) towards the second point.
        void UpdateWound(Effect& effect, float ticks);

        int RandomHalf();

        // Moves an effect by its velocity (0x4061d8); drops and trails fall and bounce off the
        // arena, bubbles stop. Returns whether it hit something.
        bool Move(Effect& effect, float ticks);
    };
}
