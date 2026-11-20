#pragma once

// System.Security.Cryptography SHA-256 (HashData, IncrementalHash) and Convert.ToHexStringLower,
// over picosha2.
//   Sha256 hash;  hash.AppendData(bytes);  auto hex = Convert::ToHexStringLower(hash.GetHashAndReset());

#include <array>
#include <istream>
#include <span>
#include <string>
#include <vector>

#include <picosha2.h>

#include "Mdk.Core/DotNet.h"

namespace Mdk
{
    class Sha256
    {
    public:
        static constexpr size_t HashSize = picosha2::k_digest_size;

        void AppendData(std::span<const byte> data) { _hasher.process(data.begin(), data.end()); }

        std::vector<byte> GetHashAndReset()
        {
            _hasher.finish();
            std::vector<byte> hash(HashSize);
            _hasher.get_hash_bytes(hash.begin(), hash.end());
            _hasher.init();
            return hash;
        }

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
        picosha2::hash256_one_by_one _hasher;
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
