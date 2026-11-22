#include "pch.h"
#include "Content/Field/Zone.h"
#include "Base/ServerGlobal.h"
#include "Entity/Subject/Monster/MonsterHelper.h"
#include "Content/Npc/NpcHelper.h"
#include "Content/Party/PartyHelper.h"
#include "Content/Transfer/TransferHelper.h"

CZone::CZone(ZoneId id)
    : m_Id(id)
    , m_Sector(
        static_cast<short>(CZoneLayout::GetZoneCoordById(id).x * CZoneLayout::SectorsPerZoneX),
        static_cast<short>(CZoneLayout::GetZoneCoordById(id).y * CZoneLayout::SectorsPerZoneY))
{
}

void CZone::Enqueue(Task task)
{
    m_LogicThread.Enqueue(std::move(task));
}

void CZone::Run()
{
    // 이 Zone 스레드의 TLS GSector를 자신의 CSector로 설정한다.
    // 이후 이 스레드에서 실행되는 모든 게임 로직은 GSector를 통해 Zone 전용 섹터에 접근한다.
    GSector = &m_Sector;
    // CZoneManager::IsCurrentThreadOwner()가 이 스레드를 자신의 ZoneId로 식별할 수 있도록 설정.
    LCurrentZoneId = m_Id;
    // 몬스터를 넣으며 거는 예약도 이 Zone 스레드가 직접 돌리게 먼저 적어 둔다
    m_LogicThread.BindToCurrentThread();

    // GSector 설정 이후 이 존 담당 몬스터를 초기화한다.
    // GetRandomPosition이 GSector 범위 안에서만 좌표를 뽑으므로
    // 메인 스레드(GSector=nullptr)에서 호출하면 크래시가 발생했던 문제를 해결한다.
    CMonsterHelper::InitForZone(m_Id);
    // 마을 Zone이면 NPC 배회 틱을 이 스레드에서 건다(이후 틱도 이 Zone으로 온다).
    CNpcHelper::InitForZone(static_cast<UInt16>(m_Id));
    // 파티원 상태 모으기 틱(Zone마다 하나 — 이 스레드가 직접 돌린다)
    CPartyHelper::InitForZone(static_cast<UInt16>(m_Id));
    CTransferHelper::InitForZone();

    m_LogicThread.Run();
}
