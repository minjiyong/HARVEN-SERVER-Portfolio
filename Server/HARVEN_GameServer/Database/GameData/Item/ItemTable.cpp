#include "pch.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/Item/ItemTableData.h"

// 서버 쪽 조회 지점은 여전히 이 함수 하나다. 행은 기획 데이터 스냅샷(GameData/Item.json — 시트 Item 탭)에 있고,
// ItemTableData::Find가 id 색인으로 바로 찾는다(Data/Load/Bind_Item.cpp).
//
// 예전 컴파일 타임 검사 IsItemTableValid(id ≠ 0 · 표시 이름 · 장비 겹치기 1 · 매입가 ≤ 판매가 · id 중복 없음)는
// 부팅 검증으로 옮겼다(GameDataLoad::ValidateItem, R087~R091) — 틀린 표면 서버가 뜨지 않고 시트 위치를 찍는다.
//
// attackBonus는 무기 공격력으로 전투 능력치에 더해진다(Data/ItemRules.h Accumulate) — 등급별 드롭
// 확률(DropTable.cpp)이 크게 벌어지는 것과 같은 취지로, 공격력 보너스도
// 등급이 오를수록 큰 폭으로 뛰게 잡았다.
const ItemTableRow* ItemTable::Find(ItemTableId id) noexcept
{
    return ItemTableData::Find(id);
}
