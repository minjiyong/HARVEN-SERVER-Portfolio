#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

#include "Database/GameData/MonsterTypes.h"
#include "Database/GameData/Gen/MonsterIds.h"   // MonsterTable::Id::* — Monster.code_name에서 생성(datatool export)

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 종족 표 — 파사드. 값은 GameData/Monster.json(시트 HARVEN_DATA의 Monster 탭)에서 온다.
// 서버 부팅 때 GameData::LoadFromDir가 읽어 불변 스냅샷에 둔다(Data/Load/Bind_Monster.cpp).
//
// 규칙(예전 static_assert → 부팅 검증 R031~R035): id는 1부터 연속 · 이름 1~19바이트 · HP · 공격력 > 0 ·
// 그로시 최소 ≤ 최대 ≤ 65535 · 드롭 횟수 > 0. R102: 공격 · 이동 간격 ≥ kMinActIntervalMs.
// 새 종족은 끝에만 추가하고 이미 배정된 id는 바꾸거나 다시 쓰지 않는다(ids.lock).
// 이름은 HARVEN-DESIGN/WORLD.md의 작명 규칙을 따른다(일반 몬스터: 수식어+종 / 세력 역할, 보스: 고유명 + 별칭).
// ─────────────────────────────────────────────────────────────────────────────
namespace MonsterTable
{
    using A = MonsterArchetype;
    using V = MonsterVariant;

    // 공격 · 이동 간격의 바닥(ms) — 표는 R102가 막고, 타이머를 거는 곳(Monster::GetAttackIntervalMs · GetMoveIntervalMs)도 이 값으로 자른다.
    // 0이면 공격 · 이동 타이머가 지연 없이 다시 걸려 Zone · 타이머 스레드가 넘친다.
    inline constexpr uint16_t kMinActIntervalMs = 100;

    // 모든 행(id 순서 — 행 i의 id = i + 1). 값은 GameData에서.
    [[nodiscard]] std::span<const MonsterTableRow> Rows() noexcept;
    [[nodiscard]] size_t RowCount() noexcept;

    // 보스 별칭 — 이름표 · 보스 체력 막대 · 등장 알림에 이름과 함께 쓴다(클라 표시 전용). 없으면 "".
    // 패킷의 이름(19바이트)과 따로 두어 길이 제한 없이 한 구절을 붙인다. 값은 Monster.epithet.
    [[nodiscard]] const char* BossEpithet(uint16_t id) noexcept;

    // id → 행(없으면 nullptr). id가 1부터 연속이라 인덱스로 바로 찾는다(O(1)).
    [[nodiscard]] const MonsterTableRow* Find(uint16_t id) noexcept;
}
