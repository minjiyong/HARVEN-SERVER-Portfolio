#pragma once

#include <memory>
#include <array>
#include <bitset>
#include <unordered_map>
#include <vector>

#include "Content/Inventory/Item/Item.h"

class CUser;

// 유저 한 명의 인벤토리.
//
// 항상 소유자(CUser)를 담당하는 Zone 스레드 위에서만 mutate된다는 전제로
// 락/atomic 없이 설계했다. WorkerThread.cpp가 패킷을 처리하기 전에 이미
// GZoneManager->EnqueueBySession()으로 소유 Zone 스레드에 진입시키므로,
// 이 전제는 정상적인 호출 경로에서는 항상 성립한다.
// 그 전제가 깨지는 경우(다른 스레드에서 실수로 직접 호출)를 대비해,
// mutating 메서드들은 시작하자마자 AssertOwnedByCurrentZone()으로 위반을
// 즉시 크래시로 드러낸다 — 소유자 생존 여부는 Release에서도 항상 확인하고,
// Zone 스레드 일치 여부는 Debug 빌드에서만 확인한다(Release에서는 제거됨).
//
// CUser -> CInventory는 shared_ptr(강한 소유), CInventory -> CUser는 weak_ptr(약한 참조)다.
// 서로를 강하게 붙잡으면 순환 참조로 둘 다 해제되지 않으므로, 소유 방향만 강하게 둔다.
//
// 슬롯에는 ItemSlot 같은 POD가 아니라 실제 CItem(CEquipmentItem/CConsumableItem)
// 인스턴스를 담는다 — 장착 여부 같은 아이템별 상태를 CItem 쪽에 캡슐화하기 위함.
//
// 모든 mutating 메서드는 성공하면 영향받은 슬롯을 그 자리에서 즉시(디바운스
// 없이) DB에 반영한다 — SaveSlotToDB() 참고.
class CInventory
{
public:
    using SharedPtr = std::shared_ptr<CInventory>;
    using WeakPtr   = std::weak_ptr<CInventory>;

public:
    // CUser::InitInstance()에서 1회 호출해 소유자를 등록한다.
    void Attach(const UserSharedPtr& owner) noexcept;

    // 로그인 시 DB에서 읽어온 행을 그대로 슬롯에 복원한다. 이미 DB가 원본인
    // 데이터를 되돌려놓는 것뿐이므로, 다른 mutating 메서드와 달리 Zone 스레드
    // 검증도 DB 재저장(SaveSlotToDB)도 하지 않는다 — 그리고 호출 시점의 CInventory는
    // 아직 CGameObjectManager에 공개되기 전(CUser::InitInstance() 직후)이라 다른
    // 스레드가 동시에 접근할 수 없다는 게 이 메서드를 쓸 수 있는 전제 조건이다.
    void LoadFromDB(const std::vector<DB_ITEM_INFO>& items) noexcept;

    // itemId가 ItemTable에 없거나, count가 0이거나, 자리가 부족하면 아무것도
    // 바꾸지 않고 false를 반환한다 — 부분 반영 없이 전부 성공하거나 전부 실패한다.
    // outTouchedSlots가 non-null이면 성공 시 실제로 채워지거나 수량이 늘어난
    // 슬롯 인덱스들을 담아준다(스택이 여러 슬롯에 걸쳐 나뉘어 채워지면 2개 이상일
    // 수 있다) — 호출자가 어떤 슬롯이 바뀌었는지 알아야(예: 클라이언트에 알림)
    // 할 때 GetSlots() 전체를 다시 비교하지 않도록 하기 위함.
    // enchant/extra는 장비에만 의미가 있다(강화 수치 · V2 희귀도·옵션 — 바닥에서 다시 주울 때 유지).
    [[nodiscard]] bool TryAddItem(ItemTableId itemId, UInt16 count, std::vector<UInt16>* outTouchedSlots = nullptr, UInt32 enchant = 0, UInt64 extra = 0) noexcept;
    // TryAddItem의 1단계(드라이런)만 — 지금 count 전부가 들어가는가(맵 · DB를 건드리지 않는다). 같은 코드를 쓴다:
    // true면 같은 스레드에서 그 사이 칸이 바뀌지 않는 한 TryAddItem도 성공한다(업적 받기가 받음 저장을 먼저 넣고 지급하려고 쓴다).
    [[nodiscard]] bool CanAddItem(ItemTableId itemId, UInt16 count) const noexcept;

