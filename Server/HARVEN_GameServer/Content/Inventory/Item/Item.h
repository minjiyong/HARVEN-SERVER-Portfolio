#pragma once

#include "Entity/Subject/Subject.h"
#include "Database/GameData/Item/ItemTableRow.h"

// 인벤토리 슬롯에 담기는 아이템 인스턴스. 필드(바닥)에 떨어진 아이템도 같은
// CItem 인스턴스가 그대로 옮겨가서 표현한다 — 별도의 "필드 아이템" 클래스는
// 만들지 않는다(ItemHelper.cpp 참고).
//
// CSubject를 상속해 이름/ObjID/위치 등 기존 필드를 재사용한다. 기본적으로는
// CGameObjectManager에 등록하지 않는다 — CInventory가 shared_ptr로 직접 들고
// 있는 것 말고는 어디서도 전역으로 조회되지 않는다. (장착/탈착/교체/소비처럼
// 인벤토리 안에서만 일어나는 동작은 최대 4만 유저 x 50슬롯(MAX_INVENTORY_SLOTS) 규모라 전역 맵
// insert/erase 락을 타면 비용이 크다.)
//
// 예외: GetOwnerID()가 비어있으면(ObjID::npos) 그 아이템은 지금 어느
// CInventory에도 속하지 않은 "필드에 떨어진 상태"다. 이 경우에 한해
// CItemHelper::SpawnFieldItem이 CGameObjectManager에 등록하고 CSector에 배치한다
// — 필드 아이템은 동시에 존재하는 개수가 훨씬 적고(전체 인벤토리 아이템 수와
// 무관), 다른 플레이어에게 "보여야" 하므로 전역 조회가 꼭 필요하다.
//
// InitInstance()는 CSubject::InitInstance()를 그대로 쓰지 않고 CGameObject의
// 것만 호출한다 — CSubject::InitInstance()는 호출될 때마다 CStat을 새로 힙
// 할당하는데(Subject.cpp), 아이템에는 HP/레벨/경험치 개념이 없어 그 자체가
// 낭비다. 그래서 CItem에는 유효한 CStat이 없다 — 앞으로도 CItem에 GetStat()을
// 호출하는 코드가 생겨서는 안 되고, 만약 생긴다면 nullptr 역참조로 바로
// 드러나도록 의도적으로 방치했다.
class CItem : public CSubject
{
public:
    using SharedPtr = ItemSharedPtr;
    using WeakPtr   = ItemWeakPtr;

protected:
    // 아이템은 MakeNewItem이 장비(CEquipmentItem) · 소비(CConsumableItem) 둘 중 하나로만 만든다 —
    // 맨 CItem은 만들 수 없게 막아 두어야 IsEquipment()가 아니면 곧 소비 아이템이라는 게 성립한다.
    CItem() = default;
    explicit CItem(bool equipment) noexcept : m_bEquipment(equipment) {}

public:
    virtual void InitInstance() override;

    // 장비인가(아니면 소비 아이템). dynamic_pointer_cast는 형 정보(RTTI)를 훑고 참조 수를 원자적으로
    // 올렸다 내리는데, 인벤토리 · 외형 패킷이 칸마다 그걸 하던 게 부하 때 서버 명령의 6~10%였다 —
    // 만들 때 정해지는 형 태그 하나로 가린다(AsEquipment · AsConsumable).
    [[nodiscard]] bool IsEquipment() const noexcept { return m_bEquipment; }

    [[nodiscard]] ItemTableId GetItemTableId() const noexcept { return m_ItemTableId; }
    void SetItemTableId(ItemTableId id) noexcept { m_ItemTableId = id; }

    [[nodiscard]] UInt16 GetCount() const noexcept { return m_Count; }
    void SetCount(UInt16 count) noexcept { m_Count = count; }
    void AddCount(UInt16 amount) noexcept { m_Count += amount; }
    // amount가 현재 수량보다 많으면 0으로 만든다 — 호출자(CInventory)가 이미
    // 수량을 검증한 뒤에만 부르는 걸 전제하지만, 언더플로 자체는 여기서 막는다.
    void RemoveCount(UInt16 amount) noexcept { m_Count = (amount >= m_Count) ? 0 : (m_Count - amount); }

