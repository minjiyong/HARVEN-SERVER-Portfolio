#pragma once

#include "Database/GameData/CombatData.h"
#include "Database/GameData/SkillData.h"

class CSubject;
class CUser;
class CMonster;
enum class SkillResult : UInt8;

// ─────────────────────────────────────────────────────────────────────────────
// 기술 판정 내부 도구 — 플레이어 기술(CUserSkillHelper)이 쓴다. 구현은 SkillDetail.cpp.
// 모두 시전자의 소유 Zone 스레드에서 부른다.
// ─────────────────────────────────────────────────────────────────────────────
namespace SkillDetail
{
    // 한 번의 적중 — 연계(대상 상태) 판정, 피해, 상태이상, 넉백까지.
    struct HitSpec
    {
        UInt32   coeff = 100;
        bool     forceCrit = false;
        UInt32   bonusPercent = 0;   // 추가 피해 %(연계 외)
        UInt8    comboId = 0;        // 강제 연계 표시
        UInt8    flags2 = 0;
        UInt8    hitIndex = 0;       // 예전: 연타 번호 / 확장: 0x80 | 타격표 번호(클라가 시각표로 반응을 찾는다)
        bool     applyStatus = true;
        bool     applyKnockback = true;
        bool     evaluateCombo = true;
        // ── Phase 2-B ──
        HitReact react = HitReact::None;   // 확장 타격의 반응(예전 넉백 칸 대신)
        UInt8    stagger = 0;              // 보스 무력화 수치
        bool     counter = false;          // 보스 강공격 예고를 끊는다
        bool     extras = false;           // 잔향 · 분신 · 함성 · 후방 보너스 · 게이지(확장 타격)
        UInt32   castSeq = 0;              // 분신은 시전마다 한 번
        short    fromX = 0, fromY = 0;     // 넉백 · 끌어당김 기준(시전자 또는 지점)
    };

    void SendResult(const UserSharedPtr& u, UInt16 seq, UInt16 skillId, SkillResult r, UInt32 ms);
    [[nodiscard]] SubjectSharedPtr ResolveEnemy(const UserSharedPtr& u, const ObjID& targetId);
    [[nodiscard]] bool IsMonster(const SubjectSharedPtr& s);
    [[nodiscard]] bool Walkable(int x, int y);
    // 강제 이동 한 걸음(돌진 · 구르기 · 도약 · 달 가르기): 도착 칸이 열리고, 대각이면 두 옆도 열려야 한다(MoveRule::CleanStep)
    [[nodiscard]] bool StepOk(int x, int y, int dx, int dy);
    // 기술 이동(돌진 · 점멸 · 그림자 걸음 · 구르기)은 지금 Zone 안에서만 — 다른 Zone으로 넘어가면 처리 중인 스레드가 CUser를 놓친다(넉백 · 끌어당김과 같은 규칙).
    [[nodiscard]] bool InSameZone(const UserSharedPtr& u, int x, int y);
    [[nodiscard]] MonsterSharedPtr MonsterAt(const UserSharedPtr& u, int x, int y);
    // 돌려주는 값 = 실제로 준 피해(0이면 헛침 · 사망)
    UInt32 DealHit(const UserSharedPtr& u, const SkillRow& row, const SubjectSharedPtr& target, HitSpec spec);
    // 예전 판정(기술 id마다 switch) — 플레이어 기술의 Legacy 기술 · 잔상 회피 · 돌진 · 그늘 걸음이 부른다
    void Execute(const UserSharedPtr& u, const SkillRow& row, const ObjID& targetId, short tx, short ty);
    // 옛길 도약 착지 칸(막혔으면 가까운 쪽으로). 못 가면 false
    bool BlinkLanding(const UserSharedPtr& u, const SkillRow& row, short& tx, short& ty);

    // 플레이어 기술이 Legacy 일정으로 예전 판정(Execute)을 부르는 동안, 그 안의 DealHit에 표의 무력화 · 반격 · 반응 · 게이지를 싣는다.
    // 같은 Zone 스레드 안에서 동기로만 쓰므로 thread_local 한 칸이면 된다.
    class CLegacyScope
    {
    public:
        explicit CLegacyScope(const HitSpec& ctx);
        ~CLegacyScope();
        CLegacyScope(const CLegacyScope&) = delete;
        CLegacyScope& operator=(const CLegacyScope&) = delete;
    private:
        const HitSpec* m_pPrev;
    };
}