    // slotIndex에 아이템이 없거나, count보다 적게 들어있으면 false.
    [[nodiscard]] bool TryRemoveItem(UInt16 slotIndex, UInt16 count) noexcept;

    // slotIndex에서 count만큼 꺼내 별도 CItem으로 분리한다(필드에 버리는 용도).
    // 실패(슬롯 없음/count 부족/count=0)하면 nullptr — 인벤토리는 전혀 바뀌지
    // 않는다. 반환된 CItem은 이제 어떤 CInventory에도 속하지 않는다(OwnerID가
    // 비어 있음, CItem::IsOnGround() 참고) — 호출자가 필드에 놓을지 결정한다.
    [[nodiscard]] CItem::SharedPtr TryExtractItem(UInt16 slotIndex, UInt16 count) noexcept;

    // slotIndex가 비어있거나, 장비 아이템이 아니거나, 이미 장착 중이면 false.
    // V2: 장비는 부위(EquipSlot)마다 하나씩 장착된다 — 아래의 "이전 장비"는 같은 부위의 장비다.
    // 직업 전용 · 착용 레벨도 검사한다.
    // 이 인벤토리에 이미 장착 중이던 "다른" 장비가 있었다면, 그 장비를 탈착하는
    // 것과 새 장비를 장착하는 것이 하나의 원자적 동작으로 함께 일어난다(둘 다
    // 장착된 상태나 둘 다 미장착인 상태가 잠깐이라도 관측되지 않는다 — 메모리
    // 상태는 이 함수 안에서 한 번에 바뀌고, DB 반영도 SaveTwoSlotsToDB로 한
    // 트랜잭션에 묶는다). outPreviousSlot이 non-null이고 그런 이전 장비가
    // 있었다면, 그 슬롯 인덱스를 채워준다(호출자가 클라이언트에 별도로 알릴 수
    // 있도록) — 없었다면 건드리지 않으므로, 호출 전에 MAX_INVENTORY_SLOTS 같은
    // "슬롯일 수 없는 값"으로 초기화해두고 바뀌었는지 확인해야 한다.
    [[nodiscard]] bool TryEquip(UInt16 slotIndex, UInt16* outPreviousSlot = nullptr) noexcept;

    // slotIndex가 비어있거나, 장비 아이템이 아니거나, 장착 중이 아니면 false.
    [[nodiscard]] bool TryUnequip(UInt16 slotIndex) noexcept;

    // slotIndex가 비어있거나 소비 아이템(CConsumableItem)이 아니면 false. 성공하면
    // 1개를 소모하고(마지막 1개였다면 슬롯이 비워짐) ItemTableRow::healAmount만큼
    // 소유자 HP를 회복시킨다(HealHp가 PLAYER_MAX_HP로 알아서 clamp함). 쿨타임
    // 검사는 여기서 하지 않는다 — 다른 액션들(공격/스킬)과 같은 이유로 Route.cpp가
    // CUser::m_LastPotionUseTime을 보고 이미 걸러낸 뒤에만 이 함수를 부른다.
    [[nodiscard]] bool TryUseItem(UInt16 slotIndex) noexcept;

    // 두 슬롯의 내용을 맞바꾼다(한쪽이 비어있으면 이동). 둘 다 비어있거나
    // 같은 인덱스면 false.
    //
    // 다른 메서드들은 "없는 슬롯을 가리키면 조회에 실패해서 알아서 false"가 되지만,
    // 이 메서드만은 m_Slots[slotIndexB] 형태로 없던 키를 새로 만들 수 있다 —
    // 그래서 범위 밖 인덱스를 그대로 받으면 아이템이 MAX_INVENTORY_SLOTS 밖의
    // "화면에도 안 보이고 다시 꺼낼 수도 없는" 슬롯으로 사라진다(DB에도 그대로
    // 저장되어 재접속해도 복구되지 않는다). IsValidSlotIndex()로 반드시 막는다.
    [[nodiscard]] bool TrySwapSlots(UInt16 slotIndexA, UInt16 slotIndexB) noexcept;

    // 슬롯 인덱스 -> CItem. 빈 슬롯은 키 자체가 존재하지 않는다(부재 = 빈 슬롯).
    [[nodiscard]] const std::unordered_map<UInt16, CItem::SharedPtr>& GetSlots() const noexcept { return m_Slots; }

