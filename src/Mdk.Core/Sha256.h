#pragma once

// System.Security.Cryptography SHA-256 (HashData, IncrementalHash) and Convert.ToHexStringLower,
// Block function in Sha256.cpp (SHA-NI / ARMv8 SHA2 when the CPU has them).
//   Sha256 hash;  hash.AppendData(bytes);  auto hex = Convert::ToHexStringLower(hash.GetHashAndReset());

#include <array>
#include <istream>
#include <span>
#include <string>
#include <vector>

#include "Mdk.Core/DotNet.h"

namespace Mdk
{
    class Sha256
    {
    public:
        static constexpr size_t HashSize = 32;

        void AppendData(std::span<const byte> data);
        std::vector<byte> GetHashAndReset();

        // SHA256.HashData(stream): the rest of the stream.
        static std::vector<byte> HashData(std::istream& stream)
        {
            constexpr size_t Buffer = 1 << 16;
            std::vector<byte> buffer(Buffer);
            Sha256 hash;
            while (stream)
            {
                stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                auto read = static_cast<size_t>(stream.gcount());
                hash.AppendData(std::span<const byte>(buffer.data(), read));
            }

            return hash.GetHashAndReset();
        }

        static std::vector<byte> HashData(std::span<const byte> data)
        {
            Sha256 hash;
            hash.AppendData(data);
            return hash.GetHashAndReset();
        }

    private:
        static constexpr size_t BlockSize = 64;
        static constexpr std::array<uint, 8> Initial = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

        std::array<uint, 8> _state = Initial;
        std::array<byte, BlockSize> _block{};
        size_t _used = 0;
        ulong _length = 0;
    };

    struct Convert
    {
        static std::string ToHexStringLower(std::span<const byte> bytes)
        {
            static constexpr char Digits[] = "0123456789abcdef";
            std::string hex;
            hex.reserve(bytes.size() * 2);
            for (auto b : bytes)
            {
                hex += Digits[b >> 4];
                hex += Digits[b & 0xF];
            }

            return hex;
        }
    };
}
