#pragma once

#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"

namespace Mdk::Formats::Tests
{
    // Synthetic little-endian files for format tests.
    class Bytes
    {
    public:
        int Position() const { return _position; }

        template <typename... T>
        Bytes& U8(T... values)
        {
            (Write(static_cast<byte>(values)), ...);
            return *this;
        }

        template <typename... T>
        Bytes& S16(T... values)
        {
            (Write(static_cast<short>(values)), ...);
            return *this;
        }

        Bytes& U16(int value)
        {
            Write(static_cast<ushort>(value));
            return *this;
        }

        template <typename... T>
        Bytes& U32(T... values)
        {
            (Write(static_cast<uint>(static_cast<long long>(values))), ...);
            return *this;
        }

        template <typename... T>
        Bytes& F32(T... values)
        {
            (Write(static_cast<float>(values)), ...);
            return *this;
        }

        Bytes& Vec(Vector3 v) { return F32(v.X, v.Y, v.Z); }

        // A fixed-length, NUL-padded name.
        Bytes& Name(const std::string& text, int length)
        {
            std::vector<byte> bytes(length);
            std::memcpy(bytes.data(), text.data(), text.size());
            return Raw(bytes);
        }

        // A Pascal string: u8 length, the characters.
        Bytes& Pascal(const std::string& text) { return U8(static_cast<int>(text.size())).Raw(std::vector<byte>(text.begin(), text.end())); }

        Bytes& Raw(std::span<const byte> bytes)
        {
            for (auto b : bytes)
            {
                Write(b);
            }

            return *this;
        }

        // Writes a u32 at `at`, later.
        Bytes& Patch(int at, long long value)
        {
            auto position = _position;
            _position = at;
            Write(static_cast<uint>(value));
            _position = position;
            return *this;
        }

        std::vector<byte> ToArray() const { return _bytes; }

    private:
        std::vector<byte> _bytes;
        int _position = 0;

        template <typename T>
        void Write(T value)
        {
            if (_position + sizeof(T) > _bytes.size())
            {
                _bytes.resize(_position + sizeof(T));
            }

            std::memcpy(_bytes.data() + _position, &value, sizeof(T));
            _position += sizeof(T);
        }
    };
}