    // 지금 장착 중인 "무기"의 ItemTableId(없으면 ITEM_TABLE_ID_NONE). TryEquip이
    // "새로 장착하면 같은 부위의 이전 장비는 자동으로 탈착"을 원자적으로 보장하므로
    // 부위마다 0개 또는 1개만 장착 상태다.
    [[nodiscard]] ItemTableId GetEquippedItemId() const noexcept;
    // 그 부위에 장착 중인 슬롯(없으면 MAX_INVENTORY_SLOTS). 착용 칸 목록(보통 0~8개)만 본다.
    [[nodiscard]] UInt16 FindEquipped(EquipSlot slot) const noexcept;
    // 전 부위를 한 번에 — [부위] = 장착 슬롯(없으면 MAX_INVENTORY_SLOTS). FindEquipped를 부위마다 부른 것과 같다.
    using EquippedParts = std::array<UInt16, static_cast<size_t>(EquipSlot::Count)>;
    [[nodiscard]] EquippedParts GetEquippedParts() const noexcept;
    // 슬롯의 V2 인스턴스 값(희귀도·옵션). 비었거나 장비가 아니면 0.
    [[nodiscard]] UInt64 GetSlotExtra(UInt16 slotIndex) const noexcept;
    // 장착 중인 무기의 강화 수치(장착한 게 없으면 0)
    [[nodiscard]] UInt32 GetEquippedEnchant() const noexcept;

    // 소유자의 전투 능력치(공격력 포함)를 CUser::RecomputeStats(직업 · 능력치 · 전 부위
    // 장비 · 강화)로 다시 계산해 반영한다. 장착 상태/강화 수치/레벨이 바뀌는 모든
    // 경로 끝에서 호출한다 — "언제 호출을 빠뜨렸는지" 따지는 대신, 항상 최종 상태를
    // 그대로 다시 계산하므로 항상 정확하다. (레벨업 경로에서도 불러야 해서 public)
    void RecomputeOwnerStats() const;
    // V2: 장착 장비(전 부위)의 능력치 보너스 · 특수 효과를 모은다(CUser::RecomputeStats가 부른다).
    void CollectGearBonus(struct GearBonus& out, struct GearExtras* outExtras = nullptr) const;

    // V2: 소지품 정리 — 같은 물건 스택을 합치고, 착용 중 → 장비(부위·희귀도 순) → 소모품 순으로
    // 슬롯을 다시 놓는다. 모든 이동은 두 칸짜리 교환(한 DB 트랜잭션)의 연속이라 도중에 서버가
    // 죽어도 아이템이 사라지거나 복제되지 않는다. 바뀐 칸 수를 돌려준다.
    int TrySort() noexcept;

    // ── 리니지풍 컨텐츠 확장 ────────────────────────────────────────────────
    // 지갑(그로시). 리니지에서는 그로시도 인벤토리 안의 물건이라, 슬롯과 같은 곳에서
    // 관리해야 "그로시 차감 + 슬롯 지급"을 한 번의 원자적 동작으로 묶을 수 있다.
    [[nodiscard]] UInt32 GetAdena() const noexcept { return m_Adena; }

    // 로그인 시 DB 값 복원(LoadFromDB와 같은 전제 — 아직 공개 전이라 검증/저장 없음).
    void LoadAdena(UInt32 adena) noexcept { m_Adena = adena; }

    // 줍기 등으로 그로시 증가. 상한(DB INT 범위)을 넘는 만큼은 버려진다.
    // 실제로 늘어난 양을 반환하고, 즉시 DB 저장 + ADENA_INF를 보낸다.
    UInt32 AddAdena(UInt32 amount, AdenaReason reason) noexcept;

    // 그로시를 쓴다(텔레포트 요금 등 물건을 받지 않는 지출). 모자라면 false — 아무것도 바뀌지 않는다.
    [[nodiscard]] bool TrySpendAdena(UInt32 amount, AdenaReason reason) noexcept;

    // 소비 아이템 하나를 소모한다(두루마리/바람걸음 물약처럼 효과가 인벤토리 밖에서
    // 일어나는 아이템용). slotIndex가 비었거나, 소비 아이템이 아니거나, 효과가
    // expected와 다르면 false — 아무것도 바뀌지 않는다.
    [[nodiscard]] bool TryConsumeOne(UInt16 slotIndex, ItemEffect expected, ItemTableId* outItemId = nullptr) noexcept;

    struct EnchantOutcome
    {
        EnchantResult result        = EnchantResult::Invalid;
        ItemTableId   itemId        = ITEM_TABLE_ID_NONE;
        UInt32       before        = 0;
        UInt32       after         = 0;
        bool          wasEquipped   = false;  // 증발/변화한 무기가 장착 중이었나(외형 브로드캐스트 필요)
    };


