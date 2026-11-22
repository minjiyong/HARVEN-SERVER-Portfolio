#pragma once

#include "Base/Core/Types.h"
#include "Base/ObjID/ObjID.h"

// ─────────────────────────────────────────────────────────────────────────────
// 장판(지면 효과 · 오라) — REIW Entity/Subject/Skill/AreaOfEffect + Content/Buff/Buff-Aura를 하르벤식으로.
//
// REIW는 장판마다 게임 객체(CCombatActor)를 만들어 필드에 넣고 OnUpdate 폴링 · BT로 돈다. 하르벤의 장판은 섹터에 들지 않는
// 작은 기록(CAreaOfEffect)이고 Zone 스레드마다 하나인 표에 산다. 틱은 Zone 타이머(EV_MONSTER_AOE — 주인 몬스터 id로 걸어
// 이 Zone 스레드가 직접 돈다)가 간격마다 깨운다 — 폴링 · 잠금 · 할당이 없다(칸은 다시 쓴다, 핸들의 세대로 낡은 예약을 거른다).
//
//  · 적(플레이어): 틱 피해(놓을 때 주인 공격력 × coeff%) · 해로운 상태(중독 · 화상 · 둔화 …)
//  · 동료(몬스터): 회복(최대 HP × heal%) · 강화(광폭 · 굳건 · 질풍 — 세기 %)
//  · follow: 주인을 따라 움직이는 오라 — owner_bound면 주인이 깨어 있는 동안 계속(잠들거나 죽으면 사라진다)
// 값은 MonsterAoE 표. 모든 함수는 그 Zone 스레드에서.
// ─────────────────────────────────────────────────────────────────────────────
struct CAreaOfEffect
{
    UInt32 handle = 0;     // 0 = 빈 칸. (세대 << 16) | (칸 + 1)
    UInt16 gen = 0;
    UInt16 aoeId = 0;
    ObjID  ownerId{};
    UInt16 attack = 0;     // 놓을 때 주인 공격력(광폭 반영) — 주인이 죽어도 같은 세기로 남는다
    short  x = 0, y = 0;
    UInt32 endAt = 0;
};

namespace CAreaOfEffectHelper
{
    // 장판을 놓는다 — 핸들(0 = 놓지 못함: 표에 없음 · 마을 · Zone 밖). 첫 틱은 first_ms 뒤.
    UInt32 Spawn(const MonsterSharedPtr& owner, UInt16 aoeId, short x, short y);
    void Remove(UInt32 handle);
    // 주인에 묶인(owner_bound) 장판을 모두 거둔다 — 주인이 잠들거나 죽을 때
    void RemoveByOwner(const ObjID& ownerId);
    // EV_MONSTER_AOE
    void HandleTick(const ObjID& ownerId, UInt32 handle);
    // 이 Zone 스레드의 살아 있는 장판 수(시험 · 통계)
    [[nodiscard]] size_t ActiveCount();
}
