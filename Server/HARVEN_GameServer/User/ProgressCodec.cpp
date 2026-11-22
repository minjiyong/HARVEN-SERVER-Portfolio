// 순수 논리 — pch.h 없이도 빌드된다(시험 AchieveRuleTests가 이 파일을 함께 컴파일한다).
#include "User/ProgressCodec.h"

#include <algorithm>

namespace
{
    void Put16(std::vector<uint8_t>& out, int v)
    {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    void Put32(std::vector<uint8_t>& out, uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
    int Get16(const std::vector<uint8_t>& in, size_t at) { return static_cast<int16_t>(in[at] | (in[at + 1] << 8)); }
    uint16_t GetU16(const std::vector<uint8_t>& in, size_t at) { return static_cast<uint16_t>(in[at] | (in[at + 1] << 8)); }
    uint32_t GetU32(const std::vector<uint8_t>& in, size_t at)
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<uint32_t>(in[at + static_cast<size_t>(i)]) << (i * 8);
        return v;
    }
}

namespace ProgressCodec
{
    std::vector<uint8_t> Encode(const Data& d, const AchieveBook& book, const AchieveRule::Catalog* cat)
    {
        std::vector<uint8_t> out;
        out.reserve(512);
        out.push_back('P');
        out.push_back('2');
        // 이 서버보다 새 판에서 읽어 얼린 업적 절은 그 판 그대로 되쓴다(새 서버가 다시 읽을 수 있게)
        out.push_back(book.Frozen() && book.FrozenVersion() > kVersion ? book.FrozenVersion() : kVersion);
        out.push_back(d.classId);
        for (int i = 0; i < 6; ++i)
            Put16(out, d.alloc[static_cast<size_t>(i)]);
        Put16(out, d.pityRare);
        Put16(out, d.pityHeroic);
        // v3: 퀘스트
        uint8_t active = 0;
        for (const Quest& q : d.quests)
            if (q.id != 0 && active < 255) ++active;
        out.push_back(active);
        uint8_t written = 0;
        for (const Quest& q : d.quests)
        {
            if (q.id == 0 || written >= active) continue;
            Put16(out, q.id);
            for (const uint16_t p : q.progress) Put16(out, p);
            ++written;
        }
        out.insert(out.end(), d.questDone.begin(), d.questDone.end());
        const uint8_t cds = static_cast<uint8_t>(std::min(d.cooldowns.size(), kMaxCooldowns));
        out.push_back(cds);
        for (size_t i = 0; i < cds; ++i)
        {
            Put16(out, d.cooldowns[i].id);
            Put32(out, d.cooldowns[i].readyAtUnix);
        }
        // v4: 개인 업적
        book.Write(out, cat);
        return out;
    }

    bool Decode(const std::vector<uint8_t>& in, Data& d, AchieveBook& book)
    {
        book.Clear();
        d = Data{};
        if (in.size() < 4 + 12 || in[0] != 'P' || in[1] != '2' || in[2] < 1)
            return false;
        d.version = in[2];
        d.classId = in[3];
        for (int i = 0; i < 6; ++i)
            d.alloc[static_cast<size_t>(i)] = static_cast<int16_t>(std::clamp(Get16(in, 4 + static_cast<size_t>(i) * 2), 0, 999));
        if (in[2] >= 2 && in.size() >= 4 + 12 + 4)
        {
            d.pityRare   = static_cast<uint16_t>(std::clamp(Get16(in, 16), 0, 1000));
            d.pityHeroic = static_cast<uint16_t>(std::clamp(Get16(in, 18), 0, 1000));
        }
        // v3: 퀘스트 — 잘린 기록은 읽은 데까지(표로 거르기 · 진행도 자르기는 CUser)
        size_t at = 20;
        bool whole = false;   // v3 절을 끝까지 읽었다(업적 절이 그 뒤에 있다)
        if (in[2] >= 3 && in.size() >= at + 1)
        {
            const uint8_t active = in[at++];
            uint8_t i = 0;
            for (; i < active && at + 8 <= in.size(); ++i, at += 8)
            {
                Quest q;
                q.id = GetU16(in, at);
                for (size_t o = 0; o < kMaxObjs; ++o)
                    q.progress[o] = GetU16(in, at + 2 + o * 2);
                d.quests.push_back(q);
            }
            if (at + d.questDone.size() <= in.size())   // 예전 판독과 같게 — 퀘스트 줄이 잘렸어도 그 자리에서 읽는다(깨진 기록)
            {
                std::copy(in.begin() + static_cast<std::ptrdiff_t>(at), in.begin() + static_cast<std::ptrdiff_t>(at + d.questDone.size()), d.questDone.begin());
                at += d.questDone.size();
                if (at + 1 <= in.size())
                {
                    const uint8_t cds = in[at++];
                    uint8_t c = 0;
                    for (; c < cds && at + 6 <= in.size(); ++c, at += 6)
                        d.cooldowns.push_back(Cooldown{ GetU16(in, at), GetU32(in, at + 2) });
                    whole = i == active && c == cds;
                }
            }
        }
        // v4: 개인 업적. 깨졌으면 비우지 않고 얼린다(원본 절 바이트를 그대로 되쓴다 — 받음 비트를 잃어 다시 받는 길을 막는다):
        //   · 모르는 kind의 통계 줄은 건너뛰고 보관(Read) · 잘림 · 비트 바이트 수 > 128 → Read가 얼린다
        //   · 절 뒤에 바이트가 남았다 · 이 서버보다 새 판 → 여기서 얼린다(새 판은 그 판 번호로 되쓴다 — Encode)
        //   (판 4인데 퀘스트 절부터 잘렸으면 업적 절이 아예 없다 — 지킬 바이트가 없어 빈 채)
        if (in[2] >= 4 && whole)
        {
            const size_t start = at;
            if (in[2] > kVersion)
                book.Freeze(in.data() + start, in.size() - start, AchieveBook::FreezeReason::NewerVersion, in[2]);
            else if (book.Read(in.data(), in.size(), at) && at != in.size())
                book.Freeze(in.data() + start, in.size() - start, AchieveBook::FreezeReason::Trailing);
        }
        return true;
    }
}
