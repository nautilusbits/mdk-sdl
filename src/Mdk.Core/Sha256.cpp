#include "Mdk.Core/Sha256.h"

#include <algorithm>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64)
#include <cpuid.h>
#include <immintrin.h>
#define MDK_SHA_X86 1
#elif defined(__aarch64__) && defined(__ARM_FEATURE_SHA2)
#include <arm_neon.h>
#define MDK_SHA_ARM 1
#endif

namespace Mdk
{
    namespace
    {
        constexpr uint K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

        constexpr uint Rotr(uint x, int n) { return (x >> n) | (x << (32 - n)); }

        uint LoadBig(const byte* p)
        {
            return (uint(p[0]) << 24) | (uint(p[1]) << 16) | (uint(p[2]) << 8) | uint(p[3]);
        }

        // FIPS 180-4 compression, one 64-byte block at a time.
        void BlocksPortable(uint* state, const byte* data, size_t blocks)
        {
            for (; blocks > 0; blocks--, data += 64)
            {
                uint w[64];
                for (int i = 0; i < 16; i++)
                {
                    w[i] = LoadBig(data + i * 4);
                }

                for (int i = 16; i < 64; i++)
                {
                    uint s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                    uint s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                }

                uint a = state[0], b = state[1], c = state[2], d = state[3];
                uint e = state[4], f = state[5], g = state[6], h = state[7];
                for (int i = 0; i < 64; i++)
                {
                    uint t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
                    uint t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                    h = g;
                    g = f;
                    f = e;
                    e = d + t1;
                    d = c;
                    c = b;
                    b = a;
                    a = t1 + t2;
                }

                state[0] += a;
                state[1] += b;
                state[2] += c;
                state[3] += d;
                state[4] += e;
                state[5] += f;
                state[6] += g;
                state[7] += h;
            }
        }

#if MDK_SHA_X86
        // Intel SHA extensions (public-domain pattern of Intel's white paper).
        __attribute__((target("sha,sse4.1"))) void BlocksShaNi(uint* state, const byte* data, size_t blocks)
        {
            const __m128i mask = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);
            __m128i tmp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state[0]));
            __m128i s1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state[4]));
            tmp = _mm_shuffle_epi32(tmp, 0xB1);
            s1 = _mm_shuffle_epi32(s1, 0x1B);
            __m128i s0 = _mm_alignr_epi8(tmp, s1, 8);
            s1 = _mm_blend_epi16(s1, tmp, 0xF0);

            for (; blocks > 0; blocks--, data += 64)
            {
                const __m128i save0 = s0;
                const __m128i save1 = s1;
                __m128i m[4];
                for (int i = 0; i < 4; i++)
                {
                    m[i] = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i * 16)), mask);
                }

                for (int r = 0; r < 16; r++)
                {
                    __m128i& cur = m[r & 3];
                    __m128i msg = _mm_add_epi32(cur, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&K[r * 4])));
                    s1 = _mm_sha256rnds2_epu32(s1, s0, msg);
                    msg = _mm_shuffle_epi32(msg, 0x0E);
                    s0 = _mm_sha256rnds2_epu32(s0, s1, msg);
                    if (r < 12)
                    {
                        // Next schedule words: m[r+4] from m[r..r+3].
                        __m128i& next = m[r & 3];
                        const __m128i& m1 = m[(r + 1) & 3];
                        const __m128i& m2 = m[(r + 2) & 3];
                        const __m128i& m3 = m[(r + 3) & 3];
                        __m128i t = _mm_sha256msg1_epu32(next, m1);
                        t = _mm_add_epi32(t, _mm_alignr_epi8(m3, m2, 4));
                        next = _mm_sha256msg2_epu32(t, m3);
                    }
                }

                s0 = _mm_add_epi32(s0, save0);
                s1 = _mm_add_epi32(s1, save1);
            }

            tmp = _mm_shuffle_epi32(s0, 0x1B);
            s1 = _mm_shuffle_epi32(s1, 0xB1);
            s0 = _mm_blend_epi16(tmp, s1, 0xF0);
            s1 = _mm_alignr_epi8(s1, tmp, 8);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(&state[0]), s0);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(&state[4]), s1);
        }

        bool HasShaNi()
        {
            unsigned a, b, c, d;
            if (!__get_cpuid_count(7, 0, &a, &b, &c, &d))
            {
                return false;
            }

            const bool sha = (b >> 29) & 1;
            __get_cpuid(1, &a, &b, &c, &d);
            const bool sse41 = (c >> 19) & 1;
            const bool ssse3 = (c >> 9) & 1;
            return sha && sse41 && ssse3;
        }