    // ── 대장간 트랜잭션(강화 · 제작 · 분해) ─────────────────────────────────────
    // 준비(메모리 변경 없이 before/after 계산)에서 건드릴 칸을 모두 잠그고, 한 DB 트랜잭션의 커밋을 확인한 뒤 소유 Zone에서 완료한다.
    // 한 사람에게 동시에 하나만(IsBusy) — 강화 · 제작은 서로를 Busy로 보고, 장착 · 정리는 진행 중이면 거부,
    // 자리 바꿈 · 버림 · 팔기 · 사용 · 경매 등록 · 정산 받기는 잠긴 칸이면 거부, 물건 넣기(TryAddItem · FindSingleSlotFor)는 잠긴 칸을 건너뛴다.
    // DB가 실패하면 잠금을 풀지 않는다(커밋 응답을 잃었을 수 있다 — 호출자가 연결을 닫는다. 다시 들어오면 DB가 원본).
    enum class PendingTx : UInt8 { None, Enchant, Craft, Transfer };
    [[nodiscard]] bool IsBusy() const noexcept { return m_PendingTx != PendingTx::None; }
    [[nodiscard]] PendingTx GetPendingTx() const noexcept { return m_PendingTx; }
    [[nodiscard]] bool IsTransferCommitting() const noexcept { return m_TransferCommitting; }
    bool BeginTransfer() noexcept;
    void SetTransferSlots(const std::vector<UInt16>& slots) noexcept;
    void FreezeTransfer(UInt32 adenaAfter) noexcept;
    void FinishTransfer(const DB_ITEM_BATCH_SAVE* committed) noexcept;
    [[nodiscard]] bool IsSlotLocked(UInt16 slot) const noexcept { return slot < MAX_INVENTORY_SLOTS && m_LockedSlots.test(slot); }

    struct EnchantPlan { EnchantOutcome outcome; DB_ENCHANT_SAVE save; };
    // 슬롯은 커밋 확인까지 예약한다. DB 실패 시 메모리/재료는 변경하지 않는다.
    [[nodiscard]] EnchantPlan PrepareEnchant(UInt16 ticketSlot, UInt16 targetSlot, UInt16 protectionSlot, UInt64 roll);
    void CompleteEnchant(const EnchantPlan& plan, bool committed);

    // 여러 칸 + 그로시를 한 트랜잭션으로(제작 · 분해 — DB_ITEM_BATCH_SAVE, 프로시저 P_ITEM_BATCH_TRADE).
    // 시작: 바쁘거나 계획이 지금 칸 · 그로시와 다르면 false(아무것도 바뀌지 않는다). 계획의 모든 칸을 잠그고
    // 그로시 차이(adenaBefore - adenaAfter)를 메모리에서 예약한다(저장 없음 — 프로시저가 절대값을 저장한다. 그 사이 다른 지출은 예약 뒤 값을 본다).
    [[nodiscard]] bool BeginBatch(const DB_ITEM_BATCH_SAVE& save) noexcept;
    // 완료(소유 Zone): committed면 칸마다 after를 메모리에 반영 · 잠금 풀기 · 칸 알림 · ADENA_INF(-수수료, reason).
    // 실패면 예약한 그로시를 되돌려 저장하고 칸은 잠근 채 둔다(강화와 같음 — 호출자가 연결을 닫는다).
    void CompleteBatch(const DB_ITEM_BATCH_SAVE& save, bool committed, AdenaReason reason) noexcept;
    [[nodiscard]] UInt32 GetSlotEnchant(UInt16 slot) const noexcept;
    void LoadEnchantGrace(const std::vector<DB_ENCHANT_GRACE>& rows);
    [[nodiscard]] UInt16 GetGraceFailures(UInt16 itemId, UInt32 currentLevel) const noexcept;
    [[nodiscard]] const auto& GetEnchantGrace() const noexcept { return m_EnchantGrace; }

    // 상점 구매: 그로시 차감 + 슬롯 하나에 지급. 수량 전체가 "한 슬롯"에 들어갈 때만
    // 성공한다(기존 스택에 합치거나 빈 슬롯 하나) — 그래야 DB 반영이 "그로시 + 슬롯 1개"
    // 한 트랜잭션(ShopTransaction 프로시저)으로 끝난다. outSlot에 지급된 슬롯을 담아준다.
    [[nodiscard]] ShopResult TryBuy(ItemTableId itemId, UInt16 count, UInt32 totalPrice, UInt16* outSlot) noexcept;

