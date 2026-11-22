#pragma once

#include "Database/GameData/Item/ItemTableRow.h"

struct MonsterTableRow;

// ─────────────────────────────────────────────────────────────────────────────
// 처치 전리품 — 굴림(그 사람 것: 직업 · 아이템 찾기 · 천장)과 지급(가방 · 넘치면 발밑). 몬스터를 가진 Zone 스레드에서
// (CRewardHelper::HandleKill → 여기). 누가 무엇을 받는지(필드 · 정예 · 보스 · 정수)는 CRewardHelper가 정한다.
// 굴림 규칙은 순수 함수 LootRoll(Content/Reward/LootRoll.h — 시험 LootRuleTests), 값은 시트 econ 묶음(EconRule).
// 설계: Shared/Document/ECONOMY.md 「처치 보상」.
// ─────────────────────────────────────────────────────────────────────────────
namespace CLootHelper
{
    // 어디서 나온 전리품인가 — 경제 로그(장비 GearSource · 재료 MaterialSource)
    enum class Source : UInt8
    {
        Drop,    // 소모품 드롭 표(Monster.drop_table) — 재료면 Kill로 센다(장비는 드롭 표에 없다 — R126)
        Field,   // 일반 몬스터: 필드 장비 · 정수 · 결정(재료 Kill)
        Elite,   // 정예 보상
        Boss,    // 보스 개인 굴림
    };

    // 처치 한 번의 넘친 전리품 자리 — 죽은 칸부터 둘레로 한 칸씩(같은 칸에 쌓여 가려지지 않게)
    struct DropSpot
    {
        short x = 0, y = 0;
        int   tile = 0;
    };

    struct Rolled
    {
        ItemTableId id = ITEM_TABLE_ID_NONE;   // 0 = 없음
        UInt64      extra = 0;                 // 장비 인스턴스(희귀도 · 옵션 · 귀속 비트)
    };

    // 물건을 user에게: 가방(SystemMessageCode::LootAcquired · 희귀도 알림 · 결사 OnLoot) · 가방이 차면 죽은 자리 둘레에
    // 그 사람의 줍기 우선권(그 사람 > 그 사람의 파티 > 누구나)으로 떨어뜨린다(LootOverflow). 경제 로그(장비 · 재료)도 여기서.
    void Grant(const UserSharedPtr& user, ItemTableId id, UInt16 count, UInt64 extra, Source source, DropSpot& spot);

    // 소모품 드롭 표(Monster.drop_table × drop_rolls) — looter에게(일반 · 정예 · 보스 모두)
    void GiveDropTable(const UserSharedPtr& looter, const MonsterTableRow* species, DropSpot& spot);

    // 필드 장비(일반 몬스터, 지역 단계 tier) — TierLoot 등급 굴림 · 희귀 천장(m_PityRare) · Items(등급, *, Field) 균등 · 직업 무기 바꾸기
    [[nodiscard]] Rolled RollFieldGear(const UserSharedPtr& finder, int tier);
    // 정예 장비 — EliteGear(tier) 칸 · 영웅 천장(m_PityHeroic) · Items(등급, 단, Elite) 균등 · 직업 무기 바꾸기
    [[nodiscard]] Rolled RollEliteGear(const UserSharedPtr& finder, int tier);
    // 보스 개인 굴림 — BossLoot(종족)에서 못 쓰는 무기 행을 빼고 한 번(0 = 없음) · 장비면 영웅 천장(m_PityHeroic)
    [[nodiscard]] Rolled RollBossLoot(const UserSharedPtr& finder, const MonsterTableRow& species);
}