    // GetOwnerID()가 비어있는지(=필드에 떨어진 상태인지)를 매번 ObjID::npos와
    // 직접 비교하지 않도록 이름 붙인 것뿐 — 별도 필드를 두지 않는다.
    [[nodiscard]] bool IsOnGround() const noexcept { return GetOwnerID() == ObjID::npos; }

    // 몬스터 처치 드롭 전용: 일정 시간 동안 처치자만 주울 수 있게 하는 "루팅
    // 우선권" 보유자. GetOwnerID()(=인벤토리 소속 여부)와는 완전히 별개 개념이라
    // 섞어 쓰지 않는다 — 필드에 떨어진 상태(OwnerID 비어있음)에서도 루팅 우선권은
    // 따로 걸려 있을 수 있다. 비어있으면(ObjID::npos) 지금 누구나 주울 수 있는
    // 상태 — 플레이어가 직접 버린 아이템은 처음부터 이 값이 비어있고, 몬스터
    // 드롭만 CItemHelper::SpawnFieldItem이 잠시 채워뒀다가 타이머로 풀어준다.
    [[nodiscard]] const ObjID& GetLootPriorityOwner() const noexcept { return m_LootPriorityOwner; }
    void SetLootPriorityOwner(const ObjID& owner) noexcept { m_LootPriorityOwner = owner; }

    // 줍기 우선권의 세 단계(자신 > 파티원 > 누구나, 시트 PartyConst loot_owner_ms · loot_party_ms):
    //   GetNowTime() < m_LootOwnerUntil  → 주인만
    //   GetNowTime() < m_LootPartyUntil  → 주인 + 떨어질 때 주인의 파티(m_LootPartyId)
    //   그 뒤                            → 누구나(CItemHelper::CanLoot). 타이머 없이 주울 때 시각으로 판정한다.
    // ownerPlayerId: 주인의 계정(playerId — 0 = 모름). ObjID는 접속마다 새로 받으므로(세션 번호) 주인이 다시 접속해도
    // 자기 물건임을 알아보려면 계정으로도 비교해야 한다 — 특히 귀속 넘침은 주인만 줍는다(CItemHelper::CanLoot).
    void SetLootPriority(const ObjID& owner, UInt32 partyId, UInt32 ownerUntil, UInt32 partyUntil, Int32 ownerPlayerId = 0) noexcept
    {
        m_LootPriorityOwner = owner;
        m_LootPartyId = partyId;
        m_LootOwnerUntil = ownerUntil;
        m_LootPartyUntil = partyUntil;
        m_LootOwnerPlayerId = ownerPlayerId;
    }
    [[nodiscard]] Int32 GetLootOwnerPlayerId() const noexcept { return m_LootOwnerPlayerId; }
    [[nodiscard]] UInt32 GetLootPartyId() const noexcept { return m_LootPartyId; }
    [[nodiscard]] UInt32 GetLootOwnerUntil() const noexcept { return m_LootOwnerUntil; }
    [[nodiscard]] UInt32 GetLootPartyUntil() const noexcept { return m_LootPartyUntil; }

private:
    ItemTableId m_ItemTableId = ITEM_TABLE_ID_NONE;
    UInt16      m_Count       = 0;
    bool        m_bEquipment  = false;
    ObjID       m_LootPriorityOwner = ObjID::npos;
    UInt32      m_LootPartyId       = 0;
    UInt32      m_LootOwnerUntil    = 0;
    UInt32      m_LootPartyUntil    = 0;
    Int32       m_LootOwnerPlayerId = 0;
};

// itemId가 ItemTable에 없으면 nullptr. ItemTableRow::type을 보고 CEquipmentItem/
// CConsumableItem 중 알맞은 파생 클래스를 만든다 — Subject.cpp의 MakeNewSubject()와
// 같은 패턴(카테고리를 보고 구체 타입을 결정하는 팩토리).
[[nodiscard]] CItem::SharedPtr MakeNewItem(ItemTableId itemId, UInt16 count);
