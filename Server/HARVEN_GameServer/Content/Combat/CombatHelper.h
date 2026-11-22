#pragma once

#include "Database/GameData/CombatData.h"

class CSubject;
class CUser;
class CMonster;

// ─────────────────────────────────────────────────────────────────────────────
// 전투 V2 공용 처리 — 피해 계산·적용, 상태이상, 넉백, 자원(MP), 능력치 알림.
// 모든 함수는 대상의 소유 Zone 스레드에서 호출된다(스킬 시전자와 같은 Zone).
// ─────────────────────────────────────────────────────────────────────────────
namespace CCombatHelper
{
    struct HitCtx
    {
        UInt16 skillId  = 0;
        UInt8  comboId  = 0;
        UInt8  flags    = 0;   // SubjectDamageFlag
        UInt8  flags2   = 0;   // SubjectDamageFlag2
        UInt8  hitIndex = 0;
    };

    // 몬스터가 V2 규칙(대각선 포함 인접)으로 대상을 칠 수 있는가
    [[nodiscard]] bool MonsterCanReach(const MonsterSharedPtr& monster, const UserSharedPtr& target);

    // 시전자 주변(같은 Zone 섹터 이웃)에서 id로 찾기
    [[nodiscard]] SubjectSharedPtr FindNear(const SubjectSharedPtr& me, const ObjID& id);
    // (cx, cy) 체비셰프 radius 안의 살아 있는 몬스터(마을 밖)
    [[nodiscard]] std::vector<MonsterSharedPtr> MonstersAround(const SubjectSharedPtr& me, int cx, int cy, int radius);

    // 유저 공격 피해 굴림(±10%, 치명타). forceCrit이면 확정 치명타.
    [[nodiscard]] UInt32 RollUserDamage(const UserSharedPtr& u, DamageKind kind, UInt32 coeffPercent, bool forceCrit, bool& outCrit);

    // 플레이어 → 몬스터(방어 적용 후 CUserHelper::AttackMonster로 처치·보상까지). 방어 적용 후 피해를 돌려준다.
    UInt32 HitMonster(const UserSharedPtr& attacker, const MonsterSharedPtr& monster, UInt32 damage, const HitCtx& ctx);
    // V2 장비 특수 효과(Data/ItemRules.h) — 기술 적중 뒤(생명 흡수·천둥·메아리·서리·잔불·맹독)
    void ApplyGearProcs(const UserSharedPtr& attacker, UInt16 skillId, bool basic, DamageKind kind,
        const MonsterSharedPtr& monster, UInt32 dealt, UInt8 hitIndex);
    // 몬스터 → 플레이어(회피·방어·철벽 태세·반격). 죽었으면 true. outLanded = 피하지 않고 맞았다(상태이상 · 밀기는 맞았을 때만).
    bool HitPlayerFromMonster(const MonsterSharedPtr& monster, const UserSharedPtr& victim, UInt32 raw, UInt8 flags, bool* outLanded = nullptr);
    // 몬스터 장판 → 플레이어(방어 · 철벽 태세 · 무적은 적용, 회피 · 반격은 없다 — 놓은 몬스터가 이미 죽었을 수 있다). 죽었으면 true.
    // 틱 피해라 지속 피해 모양(DAMAGE_DOT — dotStyle 9 화상 · 15 중독)으로 알린다: 장판에 서 있는 동안 보스 강공격처럼 화면이 크게 흔들리지 않게.
    bool HitPlayerFromAoE(const ObjID& sourceId, const UserSharedPtr& victim, UInt32 raw, UInt16 dotStyle, bool* outLanded = nullptr);
    // 잔상 회피 · 무적(이동기 · 각성기) 중 — 몬스터의 어떤 타격도 닿지 않는다(피해 없는 상태이상 · 밀기 포함)
    [[nodiscard]] bool IsUntouchable(const UserSharedPtr& victim, UInt32 now);

    // 상태이상. power = 강화 세기(%) — Empower · Fortify · Swift(그 밖에는 0)
    void ApplyStatus(const SubjectSharedPtr& target, StatusId id, UInt32 durationMs, UInt8 stacks, const ObjID& source, UInt16 tickDamage = 0, UInt16 power = 0);
    void RemoveStatus(const SubjectSharedPtr& target, StatusId id);
    void HandleStatusTick(const ObjID& subjectId);

    // 넉백 — 몬스터를 (fromX, fromY) 반대쪽으로 tiles칸 민다. 벽에 막히면 outWall.
    // 끝 칸을 먼저 정하고 SUBJECT_FORCED_MOVE_NFY(Knockback)를 보낸 뒤 한 번에 옮긴다(클라가 한 동작으로 밀린다).
    bool Knockback(const MonsterSharedPtr& target, int fromX, int fromY, int tiles, bool& outWall);
    // Phase 2-B 끌어당김 — 몬스터를 (toX, toY) 쪽으로 tiles칸(그 칸 바로 앞에서 멈춘다). 보스는 끌려오지 않는다. 알림은 Knockback과 같다(Pull).
    bool Pull(const MonsterSharedPtr& target, int toX, int toY, int tiles);
    // 서버가 옮기는 플레이어 이동(돌진·도약·그늘 걸음). 걸음 시간 stepMs로 보간.
    bool MovePlayerTo(const UserSharedPtr& player, short x, short y, UInt32 stepMs);

    // 자원·능력치 알림
    void SendResource(const UserSharedPtr& u);
    void SendStats(const UserSharedPtr& u);
    void GainMp(const UserSharedPtr& u, int amount);
    // 레벨업·장비 변경·포인트 분배 뒤: 다시 계산하고 바뀌었으면 알린다.
    void RefreshStats(const UserSharedPtr& u, bool fullHeal);
    void HandleStatAlloc(const UserSharedPtr& u, UInt8 stat, UInt8 amount);
}
