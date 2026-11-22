#include "pch.h"
#include "Content/Reward/DropTable.h"

#include <algorithm>
#include <random>

#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/Item/ItemTableData.h"
#include "Base/Core/CoreTLS.h"
#include "Database/GameData/Load/GameData.h"
#include "Database/GameData/Load/Snapshot.h"

// 소모품 드롭 표 — 값은 기획 데이터 스냅샷(시트 DropTable · DropEntry 탭)에서 온다. 장비는 드롭 표에 없다(R126 — 경제 1차:
// 필드 · 정예 · 보스 장비는 등급 굴림, Content/Reward/LootHelper.cpp). 이 파일에는 굴림 규칙만 있다(가중치 · 개수). 표 값은 Data/Load/Bind_Item.cpp가 읽고,
// 예전 시작 시점 검사(ValidateDropTable — ASSERT_CRASH)는 부팅 검증으로 옮겼다(GameDataLoad::ValidateItem, R093~R098).
//
// 이 값들은 전부 조정 가능한 밸런싱 데이터다. 등급이 높을수록 weight를 큰 폭으로
// 낮춰 "고등급일수록 현저히 희귀함"을 유지했다. 표마다 noDropWeight + weight 합 = 1000으로 맞춰 두면 weight가 곧 천분율이다.

size_t DropTable::TableCount() noexcept
{
    return GD().item.drops.size();
}

DropTable::RollResult DropTable::Roll(UInt8 tableId) noexcept
{
    // 0 = 드롭 없음(Monster.drop_table "0 = 없음" — 처치 쪽 CUserHelper::HandleItemDrop은 아예 굴리지 않는다)
    if (tableId == 0)
        return {};
    const std::vector<SnapItem::Drop>& drops = GD().item.drops;   // 1행 이상(로드 검증)
    if (drops.empty())
        return {};
    if (tableId > drops.size())
        tableId = 1;
    const SnapItem::Drop& table = drops[tableId - 1u];

    // 분포 표(가중치 정규화 · 누적 확률)는 스냅샷이 표마다 한 번 미리 계산해 둔다(index 0 = 드롭 없음) —
    // 판(세대)이 바뀌어도 옛 표가 남지 않게 함수 안에 데이터에서 나온 값을 캐시하지 않는다.
    // 아래 분포 객체는 데이터와 무관한 빈 껍데기다(discrete_distribution의 굴림은 넘긴 param만 쓴다 — 예전
    // 스레드별 캐시 분포를 부를 때와 같은 난수 · 같은 결과). LRng(Core/CoreTLS.h)도 스레드마다 독립적으로
    // 시드되므로, 이 함수는 몇 개의 Zone 스레드가 동시에 호출해도 서로 간섭하지 않는다.
    thread_local std::discrete_distribution<size_t> LDropDist;
    const size_t index = LDropDist(LRng, table.dist);
    if (index == 0)
        return {};  // 드롭 없음

    const SnapItem::DropEntry& entry = table.entries[index - 1];
    std::uniform_int_distribution<UInt16> countRoll(entry.minCount, entry.maxCount);

    RollResult result;
    result.hasItem = true;
    result.itemId  = entry.itemId;
    result.count   = countRoll(LRng);
    return result;
}
