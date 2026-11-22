#pragma once

#include <cstddef>
#include <span>

#include "Base/Core/Types.h"
#include "Database/GameData/Item/ItemTableRow.h"

// 몬스터 처치 시 적용되는 소모품 드롭 테이블들. 몬스터 종족(Data/MonsterTable.h)의
// dropTable 번호로 어느 표를 굴릴지 정한다 — 약한 몬스터는 물약/두루마리 위주,
// 강한 몬스터와 보스는 진한 물약 · 각인 두루마리가 나온다. 장비는 이 표에 없다(R126 — 경제 1차의
// 필드 · 정예 · 보스 장비 굴림은 Content/Reward/LootHelper.cpp).
//
// 한 번의 굴림 결과는 항상 "아무것도 없음" 또는 "아이템 하나"뿐이다. 여러 개를
// 떨어뜨리는 보스는 굴림 횟수(MonsterTableRow::dropRolls)로 표현한다.
// 그로시는 이 표가 아니라 종족의 adenaMin/adenaMax로 따로 굴린다.
//
// 값은 GameData/DropTable.json · DropEntry.json(시트 DropTable · DropEntry 탭)에서 온다 — 서버 부팅 때 스냅샷에 올리며
// 굴림용 분포 표(누적 확률)도 그때 미리 계산해 둔다(Data/Load/Bind_Item.cpp). 규칙(부팅 검증): 아이템이 있다(R093) ·
// 1 ≤ min_count ≤ max_count(R096) · max_count ≤ 아이템 최대 겹치기(R097) · 표마다 가중치 합 > 0(R098).
// 엔트리의 type · grade는 Item 탭 값을 그대로 쓴다(열이 없다 — R094 · R095는 구조로 보장).
namespace DropTable
{
    struct RollResult
    {
        bool        hasItem = false;
        ItemTableId itemId  = ITEM_TABLE_ID_NONE;
        UInt16      count   = 0;
    };

    // 표 수(DropTable 탭 행 수 — 표 번호 1 ~ TableCount). 지금: 1 = 1단계 몬스터 ... 5 = 5단계, 6 = 보스 공용, 7 = 세베린, 8 = 즈마이, 9 = 그라목
    [[nodiscard]] size_t TableCount() noexcept;

    // 스레드 안전: 스레드마다 독립된 상태(LRng)와 불변 스냅샷(미리 계산한 분포 표)만 사용하므로
    // 여러 Zone 스레드에서 동시에 호출해도 안전하다. 0 = 드롭 없음(Monster.drop_table "0 = 없음"), 없는 표 번호면 1번 표를 쓴다.
    [[nodiscard]] RollResult Roll(UInt8 tableId) noexcept;
}
