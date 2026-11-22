#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

#include "Database/GameData/WorldData.h"
#include "Database/GameData/MonsterTable.h"
#include "Database/GameData/CombatData.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/Item/ItemTableData.h"

// ─────────────────────────────────────────────────────────────────────────────
// 퀘스트 · 메인 이야기 — 서버·클라 공용.
//
// 이야기의 뼈대: "새벽마다 뛰던 심장나무 하르벤이 멈췄다." (세계관 · 이름 규칙: HARVEN-DESIGN/WORLD.md)
//   제1장 늦어지는 새벽(데니차) → 제2장 속삭이는 숲(올벤 숲) → 제3장 세 번의 종소리(캄파나 폐허)
//   → 제4장 깨어진 서약(서약의 언덕 · 올하르 사원) → 제5장 심장의 불씨(칼둔 균열 → 하르벤)
//
// 서버는 받기/완료 조건 · 진행(처치 · 수집 · 대화 · 탐험 · 보스 · 장비)을 판정하고 보상을
// 준다(서버 권위). 클라는 같은 표로 대사 · 목표 · 보상 · 표식(!/?) · 나침반을 그린다.
// 진행 상태는 User의 진행 데이터(PlayerProgress v3)에 저장된다.
//
// 규칙: 퀘스트 id는 한 번 배정하면 바꾸거나 재사용하지 않는다(완료 기록이 id 비트로
// 저장된다). 1~255.
//
// 수집(Collect) 물건은 가방에 들어가지 않는다 — 진행도로만 센다(가방 칸을 뺏지 않고,
// 버리거나 팔아서 퀘스트가 꼬이는 일이 없다). 운이 나빠도 끝없이 헤매지 않도록
// 실패할 때마다 확률이 조금씩 오른다(서버).
// ─────────────────────────────────────────────────────────────────────────────
namespace QuestData
{
    inline constexpr int     kMaxActive   = 8;       // 동시에 진행할 수 있는 퀘스트
    inline constexpr int     kMaxObjs     = 3;
    inline constexpr uint8_t kAnyZone     = 0xFF;
    inline constexpr int     kBossShareRange = 16;   // 보스가 쓰러질 때 이 거리(칸) 안에 있으면 함께 인정
    inline constexpr uint16_t kFinalQuest = 16;      // 끝나면 엔딩 · 전 서버 공지

    enum class QuestKind : uint8_t { Main, Side, Daily, Class };

    enum class ObjKind : uint8_t
    {
        None,
        Kill,      // target=종족, count          (zone이 있으면 그 지역에서만)
        KillIn,    // zone의 아무 몬스터 count마리
        Collect,   // target=떨어뜨리는 종족, count개, chance%(한 마리당) — 가방 밖 퀘스트 물건
        Talk,      // target=NPC — 말을 건다(동물은 쓰다듬기)
        Explore,   // target=장소(Poi id) — 반경 안에 들어간다
        Boss,      // target=보스 종족 — 쓰러질 때 가까이(kBossShareRange) 있으면 함께 인정
        Equip,     // count=장비를 입은 부위 수(지금 상태)
    };

    // 수집 물건 아이콘(클라가 그린다)
    enum class QuestIcon : uint8_t { None, Moss, Straw, Meat, SpiritLight, Sap, Crest, Ember, CarvedStone, Count };

    struct Objective
    {
        ObjKind     kind   = ObjKind::None;
        uint16_t    target = 0;
        uint16_t    count  = 0;
        uint8_t     zone   = kAnyZone;
        uint8_t     chance = 100;
        const char* label  = nullptr;     // 표시 이름(없으면 종류로 만든다) — 수집물 이름
        QuestIcon   icon   = QuestIcon::None;
    };

    struct Reward
    {
        uint32_t exp     = 0;
        uint8_t  expPct  = 0;         // > 0이면 "지금 레벨 필요 경험치의 N%"(반복 퀘스트)
        uint32_t adena   = 0;         // 그로시(내부 이름 Adena 유지)
        uint16_t item[3] = {};        // 직업별(서약기사 · 각인술사 · 밤사냥꾼). 0 = 없음
        uint16_t count   = 1;
        Rarity   rarity  = Rarity::Common;   // 장비: 적어도 이 희귀도(가끔 한두 단계 위로 — 서버 굴림)
    };

    struct QuestRow
    {
        uint16_t    id;
        QuestKind   kind;
        uint8_t     chapter;      // 메인 이야기 장(1~5), 그 밖은 0
        const char* name;
        uint16_t    giver;        // 퀘스트를 주는 NPC
        uint16_t    turnIn;       // 보고할 NPC(0 = 목표를 이루는 순간 완료)
        uint8_t     minLevel;
        uint16_t    prereq;       // 먼저 끝내야 하는 퀘스트(0 = 없음)
        uint8_t     classMask;    // 0 = 모든 직업(ItemTableData::kMask*)
        const char* summary;      // 일지 한 줄
        const char* offer;        // 받기 전 대사(주는 NPC)
        const char* during;       // 진행 중 대사(보고할 NPC · 주는 NPC)
        const char* done;         // 완료 대사(보고할 NPC)
        Objective   objs[kMaxObjs];
        Reward      reward;
        uint16_t    cooldownMin = 0;   // 반복 퀘스트: 다시 받기까지(분). 0 = 한 번뿐
    };

