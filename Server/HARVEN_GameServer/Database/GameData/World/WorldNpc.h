#pragma once
// 마을 NPC 표(NPC대사 묶음 — 탭 Npc).
// WorldData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/WorldData.h가 원래 순서로 include한다.
#include <cstddef>
#include <cstdint>
#include <span>

namespace WorldData
{
    // ── NPC ─────────────────────────────────────────────────────────────────
    // V2: 마을 NPC는 서버가 배회시킨다(Npc/NpcHelper — 마을 Zone 스레드, NPC_* 패킷).
    // Sector의 오브젝트로 올리지 않으므로 SUBJECT_ADD_NFY로는 가지 않는다. 여기의 x, y는 "집"(배회의 중심).
    enum class NpcKind : uint8_t
    {
        eMerchant, eWeaponSmith, eTeleporter, eGuard,
        // V2 — 살아 있는 마을
        eElder,       // 촌장(이야기의 시작)
        ePriest,      // 사제(예배당 — 치유의 기도)
        eInnkeeper,   // 여관 주인(수프·소문)
        eBard,        // 음유시인(노래)
        eChild,       // 뛰노는 아이
        eVillager,    // 주민
        eFarmer,      // 농부
        eDog, eCat, eChicken,   // 동물(쓰다듬기)
    };

    [[nodiscard]] constexpr bool IsAnimal(NpcKind k) noexcept
    {
        return k == NpcKind::eDog || k == NpcKind::eCat || k == NpcKind::eChicken;
    }

    // NPC 서비스(NPC_SERVICE_REQ)
    enum class NpcService : uint8_t { None, Heal, Soup };

    struct NpcRow
    {
        uint16_t    id;
        NpcKind     kind;
        const char* name;
        int16_t     x, y;          // 집(배회의 중심) — 걸을 수 있는 칸
        uint8_t     facing;        // 클라 표시용: 0=남서 1=남동 2=북동 3=북서
        const char* greeting;
        uint8_t     wander = 0;    // 배회 반경(칸). 0 = 제자리(가끔 돌아본다)
        uint16_t    stepMs = 650;  // 한 칸 걷는 시간(ms)
        uint32_t    color = 0;     // 옷 색(클라, 0 = 종류 기본)
        const char* role = nullptr;   // 머리 위 둘째 줄(클라, 없으면 종류 기본 — "잡화 상점" 등). Phase 4-B
    };

    // ── 표(탭 Npc) — 파사드. 값은 GameData/Npc.json(시트 HARVEN_DATA의 Npc 탭)에서 온다(Data/Load/Bind_Npc.cpp).
    // 이름 = 직함 + 변경말 이름(HARVEN-DESIGN/WORLD.md 3.2). 이름 · 말 · 성격만 Npc 탭에 둔다.
    // 자리(x, y = 집)는 마을 원본(HARVEN-DESIGN/tools/mapgen/denica/anchors.txt의 npc → 마을 표 TownNpcHome)이 주인 —
    // 로더가 Npc 행에 채운다. 마을을 다시 그리면 NPC 자리도 그 파일에서 옮긴다.
    // 규칙(예전 static_assert → 부팅 검증 R066~R070): 집은 마을 안 걸을 수 있는 칸 · id 1~63 겹침 없음 ·
    // 마을 원본의 집은 모두 Npc 탭에 있는 NPC · 최대 48명. id는 영구(ids.lock).

    // 모든 행(Npc.json 순서 = id 순). 포인터 · 스팬은 스냅샷이 살아 있는 동안 유효하다.
    [[nodiscard]] std::span<const NpcRow> Npcs() noexcept;
    [[nodiscard]] size_t NpcCount() noexcept;

    // NPC 집(배회 중심) — 마을 원본 TownNpcHome. 없으면 -1.
    [[nodiscard]] int16_t HomeX(uint16_t id) noexcept;
    [[nodiscard]] int16_t HomeY(uint16_t id) noexcept;

    // NPC와 상호작용할 수 있는 거리(체비셰프). 클라이언트 표시값이 아니라 서버 판정 기준이다.
    inline constexpr int kNpcInteractRange = 3;

    // id → 행(없으면 nullptr). id 색인으로 O(1).
    [[nodiscard]] const NpcRow* FindNpc(uint16_t id) noexcept;

    [[nodiscard]] constexpr bool IsNearNpc(const NpcRow& npc, int x, int y) noexcept
    {
        const int dx = x - npc.x;
        const int dy = y - npc.y;
        return (dx < 0 ? -dx : dx) <= kNpcInteractRange && (dy < 0 ? -dy : dy) <= kNpcInteractRange;
    }

    // 예전 static_assert(Detail::ValidateNpcs())의 규칙은 부팅 검증으로 옮겼다 — GameDataLoad::ValidateNpc(R066~R071, 행 위치를 남긴다).
}