#endif

#if MDK_SHA_ARM
        // ARMv8 SHA2 instructions (all Apple Silicon; arm64 builds with +sha2).
        void BlocksArm(uint* state, const byte* data, size_t blocks)
        {
            uint32x4_t s0 = vld1q_u32(&state[0]);
            uint32x4_t s1 = vld1q_u32(&state[4]);
            for (; blocks > 0; blocks--, data += 64)
            {
                const uint32x4_t save0 = s0;
                const uint32x4_t save1 = s1;
                uint32x4_t m[4];
                for (int i = 0; i < 4; i++)
                {
                    m[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + i * 16)));
                }

                for (int r = 0; r < 16; r++)
                {
                    uint32x4_t& cur = m[r & 3];
                    const uint32x4_t wk = vaddq_u32(cur, vld1q_u32(&K[r * 4]));
                    if (r < 12)
                    {
                        cur = vsha256su1q_u32(vsha256su0q_u32(cur, m[(r + 1) & 3]), m[(r + 2) & 3], m[(r + 3) & 3]);
                    }

                    const uint32x4_t t = s0;
                    s0 = vsha256hq_u32(s0, s1, wk);
                    s1 = vsha256h2q_u32(s1, t, wk);
                }

                s0 = vaddq_u32(s0, save0);
                s1 = vaddq_u32(s1, save1);
            }

            vst1q_u32(&state[0], s0);
            vst1q_u32(&state[4], s1);
        }
#endif

        void Blocks(uint* state, const byte* data, size_t blocks)
        {
#if MDK_SHA_X86
            static const bool shaNi = HasShaNi();
            if (shaNi)
            {
                BlocksShaNi(state, data, blocks);
                return;
            }
#elif MDK_SHA_ARM
            BlocksArm(state, data, blocks);
            return;
#endif
            BlocksPortable(state, data, blocks);
        }
    }

    void Sha256::AppendData(std::span<const byte> data)
    {
        _length += data.size();
        const byte* p = data.data();
        size_t n = data.size();

        // Fill a partial block first.
        if (_used > 0)
        {
            const size_t take = std::min(n, BlockSize - _used);
            std::memcpy(_block.data() + _used, p, take);
            _used += take;
            p += take;
            n -= take;
            if (_used < BlockSize)
            {
                return;
            }

            Blocks(_state.data(), _block.data(), 1);
            _used = 0;
        }

        // Whole blocks straight from the input.
        const size_t whole = n / BlockSize;
        if (whole > 0)
        {
            Blocks(_state.data(), p, whole);
            p += whole * BlockSize;
            n -= whole * BlockSize;
        }

        if (n > 0)
        {
            std::memcpy(_block.data(), p, n);
            _used = n;
        }
    }

    std::vector<byte> Sha256::GetHashAndReset()
    {
        // Padding: 0x80, zeros, then the bit length big-endian in the last 8 bytes.
        const ulong bits = _length * 8;
        _block[_used++] = 0x80;
        if (_used > BlockSize - 8)
        {
            std::memset(_block.data() + _used, 0, BlockSize - _used);
            Blocks(_state.data(), _block.data(), 1);
            _used = 0;
        }

        std::memset(_block.data() + _used, 0, BlockSize - 8 - _used);
        for (int i = 0; i < 8; i++)
        {
            _block[BlockSize - 1 - i] = static_cast<byte>(bits >> (i * 8));
        }

        Blocks(_state.data(), _block.data(), 1);

        std::vector<byte> hash(HashSize);
        for (size_t i = 0; i < 8; i++)
        {
            hash[i * 4] = static_cast<byte>(_state[i] >> 24);
            hash[i * 4 + 1] = static_cast<byte>(_state[i] >> 16);
            hash[i * 4 + 2] = static_cast<byte>(_state[i] >> 8);
            hash[i * 4 + 3] = static_cast<byte>(_state[i]);
        }

        _state = Initial;
        _used = 0;
        _length = 0;
        return hash;
    }
}
