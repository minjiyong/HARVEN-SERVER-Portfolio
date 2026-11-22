#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 마을 NPC 대사 — 서버·클라 공용.
//
//  · talk  : 대화창의 "이야기" 버튼을 누를 때마다 차례로 보여 주는 이야기(클라)
//  · barks : 혼잣말/말풍선. 서버가 번호를 골라 NPC_BARK_NFY로 알린다 — 같은 마을에
//            있는 모두가 같은 NPC가 같은 말을 하는 것을 본다(살아 있는 마을).
//
// 톤: 어두운 동화. 이야기의 뼈대는 "새벽마다 뛰던 심장나무 하르벤이 멈췄다" —
// 데니차 → 올벤 숲 → 캄파나 폐허 → 서약의 언덕 → 칼둔 균열 → 하르벤.
// 말투는 HARVEN-DESIGN/WORLD.md 5장(인물별 말투 표)을 따른다.
// ─────────────────────────────────────────────────────────────────────────────
namespace NpcTalk
{
    inline constexpr int kMaxTalk = 5;
    inline constexpr int kMaxBarks = 4;

    struct Lines
    {
        uint16_t    npcId;
        const char* talk[kMaxTalk];
        const char* barks[kMaxBarks];
    };

    // ── 표(탭 NpcLine) — 파사드. 값은 GameData/NpcLine.json(시트 HARVEN_DATA의 NpcLine 탭)에서 온다(Data/Load/Bind_Npc.cpp).
    // 한 줄 = (npc_id, kind = talk · bark, seq 1부터, text). talk ≤ kMaxTalk · bark ≤ kMaxBarks는 구조 상수 — 넘으면 로드 실패.

    // NPC의 대사(없으면 nullptr). 포인터는 스냅샷이 살아 있는 동안 유효하다.
    [[nodiscard]] const Lines* Find(uint16_t npcId) noexcept;

    [[nodiscard]] inline int TalkCount(uint16_t npcId) noexcept
    {
        const Lines* l = Find(npcId);
        int n = 0;
        if (l) for (const char* t : l->talk) if (t != nullptr) ++n;
        return n;
    }

    [[nodiscard]] inline int BarkCount(uint16_t npcId) noexcept
    {
        const Lines* l = Find(npcId);
        int n = 0;
        if (l) for (const char* t : l->barks) if (t != nullptr) ++n;
        return n;
    }
}
