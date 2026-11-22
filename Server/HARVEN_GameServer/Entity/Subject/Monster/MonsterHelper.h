#pragma once
#include "Entity/Subject/Monster/Monster.h"
#include "Content/Field/ZoneTypes.h"

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 공용 처리 — 이동(걸음 · 배회 · 길 찾기) · 리스폰 · Zone 초기화.
// 무엇을 할지(쫓기 · 치기 · 기술 · 귀환 · 잠들기)는 행동 트리가 정한다(Entity/Component/Behavior — CEntityBehaviorHelper).
// 여기 함수는 트리의 잎(T_)이 부르는 손발이다. 모두 몬스터의 소유 Zone 스레드에서.
// ─────────────────────────────────────────────────────────────────────────────
namespace CMonsterHelper
{
    // 목표 칸 쪽으로 한 걸음(캐시한 경로 → 한 칸 직진 → 새 A*). 옮겼으면 true. stepMs = 이 걸음의 보간 시간(SUBJECT_MOVE_NFY.move_time).
    bool StepToward(const MonsterSharedPtr& monster, short goalX, short goalY, UInt32 stepMs);
    // 아무 방향 한 걸음(배회) — Zone 밖 · 마을 · 막힌 칸이면 서 있는다
    bool RandomStep(const MonsterSharedPtr& monster, UInt32 stepMs);

    // Zone 경계 ZONE_BOUNDARY_MARGIN 칸 안인가 — 추격은 여기서 돌아선다(몬스터는 Zone을 넘지 않는다)
    [[nodiscard]] bool IsNearZoneBoundary(short x, short y);
    // 한 걸음 간격(둔화 늦게 · 질풍 빠르게)
    [[nodiscard]] UInt32 MoveDelayMs(const MonsterSharedPtr& monster, UInt32 now);
    // 평타 간격(빙결 두 배 · 질풍 빠르게)
    [[nodiscard]] UInt32 AttackDelayMs(const MonsterSharedPtr& monster, UInt32 now);
    // 이동(forMove) · 공격을 막는 상태가 풀리는 시각 — 막히지 않았으면 0
    [[nodiscard]] UInt32 DisabledUntil(const MonsterSharedPtr& monster, UInt32 now, bool forMove);

    // 정예 굴림(경제 1차 명세 §6) — 보스 · 허수아비가 아니고 지역(Region.tier) ≥ elite_min_tier면 elite_permille.
    // 정예면 최대 HP · 공격력 배율, 아니면 종 기본값(CMonster::SetElite). HP는 부른 쪽이 채운다. 정예가 됐으면 true.
    bool RollElite(const MonsterSharedPtr& monster);

    // 다시 태어나기 — 자리 · HP · 정예를 새로 정하고 보는 사람에게 SUBJECT_RESPAWN_NFY(정예이거나 직전에 정예였으면 뒤에 SUBJECT_INFO_NFY)
    void HandleRespawn(const ObjID& monsterId);

    // Zone 전용 스레드에서 GSector 설정 이후 호출.
    // 지역(Data/WorldData.h)의 스폰 가중치대로 종족을 정하고, 필드 보스를 배치한다.
    void InitForZone(ZoneId zoneId);
};
