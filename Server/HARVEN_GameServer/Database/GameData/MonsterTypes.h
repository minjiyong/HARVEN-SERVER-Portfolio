#pragma once

#include <cstdint>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 종족 — 형(열거형 · 행 구조체). 값은 GameData/Monster.json(시트 Monster 탭)에서 온다(파사드: Data/MonsterTable.h).
//
// 종족 id는 몬스터 ObjID의 KIND(ContentID::m_KIND, uint16)에 실려 나간다.
// 그래서 SUBJECT_ADD_NFY 같은 기존 패킷 구조를 한 바이트도 바꾸지 않고도
// 클라이언트가 "이 몬스터가 무엇인지"(모양/이름/레벨/최대 HP)를 알 수 있다.
// STRESS_TEST 봇은 KIND를 보지 않는다(ObjID 비교 · 카테고리만).
//
// 규칙: 새 종족은 끝에만 추가하고, 이미 배정된 id는 바꾸거나 재사용하지 않는다.
// 이름은 HARVEN-DESIGN/WORLD.md의 작명 규칙을 따른다(일반 몬스터: 수식어+종 / 세력 역할, 보스: 고유명 + 별칭).
// ─────────────────────────────────────────────────────────────────────────────

// 클라이언트가 어떤 모양으로 그릴지(절차적 드로잉 루틴) 고르는 값. 서버는 쓰지 않는다.
enum class MonsterArchetype : uint8_t
{
    eBlob,        // 덩이(수액·이슬·진흙)
    eQuadruped,   // 늑대/곰/개/칼둔 사냥개
    eHumanoid,    // 그락족/걷는 망자
    eInsect,      // 굴개미/전갈/거미
    eGolem,       // 석인/거인
    eGhost,       // 정령/망령
    eDragon,      // 비룡
    eDemon,       // 칼둔의 것
    eCrab,        // 게
    eMushroom,    // 버섯
    eSkeleton,    // 백골/파약 기사
    // ── Phase 2-E(끝에만 추가) ──
    eDummy,       // 허수아비(훈련장) — 나무 기둥 + 짚 몸통 + 과녁
};

// V2: 몬스터 강공격 패턴 — 모두 예고(telegraph) 뒤에 판정되어 피할 수 있다.
enum class MonsterPattern : uint8_t
{
    None,     // 평타만
    Charge,   // 거리 2~4에서 직선 예고 → 돌진(선 위 대상 피해 + 둔화)
    Slam,     // 붙어 있을 때 주변 원 예고 → 내려찍기
    Ranged,   // 거리 3~5에서 대상 칸 예고 → 투척/주문
    Leap,     // 거리 2~4에서 대상 주변 원 예고 → 도약
};

enum class MonsterVariant : uint8_t
{
    eNormal,
    eScorpion,    // eInsect 변형: 집게 + 꼬리
    eSpider,      // eInsect 변형: 다리 8개
    eArmored,     // eSkeleton/eHumanoid 변형: 갑옷 + 투구
    eZombie,      // eHumanoid 변형
    eLava,        // eGolem 변형: 균열에서 빛남
    eIce,         // eGolem 변형
};

struct MonsterTableRow
{
    uint16_t         id           = 0;
    const char*      name         = "";     // UTF-8, SUBJECT_ADD_NFY.name[20]에 들어가야 하므로 19바이트 이하
    bool             aggressive   = false;  // true = AGGRO(시야에 들어오면 선공), false = PASSIVE(맞으면 반격)
    uint8_t          level        = 1;
    uint16_t         maxHp        = 60;
    uint16_t         attack       = 3;
    uint32_t         exp          = 5;
    uint32_t         adenaMin     = 0;
    uint32_t         adenaMax     = 0;
    uint8_t          adenaChance  = 0;      // %
    uint8_t          dropTable    = 0;      // DropTable 번호(서버 DropTable.cpp)
    uint8_t          dropRolls    = 1;      // 한 번 처치에 드롭을 몇 번 굴리는가(보스는 여러 번)
    uint16_t         attackIntervalMs = 1000;
    uint16_t         moveIntervalMs   = 500;
    uint8_t          leash        = 7;      // 스폰 지점에서 이 칸 수를 넘으면 추격 포기(기존 7칸)
    bool             boss         = false;
    // ── 클라이언트 표시 전용 ──
    MonsterArchetype archetype    = MonsterArchetype::eBlob;
    MonsterVariant   variant      = MonsterVariant::eNormal;
    uint32_t         color1       = 0xFFFFFF;  // 0xRRGGBB
    uint32_t         color2       = 0x000000;
    uint16_t         scale        = 100;       // 100 = 1.0배
    // ── Phase 2-E ──
    bool             dummy        = false;     // 훈련장 허수아비: 움직이지 않고 · 때리지 않고 · 죽지 않는다(HP가 늘 찬다) · 경험치 · 전리품 없음
};
