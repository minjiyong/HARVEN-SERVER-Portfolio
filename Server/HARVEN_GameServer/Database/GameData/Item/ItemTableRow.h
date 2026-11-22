#pragma once

#include "Protocol/Protocol.h"

// 아이템의 취급 방식을 구분하는 최소 분류.
// 값이 늘어나는 건 괜찮지만, 이미 배정된 값은 절대 바꾸지 않는다 —
// 클라이언트도 같은 숫자로 분기하게 되므로, 값이 바뀌면 서버/클라이언트 데이터가
// 재빌드 없이 어긋난다.
enum ItemType : uint8_t
{
    eConsumable = 0,  // 사용 시 소모됨 (포션 등)
    eEquipment  = 1,  // 장착형. 기본적으로 스택 불가(maxStack = 1)
    eMaterial   = 2,  // 제작 재료 등, 사용 로직 없음
    eEtc        = 3,  // 분류 미정
    // 그로시. 필드에 떨어진 상태로만 존재하고, 주우면 인벤토리 슬롯이 아니라
    // 지갑(Inventory::GetAdena)으로 들어간다 — 슬롯을 차지하지 않는다.
    eCurrency   = 4,
};

// 소비 아이템을 "사용"했을 때 일어나는 일. ItemType과 같은 이유로 이미 배정된 값은
// 절대 바꾸지 않는다. 회복 물약은 기존 경로(healAmount)를 그대로 쓰고, 나머지
// 효과는 인벤토리 밖의 시스템(텔레포트/버프/강화)이 필요해서 UserHelper가 처리한다.
enum class ItemEffect : uint8_t
{
    eNone           = 0,
    eHeal           = 1,  // healAmount만큼 HP 회복
    eReturnTown     = 2,  // 귀환 두루마리
    eTeleportRandom = 3,  // 옛길 두루마리 — 현재 지역 안의 무작위 위치
    eHaste          = 4,  // 바람걸음 물약 — effectValue초 동안 이동 가속
    eEnchantWeapon  = 5,  // 각인 두루마리 — ITEM_ENCHANT_REQ로만 사용(effectValue: 0 일반 / 1 축복)
};

// 장비 등급. 열거자 이름은 옛 「바닥」 이름이라 헷갈리니 뜻은 아래로 고정한다(경제 1차 — Shared/Document/ECONOMY.md):
//   eNone 일반 · eRare 고급 · eEpic 희귀 · eUnique 영웅 · eLegendary 전설 · eMythic 신화
//   (= 바닥 희귀도 ItemRules::FloorRarity — Common · Uncommon · Rare · Heroic · Legendary · Mythic)
// 장비가 아니면 grade는 표시 색 전용(재료의 등급 색 — 희귀도 · 옵션에는 쓰지 않는다: FloorRarity는 비장비를 Common으로 본다).
// ItemType과 같은 이유로, 이미 배정된 값은 절대 바꾸지 않는다(끝에만 추가).
// ItemType과 달리 scoped enum(enum class)으로 둔다 — unscoped로 두면 eNone이
// EnumCategory::eNone과 같은 전역 스코프에서 충돌한다(둘 다 unscoped라면
// 재정의 오류). eRare/eEpic 등도 흔히 쓰일 이름이라 어차피 항상
// ItemGrade::eXxx로 명시하는 게 안전하다.
enum class ItemGrade : uint8_t
{
    eNone      = 0,   // 일반
    eRare      = 1,   // 고급
    eEpic      = 2,   // 희귀
    eUnique    = 3,   // 영웅
    eLegendary = 4,   // 전설
    eMythic    = 5,   // 신화(1차에는 행 없음)
};
inline constexpr int kItemGradeCount = static_cast<int>(ItemGrade::eMythic) + 1;   // 등급별 표(Dismantle 등)의 행 수

// 장비를 얻는 길(비트 — Item.route 열, 여러 개는 시트에서 `Field|Craft`). 값은 시트 · 클라가 이름으로 쓰지만 비트 값도 바꾸지 않는다.
enum class ItemRoute : uint8_t
{
    Shop  = 1,    // 상점(Shop 표가 판다)
    Field = 2,    // 필드 몬스터 장비 굴림(TierLoot)
    Elite = 4,    // 정예 장비 굴림(EliteGear)
    Boss  = 8,    // 보스 개인 굴림(BossLoot)
    Craft = 16,   // 대장장이 제작(CraftRecipe)
    Quest = 32,   // 퀘스트 보상
};

// 귀속. 이미 배정된 값은 바꾸지 않는다(끝에만 추가).
enum class ItemBind : uint8_t
{
    eNone    = 0,   // 귀속 없음
    eAcquire = 1,   // 획득 귀속 — 얻는 순간 귀속(장비는 인스턴스 bit 8, 겹치는 재료는 행으로만 판단)
};

// 제작 · 분해의 부위 묶음(CraftRecipe.part). 무기 = equip_slot Weapon(또는 None + 장비), 방어구 = Helmet ~ Cloak, 장신구 = Necklace · Ring.
enum class CraftPart : uint8_t
{
    eWeapon    = 0,
    eArmor     = 1,
    eAccessory = 2,
    Count
};