    // ── 장소(탐험 목표 · 지도 표식) ───────────────────────────────────────────
    struct Poi
    {
        uint16_t    id;
        const char* name;
        int16_t     x, y;
        uint8_t     radius;
        const char* flavor;       // 처음 들어섰을 때 이야기(이야기책 한 줄)
    };

    // ── 장(章) — 제목 카드 ─────────────────────────────────────────────────────
    struct Chapter
    {
        const char* title;
        const char* place;
        const char* line;         // 제목 아래 한 줄
    };
    inline constexpr int kChapterCount = 5;   // 메인 이야기 장 수(구조 상수 — Chapter 탭은 0번 빈 장 + 1~5 = 정확히 6행, 아니면 로드 실패)

    // ── 표(탭 Quest · QuestObjective · Poi · Chapter) — 파사드 ───────────────────────────
    // 값은 GameData/*.json(시트 HARVEN_DATA)에서 온다(Data/Load/Bind_Quest.cpp). 대사(offer · during · done)는 Quest 탭의 열,
    // 목표 objs[kMaxObjs]는 자식 탭 QuestObjective(quest_id, seq 1~3 — 넘으면 로드 실패, 비운 칸은 기본값 None).
    // 규칙(예전 static_assert → 부팅 검증 R037~R062): 장소 · 퀘스트 id · NPC · 목표 · 보상 · 선행 퀘스트 · 메인 이야기 순서 ·
    // 마지막 퀘스트(kFinalQuest). 포인터 · 스팬은 스냅샷이 살아 있는 동안 유효하다.

    // 모든 퀘스트(Quest.json 순서 = id 순)
    [[nodiscard]] std::span<const QuestRow> Quests() noexcept;
    [[nodiscard]] size_t QuestCount() noexcept;

    // 모든 장소(Poi.json 순서 = id 순)
    [[nodiscard]] std::span<const Poi> Pois() noexcept;
    [[nodiscard]] size_t PoiCount() noexcept;

    // 장 제목 카드 — 행 i = 제 i장(0번은 빈 장), 정확히 kChapterCount + 1개
    [[nodiscard]] std::span<const Chapter> Chapters() noexcept;

    // ── 조회 ────────────────────────────────────────────────────────────────
    // id → 퀘스트(없으면 nullptr). id 색인으로 O(1).
    [[nodiscard]] const QuestRow* Find(uint16_t id) noexcept;

    // id → 장소(없으면 nullptr)
    [[nodiscard]] const Poi* FindPoi(uint16_t id) noexcept;

    [[nodiscard]] constexpr int ObjCount(const QuestRow& q) noexcept
    {
        int n = 0;
        for (const auto& o : q.objs)
            if (o.kind != ObjKind::None) ++n;
        return n;
    }

    [[nodiscard]] constexpr bool IsRepeatable(const QuestRow& q) noexcept { return q.cooldownMin > 0; }

    [[nodiscard]] constexpr bool ClassCanTake(const QuestRow& q, ClassId cls) noexcept
    {
        return q.classMask == 0 || (q.classMask & (1u << static_cast<unsigned>(cls))) != 0;
    }

    // 모든 목표를 이뤘나(목표가 없으면 — 전하기 퀘스트 — 받자마자 보고할 수 있다)
    [[nodiscard]] constexpr bool ObjectivesDone(const QuestRow& q, const uint16_t* progress) noexcept
    {
        for (int i = 0; i < kMaxObjs; ++i)
            if (q.objs[i].kind != ObjKind::None && progress[i] < q.objs[i].count)
                return false;
        return true;
    }

    // 직업에 맞는 보상 아이템(없으면 0)
    [[nodiscard]] constexpr uint16_t RewardItem(const QuestRow& q, ClassId cls) noexcept
    {
        const int c = static_cast<int>(cls);
        return (c >= 0 && c < 3) ? q.reward.item[c] : q.reward.item[0];
    }

    // 받을 경험치(반복 퀘스트는 지금 레벨에 맞춰). LevelTable(레벨 묶음)이 표를 읽게 되므로 constexpr가 아니다.
    [[nodiscard]] inline uint32_t RewardExp(const QuestRow& q, uint8_t level) noexcept
    {
        if (q.reward.expPct > 0)
            return LevelTable::ExpToNext(level) * q.reward.expPct / 100u;
        return q.reward.exp;
    }

    [[nodiscard]] constexpr bool InPoi(const Poi& p, int x, int y) noexcept
    {
        const int dx = x - p.x, dy = y - p.y;
        return (dx < 0 ? -dx : dx) <= p.radius && (dy < 0 ? -dy : dy) <= p.radius;
    }

    // 완료 기록(1~255 비트)
    [[nodiscard]] constexpr bool TestBit(const uint8_t* bits, uint16_t id) noexcept
    {
        return id > 0 && id < 256 && (bits[id >> 3] & (1u << (id & 7))) != 0;
    }

    // 메인 이야기에서 지금 "다음" 퀘스트(이미 받았거나 끝난 것은 건너뛴다). 없으면 nullptr.
    template <typename IsDoneFn>
    [[nodiscard]] const QuestRow* NextMain(IsDoneFn&& isDone) noexcept
    {
        for (const auto& q : Quests())
            if (q.kind == QuestKind::Main && !isDone(q.id))
                return &q;
        return nullptr;
    }

    // 예전 static_assert(Detail::Validate())의 규칙(R037~R062)은 부팅 검증으로 옮겼다 —
    // GameDataLoad::ValidateQuest(Data/Load/Bind_Quest.cpp, R063 — 행 위치를 남긴다).
}
