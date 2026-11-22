#pragma once

#include "Content/Inventory/Item/Item.h"

class CUser;

// 필드(바닥)에 떨어진 아이템의 생성/습득/소멸을 다룬다. 별도의 "필드 아이템"
// 클래스는 없다 — CItem 인스턴스가 CGameObjectManager 등록 여부만 바뀌며 인벤토리와
// 필드를 오간다(Item.h 상단 주석 참고).
namespace CItemHelper
{
    // item(CInventory::TryExtractItem 등으로 이미 어느 CInventory에도 속하지 않게 된
    // 상태여야 함, 즉 IsOnGround()==true)을 (x, y) 위치에 배치한다. CGameObjectManager
    // 등록, CSector 배치, 주변 알림, 30초 뒤 자동 소멸 예약까지 전부 이 함수가 한다.
    // 반드시 이 item을 소유할(=그 Zone의) 스레드 위에서 호출해야 한다 — 호출자가
    // 이미 자기 Zone 스레드 위에서 실행 중이라는 전제(Route::Dispatch 경유)로 설계됨.
    //
    // lootPriorityOwner를 넘기면(기본값 ObjID::npos = 즉시 전체 공개) 줍기 우선권이 세 단계로 걸린다
    // (시트 PartyConst — 자신 > 파티원 > 누구나): loot_owner_ms까지 그 플레이어만, lootPartyId(떨어질 때 주인의
    // 파티, 0 = 파티 없음)가 있으면 loot_party_ms까지 주인 + 그 파티원, 그 뒤 누구나 — 몬스터 전리품이 막타의 가방에
    // 다 들어가지 못해 넘친 것이 이 경로를 쓴다(주인 = 막타, CRewardHelper::HandleKill). 우선권이 걸린 물건 = 몬스터
    // 전리품(주우면 희귀도 알림). 플레이어가 직접 버린 아이템은 처음부터 전체 공개이므로 이 인자를 생략한다.
    // lootOwnerPlayerId = 주인의 계정(playerId) — 주인이 다시 접속해 ObjID가 바뀌어도 주인으로 알아본다(CanLoot).
    void SpawnFieldItem(const CItem::SharedPtr& item, short x, short y,
        const ObjID& lootPriorityOwner = ObjID::npos, UInt32 lootPartyId = 0, Int32 lootOwnerPlayerId = 0);

    // 몬스터 처치 드롭을 놓을 칸 — 죽은 칸부터 시작해 주변으로 퍼뜨린다(한 칸에 여러 개가 겹치면 줍기 대상이 가려지므로).
    // 같은 Zone, 막히지 않은 칸, 안전지대 밖만 쓴다. index번째 칸이 없으면 false.
    [[nodiscard]] bool PickDropTile(short originX, short originY, int index, short& outX, short& outY);

    // 지금(now = GetNowTime()) player가 이 필드 아이템을 주울 수 있는가 — 줍기 우선권 세 단계(Item.h SetLootPriority).
    [[nodiscard]] bool CanLoot(const CItem& item, const CUser& player, UInt32 now) noexcept;

    // player가 지금 서 있는 칸(GetX()/GetY())에 필드 아이템이 있으면 주워서
    // player의 인벤토리에 넣는다. 결과(성공/실패/인벤토리 꽉 참)는 이 함수 안에서
    // 전부 패킷으로 통지하므로 반환값은 없다. 그 아이템에 아직 유효한 루팅
    // 우선권이 걸려 있고 player가 그 보유자가 아니면, "아무 것도 없음"과 동일하게
    // 취급한다(별도 실패 사유를 알리지 않는다 — 남의 우선권인지 애초에 빈 칸인지
    // 클라이언트가 구분할 필요가 없다고 보고 단순하게 갔다).
    //
    // 중요(동시 습득 방지): 이 함수는 "칸에 있는 아이템을 찾는다 -> 인벤토리에
    // 넣어본다 -> 성공했을 때만 필드에서 지운다" 순서로, 도중에 다른 스레드가
    // 끼어들 수 있는 지점(await/enqueue 등)이 전혀 없다. 같은 칸에 두 플레이어가
    // 서 있어도 그 둘은 반드시 같은 Zone(=같은 스레드)에 속하므로, 두 번째
    // 요청은 첫 번째 요청이 완전히 끝난 뒤에야 처리된다 — 그래서 같은 아이템을
    // 두 명이 동시에 집는 상황 자체가 발생할 수 없다(복사 불가능).
    void TryPickupAt(const UserSharedPtr& player);

    // CTimerThread가 30초 뒤 호출하는 소멸 콜백. 그 사이에 이미 주워졌다면(더
    // 이상 CGameObjectManager에 없다면) 조용히 아무 것도 하지 않는다.
    void HandleDespawn(const ObjID& itemId);
}
