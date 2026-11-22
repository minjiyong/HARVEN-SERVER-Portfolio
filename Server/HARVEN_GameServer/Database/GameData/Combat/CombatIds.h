#pragma once
// 직업 · 상태이상 id(열거형) — 여러 묶음이 함께 쓴다.
// CombatData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/CombatData.h가 원래 순서로 include한다.
#include <cstddef>
#include <cstdint>

enum class ClassId : uint8_t
{
    Knight = 0,        // 서약기사
    Engraver = 1,      // 각인술사
    NightHunter = 2,   // 밤사냥꾼
    Count
};

enum class StatusId : uint8_t
{
    None = 0,
    Stun,        // 기절: 이동·공격 불가
    Freeze,      // 빙결: 이동 불가, 공격 느려짐. 화염에 맞으면 증발 폭발
    Burn,        // 화상: 초당 피해
    Poison,      // 중독: 초당 피해(중첩), 그믐 베기가 터뜨린다
    Slow,        // 둔화: 이동 느려짐
    Guard,       // 철벽 태세: 받는 피해 감소 + 반격
    Ambush,      // 기습: 다음 공격 확정 치명타
    Evade,       // 잔상: 잠깐 모든 공격을 피함
    Marked,      // 각인 표식(벼락 사슬 연쇄 대상)
    // ── Phase 2-B(끝에만 추가) ──
    Oath,        // 서약 해방: 슈퍼아머 + 잔향 타격(30%)
    Rally,       // 함성: 공격 +15%
    Veil,        // 밤 장막: 은신(몬스터가 놓친다) · 이동 +30%
    Taunt,       // 도발: 이 몬스터는 건 사람을 쫓는다
    Broken,      // 무력화: 보스 무력화 게이지가 비었다 — 기절 + 받는 피해 +30%
    Clone,       // 먹물 분신: 남은 따라 하기 횟수(중첩)
    // ── 몬스터 AI(끝에만 추가) — 세기(%)는 거는 쪽이 정한다(StatusSlot::power — 장판 · 기술 표의 status_power) ──
    Empower,     // 광폭: 주는 피해 +세기%
    Fortify,     // 굳건: 받는 피해 −세기%(최대 90%)
    Swift,       // 질풍: 이동 · 공격 간격 ÷ (1 + 세기%)
    Count
};