    // 상점 판매: 슬롯에서 count만큼 빼고 그로시를 준다. 장착 중인 장비는 팔 수 없다.
    [[nodiscard]] ShopResult TrySell(UInt16 slotIndex, UInt16 count, UInt32* outGain) noexcept;

    // 악명 사망 시 떨어뜨릴 아이템을 하나 골라 꺼낸다(roll은 호출자가 넘기는 난수).
    // 비어 있으면 nullptr. 꺼낸 아이템은 어느 인벤토리에도 속하지 않는다(TryExtractItem과 같음).
    [[nodiscard]] CItem::SharedPtr TryTakeRandomItem(UInt32 roll) noexcept;

    // slotIndex의 현재 상태를 확장 슬롯 포맷으로 채운다(빈 슬롯이면 itemId=0).
    void FillSlotState(UInt16 slotIndex, ITEM_SLOT_STATE_DATA& out) const noexcept;

    // 슬롯의 현재 상태를 DB 저장 단위로 바꾼다(SaveTwoSlotsToDB · 상점 거래 · 제작 계획 공용). 빈 칸이면 hasItem=false.
    [[nodiscard]] DB_ITEM_SLOT_SAVE DescribeSlot(UInt16 slotIndex) const;

    // ── Phase 5 결사 · 경매장 — 메모리만 바꾸고 DB는 호출자가 한 트랜잭션으로 ──
    // 아래 함수들은 따로 DB에 저장하지 않는다: 세우기 · 등록 · 구매 · 받기의 프로시저가 그로시(절대값)와 슬롯을 함께 커밋한다.
    // 그 트랜잭션이 실패하면 Return*/Undo*가 메모리를 되돌리고 그때는 저장한다(그 사이 다른 절대값 저장이 예약분을 담았을 수 있다).
    [[nodiscard]] bool ReserveAdena(UInt32 amount, AdenaReason reason) noexcept;        // 모자라면 false
    void ReturnAdena(UInt32 amount, AdenaReason reason) noexcept;                      // 되돌리고 저장
    void CreditAdenaNoSave(UInt32 amount, AdenaReason reason) noexcept;                // 정산 받기(프로시저가 저장)
    struct TakenItem { UInt16 itemId = 0; UInt16 count = 0; UInt32 enchant = 0; UInt64 extra = 0; };
    enum class TakeResult : UInt8 { Ok, NoItem, Equipped, BadCount, NotListable, Bound };   // Bound: 귀속(ItemRules::IsBound)
    // 경매 등록: 슬롯에서 count만큼 뺀다(착용 중 · 그로시 · 없는 칸은 안 된다). remain = 뺀 뒤 그 슬롯(DB 저장 단위)
    [[nodiscard]] TakeResult TakeForListing(UInt16 slotIndex, UInt16 count, TakenItem& out, DB_ITEM_SLOT_SAVE& remain) noexcept;
    void RestoreTaken(UInt16 slotIndex, const TakenItem& item) noexcept;                // 등록 트랜잭션 실패 — 되돌리고 저장
    // 정산 받기: 물건 하나가 한 칸에 통째로 들어갈 자리(없으면 MAX_INVENTORY_SLOTS)
    [[nodiscard]] UInt16 FindSlotForIncoming(UInt16 itemId, UInt16 count) const noexcept;
    [[nodiscard]] bool PlaceIncoming(UInt16 slotIndex, const TakenItem& item, DB_ITEM_SLOT_SAVE& out) noexcept;
    void UndoIncoming(UInt16 slotIndex, const TakenItem& item) noexcept;               // 받기 트랜잭션 실패 — 빼고 저장

private:
    // 슬롯 인덱스는 전부 클라이언트 패킷에서 그대로 넘어온 값이므로 절대 신뢰하지
    // 않는다. 검증을 Route.cpp가 아니라 여기 두는 이유: Route에 두면 핸들러마다
    // 같은 검사를 반복해야 하고 하나만 빠뜨려도 그대로 구멍이 되지만, CInventory는
    // 모든 경로가 반드시 지나가는 길목이라 여기서 한 번 막으면 전부 덮인다.
    [[nodiscard]] static constexpr bool IsValidSlotIndex(UInt16 slotIndex) noexcept
    {
        return slotIndex < MAX_INVENTORY_SLOTS;
    }

