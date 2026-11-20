#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/BinReader.h"
#include "Mdk.Formats/Model.h"

namespace Mdk::Formats
{
    // A vertex animation of a model's parts.
    class ModelAnimation
    {
    public:
        std::string Name;
        // 1.0 means 30 frames per second.
        float Speed = 1.0f;
        int FrameCount = 0;
        // Per frame, the object's movement in model space.
        std::vector<Vector3> RootMotion;
        // Per reference point, its place in each frame.
        std::vector<std::vector<Vector3>> ReferencePoints;

        static std::shared_ptr<ModelAnimation> Parse(const std::string& name, SharedBytes bytes, int offset);

        // An animation of the 1996 demo (BetaDemo), kept in LEVELn.CMI:
        //   +0   u32 track count T
        //   +4   u32 frame count F
        //   +8   u32 track offset[T]         relative to the animation
        //        f32 root motion[F][3]
        //        f32 bounds[F][6]            the model's box in each frame
        //        u32 reference point count R, f32[R][F][3]
        //   track: char[12] part name, u32 vertex count, f32 scale, f32 base[n][3],
        //          then for each of the other F - 1 frames s8 delta[n][3]
        // No speed, no frame numbers in the deltas, no matrix tracks.
        static std::shared_ptr<ModelAnimation> ParseBeta(const std::string& name, SharedBytes bytes, int offset);

        // Whether this animation moves `model`: one of its tracks names a part
        // of the model (a level's load bakes these pairs).
        bool Animates(const Model& model) const;

        // Per frame, every part's vertices. Parts without a track keep their model vertices.
        std::vector<std::vector<std::vector<Vector3>>> Bake(const Model& model) const;

    private:
        static constexpr uint SignMask = 0x7FFFFFFF;
        static constexpr int MatrixValues = 12;
        static constexpr int TrackNameLength = 12;
        // Each frame's box (6 floats) in the 1996 demo's animations.
        static constexpr int BoundsSize = 24;

        // The retail layout, or the 1996 demo's (ParseBeta).
        enum class Layout { Retail, Beta };

        SharedBytes _bytes = Share({});
        Layout _layout = Layout::Retail;
        // Lowercase track name to its offset.
        std::unordered_map<std::string, int> _tracks;

        std::vector<std::vector<Vector3>> DecodeTrack(int offset, int vertexCount) const;

        // Base vertices, then records s16 frame, s8 delta[n][3] (ending with frame -1)
        // adding delta * scale to the previous frame.
        std::vector<std::vector<Vector3>> DecodeDeltaTrack(int offset, int vertexCount, float scale) const;

        // The 1996 demo's tracks: base vertices, then s8 delta[n][3] for every next frame.
        std::vector<std::vector<Vector3>> DecodeBetaTrack(int offset, int vertexCount, float scale) const;

        // Rigid parts: shifts, base vertices, then a 3x4 s16 matrix per frame.
        std::vector<std::vector<Vector3>> DecodeMatrixTrack(int offset, int vertexCount) const;
    };
}
