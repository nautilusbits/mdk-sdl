#pragma once

#include <string>
#include <vector>

#include "Mdk.Formats/Bni.h"

namespace Mdk::Formats
{
    // The fall's own entries of FALL3D.BNI (godot-mdk docs/formats.md, "Fall files").
    //   ZOOMnnnn  u32 size, then 180 records (one per pair of screen rows):
    //             u32 nL, u8 left[4 nL], u32 nM, u32 nR, u8 right[4 nR]    (nL + nM + nR = 150)
    //   FALLPU_n  char[12] pickup model names, ended by a name starting with 0
    struct Fall3d
    {
        static constexpr int HazeWidth = 600;
        // One record covers two screen rows of the 360-high view.
        static constexpr int HazeRows = 180;

        // A haze frame as one byte per pixel (0 plain, 1-8 the blend table offset), 600 x 180.
        static std::vector<byte> Haze(const Bni& bni, const std::string& name);

        // The pickups dropped in the fall of FALLPU_n (none if there is no entry).
        static std::vector<std::string> Pickups(const Bni& bni, const std::string& name);

    private:
        // Pixels per group of the records.
        static constexpr int HazeGroup = 4;
        static constexpr int PickupNameLength = 12;
    };
}