    // 소유자가 아직 살아있는지(Release 포함 항상 확인) + 지금 이 스레드가 그 소유자의
    // Zone 스레드가 맞는지(Debug 한정)를 한 곳에서 검사한다. mutating 메서드마다
    // 이 두 조건을 따로 챙기다 실수로 하나를 빠뜨리는 일이 없도록 한 곳에 모았다.
    void AssertOwnedByCurrentZone() const noexcept;

    // slotIndex의 현재 상태(있으면 내용, 없으면 빈 슬롯)를 그대로 DB에 반영한다.
    // "무엇이 바뀌었는지"를 따로 추적하지 않고 최종 상태를 그대로 저장/삭제하므로,
    // 호출 시점에 그 슬롯이 어떤 이유로 바뀌었는지는 몰라도 항상 정확하다.
    void SaveSlotToDB(UInt16 slotIndex) const;

    // 두 슬롯의 현재 상태를 하나의 DB 트랜잭션으로 함께 반영한다(SaveSlotToDB를
    // 두 번 따로 부르면 그 사이에 서버가 죽었을 때 한쪽만 반영된 상태가 영구히
    // 남을 수 있는 동작들 — 장착 시 이전 장비 자동 탈착, 슬롯 교체 — 에서 쓴다).
    void SaveTwoSlotsToDB(UInt16 slotIndexA, UInt16 slotIndexB) const;

    // 슬롯이 바뀌었다고 소유자에게 ITEM_SLOT_INF로 알린다. 모든 변경
    // 경로가 이미 SaveSlotToDB/SaveTwoSlotsToDB를 거치므로 그 두 곳에서 함께 부른다 —
    // "저장한 것 = 알린 것"이 항상 같아진다.
    void NotifySlotChanged(UInt16 slotIndex) const;

    // itemId/count를 "한 슬롯"에 넣을 수 있는 자리(기존 스택 우선, 없으면 빈 슬롯).
    // 없으면 MAX_INVENTORY_SLOTS. 맵은 건드리지 않는다.
    [[nodiscard]] UInt16 FindSingleSlotFor(const ItemTableRow& row, UInt16 count) const noexcept;

    // 착용 칸 목록(m_Equipped)을 지금 칸 상태로 다시 만든다. 칸을 넣거나 빼거나 옮긴 곳, 착용 여부를
    // 바꾼 곳마다 "바로 그 자리에서" 부른다 — 그 뒤에 이어지는 능력치 재계산 · 알림이 지금 상태를 보도록.
    // (맵 순회 순서까지 그대로 담으므로 예전처럼 칸 전체를 훑은 결과와 항상 같다 — Debug는 읽을 때마다 대조한다.)
    void RefreshEquipped() noexcept;
    void VerifyEquipped() const noexcept;   // Debug 전용 대조(Release에서는 아무것도 안 한다)

    // 대장간 트랜잭션 잠금(PendingTx) — 칸 번호가 범위 밖이면(보호권 없음 등) 건너뛴다
    void LockSlots(PendingTx kind, std::initializer_list<UInt16> slots) noexcept;
    void UnlockSlots() noexcept;

private:
    UserWeakPtr m_wpOwner;
    std::unordered_map<UInt16, CItem::SharedPtr> m_Slots;
    // 장착 중인 칸 — m_Slots 순회 순서 그대로, 아이템 id와 함께(아이템 id는 아이템이 만들어질 때 정해져 바뀌지 않는다).
    // 부위는 읽을 때 표(ItemTableData::SlotOf)로 가린다 — 개발용 핫 리로드가 부위 값을 바꿔도 예전과 같은 답이 나온다.
    struct EquippedRef { UInt16 slotIndex = MAX_INVENTORY_SLOTS; ItemTableId itemId = ITEM_TABLE_ID_NONE; };
    std::array<EquippedRef, MAX_INVENTORY_SLOTS> m_Equipped{};
    UInt8 m_EquippedCount = 0;
    UInt32 m_Adena = 0;
    PendingTx m_PendingTx = PendingTx::None;
    bool m_TransferCommitting = false;
    UInt32 m_TransferWalletCeiling = 0, m_DeferredAdena = 0;
    std::vector<std::pair<UInt32, AdenaReason>> m_DeferredCredits;
    std::bitset<MAX_INVENTORY_SLOTS> m_LockedSlots;   // 진행 중인 대장간 트랜잭션이 건드리는 칸
    // 캐릭터 소유 데이터: 장비 테이블 ID × 시도 전 단계. 물건 이동/파괴와 독립적이다.
    std::unordered_map<UInt64, DB_ENCHANT_GRACE> m_EnchantGrace;
};