// V2 장비 부위. 한 부위에는 장비 하나만 장착된다(Inventory::TryEquip).
// 예전 행(무기만 있던 시절)은 equipSlot을 적지 않았으므로 None + eEquipment = 무기로 본다
// (ItemTableData::SlotOf). 값은 서버·클라가 같은 표로 읽으므로 이미 배정된 값은 바꾸지 않는다.
enum class EquipSlot : uint8_t
{
    None     = 0,
    Weapon   = 1,
    Helmet   = 2,
    Armor    = 3,
    Gloves   = 4,
    Boots    = 5,
    Cloak    = 6,
    Necklace = 7,
    Ring     = 8,
    Count
};

// 무기 종류(표시·외형·공격력 종류). Staff는 공격력 보너스가 마법 공격력으로 들어간다.
enum class WeaponKind : uint8_t
{
    None       = 0,
    Sword      = 1,
    Greatsword = 2,
    Saber      = 3,   // 곡도(기병 곡도)
    Dagger     = 4,
    Staff      = 5,
};

// ContentID::KIND(uint16_t)에 그대로 실려서 아이템 인스턴스 ObjID, 인벤토리 슬롯,
// DB 저장 데이터를 오가는 영구 식별자. ItemTable(아이템 테이블) 안의 한 행(row)을 가리킨다.
// 0은 "빈 슬롯 / 없음"을 뜻하는 예약값이므로 실제 아이템에는 절대 배정하지 않는다.
using ItemTableId = uint16_t;
constexpr ItemTableId ITEM_TABLE_ID_NONE = 0;

// 서버가 들고 있는 아이템의 정적(불변) 정보 — 아이템 테이블의 행(row) 하나.
// 인스턴스별로 달라지는 값(수량, 슬롯 위치, 인스턴스 시리얼 등)은 여기 넣지 않는다 —
// 그건 나중에 만들 인벤토리 슬롯 쪽 데이터의 몫이다. 이 구조체는 오직
// ItemTable(ItemTable.cpp) 안의 상수 배열을 채우는 용도로만 쓴다.
struct ItemTableRow
{
    ItemTableId    id                   = ITEM_TABLE_ID_NONE;
    ItemType       type                 = ItemType::eEtc;
    uint16_t       maxStack             = 1;   // 1 = 스택 불가
    char           name[ITEM_NAME_SIZE] = {};
    ItemGrade      grade                = ItemGrade::eNone;  // 장비: 등급(바닥 희귀도). 장비가 아니면 표시 색 전용
    // 무기를 장착 중일 때 공격력(지팡이는 마법 공격력)에 더해지는 보너스 — 희귀도 배율은
    // Data/ItemRules.h Accumulate. 무기가 아니면 항상 0(의미 없음). 방어구는 아래 armorBonus.
    uint16_t       attackBonus          = 0;
    // ConsumableItem 사용(ITEM_USE_REQ) 시 회복되는 HP. 소비 아이템이 아니면
    // 항상 0(의미 없음) — 장비 쪽 attackBonus와 같은 자리(무기가 아니면 0)를 따랐다.
    uint16_t       healAmount           = 0;

    // ── 아래는 리니지풍 컨텐츠 확장으로 추가된 필드 ─────────────────────────────
    // 기존 행 초기화 구문({ id, type, ..., healAmount })이 그대로 컴파일되도록 전부
    // 끝에 붙이고 기본값을 둔다.
    ItemEffect     effect               = ItemEffect::eNone;
    uint16_t       effectValue          = 0;   // 효과별 수치(가속 지속 초, 축복 여부 등)
    uint32_t       buyPrice             = 0;   // 상점 판매가(플레이어가 살 때). 0 = 상점에서 팔지 않음
    uint32_t       sellPrice            = 0;   // 상점 매입가(플레이어가 팔 때). 0 = 팔 수 없음
    // 클라이언트 표시 전용 한글 이름(UTF-8). 서버 로직/패킷은 name(ASCII)만 쓴다 —
    // name[ITEM_NAME_SIZE]에 한글을 넣으면 20바이트 제한에 걸리는 이름이 많아서다.
    const char*    displayName          = "";
    uint8_t        iconId               = 0;   // 클라이언트 아이콘 모양 번호

    // ── V2 장비 확장(부위 · 방어 · 단계 · 세트 · 직업 제한) ─────────────────────
    // 예전 행은 전부 기본값이어도 뜻이 같다(무기 · 방어 0 · 제한 없음).
    EquipSlot      equipSlot            = EquipSlot::None;   // 장비인데 None이면 무기(예전 행)
    WeaponKind     weaponKind           = WeaponKind::None;
    uint16_t       armorBonus           = 0;   // 방어력
    uint16_t       magicArmorBonus      = 0;   // 마법 방어력
    uint8_t        tier                 = 1;   // 1~5: 옵션 수치 단계 · 드롭 구간
    uint8_t        setId                = 0;   // Data/ItemRules.h 세트 번호(0 = 없음)
    uint8_t        classMask            = 0;   // 착용 가능 직업 비트(1 << ClassId), 0 = 모두
    uint8_t        reqLevel             = 0;   // 착용 레벨

    // ── 경제 1차(단 · 얻는 길 · 귀속) — 끝에만 붙인다(위 순서의 집합 초기화가 그대로 컴파일되게) ──
    uint8_t        rank                 = 0;   // 단: 0 없음(장비 아님) · 1 하 · 2 중 · 3 상
    uint8_t        route                = 0;   // ItemRoute 비트(EconRule::HasRoute). 장비가 아니면 0
    ItemBind       bind                 = ItemBind::eNone;
};
