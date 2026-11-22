#pragma once
// SHA-256 (FIPS 180-4) — GameData manifest의 판 해시를 서버가 스스로 확인하는 데 쓴다(datatool export와 같은 값).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace GameDataLoad
{
    class Sha256
    {
    public:
        Sha256() { Reset(); }

        void Reset()
        {
            static constexpr uint32_t kInit[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
            std::memcpy(m_h, kInit, sizeof(m_h));
            m_len = 0;
            m_fill = 0;
        }

        void Update(std::string_view data)
        {
            for (const char c : data)
            {
                m_buf[m_fill++] = static_cast<uint8_t>(c);
                if (m_fill == 64)
                {
                    Block(m_buf);
                    m_fill = 0;
                }
            }
            m_len += data.size();
        }

        std::string HexDigest()
        {
            const uint64_t bits = m_len * 8u;
            const uint8_t one = 0x80;
            Update(std::string_view(reinterpret_cast<const char*>(&one), 1));
            m_len -= 1;   // 덧붙인 바이트는 길이에 넣지 않는다
            const uint8_t zero = 0;
            while (m_fill != 56)
            {
                Update(std::string_view(reinterpret_cast<const char*>(&zero), 1));
                m_len -= 1;
            }
            uint8_t lenBytes[8];
            for (int i = 0; i < 8; ++i)
                lenBytes[7 - i] = static_cast<uint8_t>(bits >> (8 * i));
            Update(std::string_view(reinterpret_cast<const char*>(lenBytes), 8));
            static const char* kHex = "0123456789abcdef";
            std::string out;
            out.reserve(64);
            for (const uint32_t h : m_h)
                for (int i = 3; i >= 0; --i)
                {
                    const uint8_t b = static_cast<uint8_t>(h >> (8 * i));
                    out.push_back(kHex[b >> 4]);
                    out.push_back(kHex[b & 15]);
                }
            Reset();
            return out;
        }

        static std::string Of(std::string_view data)
        {
            Sha256 s;
            s.Update(data);
            return s.HexDigest();
        }

    private:
        static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

        void Block(const uint8_t* p)
        {
            static constexpr uint32_t k[64] = {
                0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
                0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
                0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
                0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
                0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
                0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
                0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
                0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u };
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) | (static_cast<uint32_t>(p[i * 4 + 1]) << 16) | (static_cast<uint32_t>(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
            for (int i = 16; i < 64; ++i)
            {
                const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
            for (int i = 0; i < 64; ++i)
            {
                const uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
                const uint32_t ch = (e & f) ^ (~e & g);
                const uint32_t t1 = h + S1 + ch + k[i] + w[i];
                const uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
                const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
                const uint32_t t2 = S0 + mj;
                h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }
            m_h[0] += a; m_h[1] += b; m_h[2] += c; m_h[3] += d; m_h[4] += e; m_h[5] += f; m_h[6] += g; m_h[7] += h;
        }

        uint32_t m_h[8];
        uint8_t m_buf[64];
        uint64_t m_len;
        size_t m_fill;
    };
}
