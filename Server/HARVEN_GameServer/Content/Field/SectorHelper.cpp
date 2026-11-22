#include "pch.h"
#include "Content/Transfer/TransferHelper.h"
#include <bit>
#include "Content/Field/SectorHelper.h"
#include "Entity/Subject/SubjectHelper.h"
#include "User/User.h"
#include "Entity/Subject/Monster/Monster.h"
#include "User/UserHelper.h"
#include "Entity/Subject/Monster/MonsterHelper.h"
#include "Entity/Component/Behavior/EntityBehaviorHelper.h"
#include "Content/Field/Sector.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Field/ZoneManager.h"
#include "Content/Navigator/Collision.h"
#include "Base/Core/CoreTLS.h"
#include "Database/GameData/WorldData.h"
#include "Content/Achieve/AchieveHelper.h"

namespace CSectorHelper
{
    void UpdatePosition(ObjID& subjectId, short nextX, short nextY)
    {
        UpdatePosition(::GetGameObject<CSubject>(subjectId), nextX, nextY);
    }

    void UpdatePosition(const SubjectSharedPtr& object, short nextX, short nextY)
    {
        if (object == nullptr)
            return;
        const ObjID subjectId = object->GetObjID();

        // 섹터 칸 이동과 좌표 갱신을 한 번에 처리해 둘이 어긋난 상태가 남지 않게 한다.
        // CSector 격자는 Zone마다 하나씩이고 thread_local GSector로만 닿으므로,
        // 이 Zone 스레드 외에는 건드리는 쪽이 없어 락이 필요 없다.
        const bool valid = GSector->UpdateObjectSectorAndPosition(object, nextX, nextY);

        if (!valid)
            return;

        const ZoneId zoneId = CZoneLayout::GetZoneIdByWorld(nextX, nextY);

        // 좌표가 바뀌어도 Zone은 대부분 그대로다. 몬스터는 아예 Zone을 벗어날 수
        // 없고(RandomMove가 존 밖 걸음을 거부하고, 어그로 추격은 ZONE_BOUNDARY_MARGIN
        // 안에서 되돌아간다), 플레이어도 500x500칸짜리 Zone을 넘는 일은 드물다.
        // 그런데 UpdateObjectZone은 전역 뮤텍스를 잡으므로, 같은 값을 다시 쓰는
        // 호출 때문에 Zone 스레드 16개가 이동 한 번마다 이 락 하나에서 다시 만난다.
        // 실제로 Zone이 바뀐 경우에만 갱신한다.
        //
        // m_ZoneId를 "맵에 이미 반영된 값"으로 믿어도 되는 이유 — 아래 두 줄을 항상
        // 함께 실행하므로 m_ZoneId는 곧 이 오브젝트에 대해 마지막으로 맵에 쓴 값이다.
        // 맵에서 빠지는 경로(접속 종료, 필드 아이템 습득/소멸)는 CGameObjectManager에서도
        // 함께 지우므로 위 GetGameObject가 nullptr을 반환해 여기까지 오지 못하고,
        // 맵에 새로 들어오는 객체(로그인 CUser, InitForZone의 CMonster, MakeNewItem으로
        // 만든 CItem)는 전부 새로 할당되어 m_ZoneId가 InvalidZoneId다. 따라서
        // "맵에는 없는데 m_ZoneId만 우연히 맞아 갱신을 건너뛰는" 상태가 생기지 않는다.
        if (object->GetZoneId() != zoneId)
        {
            object->SetZoneId(zoneId);
            GZoneManager->UpdateObjectZone(subjectId, zoneId);
        }

        // 세션 쪽은 원자적 저장이라 락이 없다. IO 스레드가 EnqueueBySession으로
        // 읽는 값이므로 조건을 걸지 않고 항상 최신으로 맞춰둔다.
        if (subjectId.GetCategory<EnumCategory>() == EnumCategory::eUser)
        {
            const auto user = static_pointer_cast<CUser>(object);
            if (const auto session = user->GetGameSession(); session != nullptr)
                session->SetRoutingZoneId(zoneId);
        }
    }

    std::pair<short, short> PickRandomPositionInZone(bool excludeSafeZone)
    {
        ASSERT_CRASH(GSector != nullptr);

        // 현재 Zone의 섹터 오프셋에서 월드 좌표 범위를 계산하여
        // 이 Zone 소속 Sector 범위 안에서만 고른다(다른 Zone 좌표가 나오면 섹터 등록이 실패한다).
        const short minX = static_cast<short>(GSector->GetOffsetX() * SECTOR_RANGE);
        const short maxX = static_cast<short>((GSector->GetOffsetX() + CSector::kLocalWidth)  * SECTOR_RANGE - 1);
        const short minY = static_cast<short>(GSector->GetOffsetY() * SECTOR_RANGE);
        const short maxY = static_cast<short>((GSector->GetOffsetY() + CSector::kLocalHeight) * SECTOR_RANGE - 1);

        std::uniform_int_distribution<short> distX(minX, maxX);
        std::uniform_int_distribution<short> distY(minY, maxY);
        while (true)
        {
            const short x = distX(LRng);
            const short y = distY(LRng);
            if (isCollision(x, y))
                continue;
            if (excludeSafeZone && WorldData::IsSafeZone(x, y))
                continue;
            return { x, y };
        }
    }

    void GetRandomPosition(ObjID& subjectId)
    {
        // 몬스터는 마을(안전지대)에 배치하지 않는다.
        const bool isMonster = subjectId.GetCategory<EnumCategory>() == EnumCategory::eMonster;
        const auto [x, y] = PickRandomPositionInZone(isMonster);
        UpdatePosition(subjectId, x, y);
    }

    // 이 몬스터를 볼 수 있는 게임 중 플레이어
    void CollectUsers(const MonsterSharedPtr& monster, std::vector<SubjectSharedPtr>& outUsers)
    {
        if (monster == nullptr)
            return;
        // 플레이어만, 시야(체비셰프 GViewRange = CanSee) 안만 — 칸에 적어 둔 좌표로 걸러 범위 밖은 약한 참조를 풀지 않는다
        GSector->AppendWithin(monster->GetSectorX(), monster->GetSectorY(), monster->GetX(), monster->GetY(), GViewRange, EnumCategory::eUser, outUsers);
        std::erase_if(outUsers, [&](const SubjectSharedPtr& object)
        {
            const auto session = static_cast<CUser*>(object.get())->GetGameSession();
            return !session || session->m_eState != ST_INGAME;
        });
    }

    // 플레이어 시야에 든 객체(자기 자신 · 게임 중이 아닌 플레이어 · 볼 수 없는 것은 뺀다)
    void CollectVisible(const UserSharedPtr& player, std::vector<SubjectSharedPtr>& outVisible)
    {
        if (player == nullptr)
            return;
        const ObjID playerId = player->GetObjID();
        // 시야(체비셰프 GViewRange = CanSee) 안만 — 칸에 적어 둔 좌표로 걸러 범위 밖은 약한 참조를 풀지 않는다
        GSector->AppendWithin(player->GetSectorX(), player->GetSectorY(), player->GetX(), player->GetY(), GViewRange, EnumCategory::eNone, outVisible);
        std::erase_if(outVisible, [&](const SubjectSharedPtr& object)
        {
            const ObjID& objectId = object->GetObjID();
            if (objectId == playerId)
                return true;
            if (objectId.GetCategory<EnumCategory>() == EnumCategory::eUser)
            {
                const auto session = static_cast<CUser*>(object.get())->GetGameSession();
                if (!session || session->m_eState != SOCKET_STATE::ST_INGAME)
                    return true;
            }
            return false;
        });
    }

    namespace
    {
        // 몬스터 걸음의 보는 사람 목록(몇 명)에 쓴다 — 플레이어 걸음은 CVisibilityMarks로 답한다(Debug는 이것과 대조)
        [[nodiscard]] bool ContainsId(const std::vector<SubjectSharedPtr>& list, const ObjID& id)
        {
            return std::any_of(list.begin(), list.end(), [&](const SubjectSharedPtr& o) { return o->GetObjID() == id; });
        }

        [[nodiscard]] bool ContainsId(const std::vector<ObjID>& list, const ObjID& id)
        {
            return std::find(list.begin(), list.end(), id) != list.end();
        }

        // 플레이어 걸음 한 번의 옛 시야 · 새 시야에 든 ObjID를 적어 두고 "옛 목록에 있었나 · 새 목록에 있나"를 바로 답한다.
        // 두 목록을 서로 선형 탐색하던 ContainsId는 보이는 수의 제곱이었다(16명이면 걸음마다 수백 번, 마을처럼 180명이 보이면
        // 수만 번 비교). 답은 같다 — ObjID 그대로 비교한다. 열린 주소 표 하나를 세대 번호로 비워 다시 쓴다(할당 없음).
        class CVisibilityMarks
        {
        public:
            static constexpr UInt8 kOld = 1;
            static constexpr UInt8 kNew = 2;

            // 새로 적는다 — count = 적을 ObjID 수(옛 + 새). 표는 그 두 배 이상(반 이하로만 찬다)
            void Begin(size_t count)
            {
                const size_t need = std::bit_ceil(std::max<size_t>(64, count * 2));
                if (m_Slots.size() < need)
                    m_Slots.assign(need, Slot{});   // 넓힐 때 옛 기록은 버린다(새 세대만 쓴다)
                m_Mask = m_Slots.size() - 1;
                m_Shift = 64 - std::countr_zero(m_Slots.size());
                if (++m_Stamp == 0)                 // 세대 번호가 한 바퀴 돌았다 — 실제로 비운다
                {
                    std::fill(m_Slots.begin(), m_Slots.end(), Slot{});
                    m_Stamp = 1;
                }
            }

            void Mark(const ObjID& id, UInt8 flag)
            {
                for (size_t i = Index(id);; i = (i + 1) & m_Mask)
                {
                    Slot& slot = m_Slots[i];
                    if (slot.stamp != m_Stamp)
                    {
                        slot.id = id;
                        slot.stamp = m_Stamp;
                        slot.flags = flag;
                        return;
                    }
                    if (slot.id == id)
                    {
                        slot.flags |= flag;
                        return;
                    }
                }
            }

            [[nodiscard]] UInt8 Flags(const ObjID& id) const
            {
                for (size_t i = Index(id);; i = (i + 1) & m_Mask)
                {
                    const Slot& slot = m_Slots[i];
                    if (slot.stamp != m_Stamp)
                        return 0;
                    if (slot.id == id)
                        return slot.flags;
                }
            }

        private:
            struct Slot
            {
                ObjID  id;
                UInt32 stamp = 0;   // 이 세대가 아니면 빈 칸
                UInt8  flags = 0;
            };

            // 첫 칸 — DB 번호의 피보나치 해시(윗자리). 종류가 달라 번호가 같은 객체는 같은 칸에서 시작할 뿐 ObjID 전체로 가린다
            [[nodiscard]] size_t Index(const ObjID& id) const noexcept
            {
                return static_cast<size_t>((id.GetDatabaseID() * 0x9E3779B97F4A7C15ull) >> m_Shift);
            }

            std::vector<Slot> m_Slots;
            size_t m_Mask = 0;
            int    m_Shift = 64;
            UInt32 m_Stamp = 0;
        };

        // 깊이마다 표 하나(걸음 처리 안에서 다시 걸음을 처리해도 서로 덮지 않게 — CSubjectScratch와 같은 방식)
        thread_local std::vector<std::unique_ptr<CVisibilityMarks>> LVisibilityMarks;
        thread_local size_t LVisibilityMarksDepth = 0;

        class CVisibilityMarksLease
        {
        public:
            CVisibilityMarksLease()
            {
                if (LVisibilityMarks.size() <= LVisibilityMarksDepth)
                    LVisibilityMarks.push_back(std::make_unique<CVisibilityMarks>());
                m_pMarks = LVisibilityMarks[LVisibilityMarksDepth++].get();
            }
            ~CVisibilityMarksLease() { --LVisibilityMarksDepth; }
            CVisibilityMarksLease(const CVisibilityMarksLease&) = delete;
            CVisibilityMarksLease& operator=(const CVisibilityMarksLease&) = delete;

            [[nodiscard]] CVisibilityMarks& Get() noexcept { return *m_pMarks; }

        private:
            CVisibilityMarks* m_pMarks;
        };
    }

    // 몬스터 한 걸음: 칸 · 좌표를 옮기고, 지난 걸음의 시야 목록(oldList)과 비교해 ADD · MOVE · REMOVE를 보낸다.
    // 시야 목록이 VIEW_RANGE 안의 플레이어 수(소규모)라 선형 탐색으로 충분하다.
    void MoveMonster(const MonsterSharedPtr& monster, short nextX, short nextY)
    {
        if (monster == nullptr)
            return;
        const ObjID monsterId = monster->GetObjID();
        const SubjectSharedPtr monsterSubject = monster;

        UpdatePosition(monsterSubject, nextX, nextY);

        CSubjectScratch viewerScratch;
        std::vector<SubjectSharedPtr>& viewers = viewerScratch.Get();
        CollectUsers(monster, viewers);

        const std::vector<ObjID>& oldList = monster->GetViewList();
        for (const SubjectSharedPtr& viewer : viewers)
        {
            if (!ContainsId(oldList, viewer->GetObjID())) CUserHelper::SendSUBJECT_ADD_NFY(viewer, monsterSubject);
            else                                          CUserHelper::SendSUBJECT_MOVE_NFY(viewer, monsterSubject);
        }

        for (const ObjID& id : oldList)
        {
            if (ContainsId(viewers, id))
                continue;
            auto viewer = ::GetGameObject<CSubject>(id);
            if (viewer == nullptr)
                continue;
            CUserHelper::SendSUBJECT_REMOVE_NFY(viewer, monsterId);
        }

        monster->AssignViewList(viewers);
    }

    void NotifyPlayerEnteredWorld(ObjID& playerId, bool isRespawn)
    {
        const auto player = ::GetGameObject<CUser>(playerId);
        if (player == nullptr)
            return;

        CSubjectScratch visibleScratch;
        std::vector<SubjectSharedPtr>& visibleSubjects = visibleScratch.Get();
        CollectVisible(player, visibleSubjects);
        const SubjectSharedPtr playerSubject = player;
        for (const SubjectSharedPtr& object : visibleSubjects)
        {
            const ObjID id = object->GetObjID();
            if (id.GetCategory<EnumCategory>() == EnumCategory::eUser)
            {
                if (isRespawn) CUserHelper::SendSUBJECT_RESPAWN_NFY(object, playerId);
                else           CUserHelper::SendSUBJECT_ADD_NFY(object, playerSubject);

                CUserHelper::SendSUBJECT_ADD_NFY(playerSubject, object);
            }
            else if (id.GetCategory<EnumCategory>() == EnumCategory::eMonster)
            {
                CUserHelper::SendSUBJECT_ADD_NFY(playerSubject, object);
                // isRespawn=true: player teleported to a random position, not walked in.
                // Pass as forceWake so aggro monsters skip the WAKE_RANGE check and
                // activate immediately for all monsters within VIEW_RANGE.
                CEntityBehaviorHelper::Wake(static_pointer_cast<CMonster>(object), player, isRespawn);
            }
            else if (id.GetCategory<EnumCategory>() == EnumCategory::eItem)
            {
                // 필드에 떨어진 아이템 — 깨울 것도, 상대에게 나를 알릴 것도 없다.
                // 로그인/리스폰한 플레이어에게만 "여기 아이템이 있다"고 알려주면 된다.
                CUserHelper::SendSUBJECT_ADD_NFY(playerSubject, object);
            }
        }
    }

    // ── Zone A 스레드에서 실행 ─────────────────────────────────────────────────────
    // 플레이어가 존 경계를 넘을 때 Zone A 측 정리를 수행하고,
    // Zone B 스레드에 HandleEnterZone을 비동기 메시지로 위임한다.
    static void HandleZoneTransfer(const UserSharedPtr& player, short nextX, short nextY, ZoneId newZoneId)
    {
        CTransferHelper::Cancel(player);
        const ObjID playerId = player->GetObjID();
        const SubjectSharedPtr playerSubject = player;

        // 1. Transferring 플래그 설정 — 이후 Zone B 스레드가 이 플레이어 패킷을 받아도
        //    HandleEnterZone이 완료될 때까지 처리를 건너뛴다
        player->SetTransferring(true);

        // 2. Zone A 시야 목록 수집 (REMOVE 알림 기준)
        CSubjectScratch oldScratch;
        std::vector<SubjectSharedPtr>& oldVisible = oldScratch.Get();
        CollectVisible(player, oldVisible);

        // 3. Zone A 섹터에서 제거
        GSector->RemoveObject(playerId, player->RefSectorX(), player->RefSectorY());

        // 4. 위치를 존 B 좌표로 갱신 (섹터 그리드에는 아직 미등록 상태)
        //    플레이어가 섹터에서 빠진 직후이므로 다른 Zone 스레드가 섹터를 통해
        //    이 플레이어를 조회할 수 없어 락 없이 SetPosition이 안전하다.
        player->SetPosition(nextX, nextY);

        // 5. Zone A 이웃들에게 이동·제거 알림
        CUserHelper::SendSUBJECT_MOVE_NFY(playerSubject, playerSubject);
        for (const SubjectSharedPtr& object : oldVisible)
        {
            const ObjID id = object->GetObjID();
            CUserHelper::SendSUBJECT_REMOVE_NFY(playerSubject, id);
            if (id.GetCategory<EnumCategory>() == EnumCategory::eUser && object->IsRegistered())
                CUserHelper::SendSUBJECT_REMOVE_NFY(object, playerId);
        }

        // 6. Zone 매핑 갱신
        player->SetZoneId(newZoneId);
        GZoneManager->UpdateObjectZone(playerId, newZoneId);

        // 7. Zone B에 입장 처리 위임 (비동기 메시지 패싱)
        const SessionSharedPtr session = player->GetGameSession();
        GZoneManager->EnqueueByZone(newZoneId, [playerId, nextX, nextY]()
        {
            CSectorHelper::HandleEnterZone(playerId, nextX, nextY);
        });

        // 8. 라우팅을 B로 — 입장 처리를 넣은 "뒤에" 바꾼다(release). 이 값을 본 IO 스레드의 새 패킷은 B 큐에서 입장 처리 뒤에 선다.
        //    (예전엔 라우팅을 먼저 바꿔, 그 사이 B에 먼저 들어간 패킷이 "옮기는 중"이라 B로 되넘겨지다 횟수 상한에서 버려질 수 있었다)
        if (session)
            session->SetRoutingZoneId(newZoneId);
    }
    // ──────────────────────────────────────────────────────────────────────────────

    bool CrossesZone(const UserSharedPtr& player, short nextX, short nextY)
    {
        const ZoneId newZoneId = CZoneLayout::GetZoneIdByWorld(nextX, nextY);
        return player != nullptr && newZoneId != player->GetZoneId() && CZoneLayout::IsValidZoneId(newZoneId);
    }

    void HandlePlayerMove(const UserSharedPtr& player, short nextX, short nextY, bool notifySelf)
    {
        if (player == nullptr)
            return;
        if (player->GetInventory()->IsTransferCommitting()) return;

        // Zone 경계 이동 감지 → Actor 모델 비동기 전달
        if (CrossesZone(player, nextX, nextY))
        {
            HandleZoneTransfer(player, nextX, nextY, CZoneLayout::GetZoneIdByWorld(nextX, nextY));
            return;
        }

        // ── 같은 Zone 내 이동 ─────────────────────────────────────────────────────
        const short oldX     = player->GetX();
        const short oldY     = player->GetY();
        const ObjID playerId = player->GetObjID();
        const SubjectSharedPtr playerSubject = player;

        CSubjectScratch oldScratch;
        std::vector<SubjectSharedPtr>& oldVisible = oldScratch.Get();
        CollectVisible(player, oldVisible);

        UpdatePosition(playerSubject, nextX, nextY);
        if (notifySelf)
            CUserHelper::SendSUBJECT_MOVE_NFY(playerSubject, playerSubject);

        if (oldX == player->GetX() && oldY == player->GetY())
            return;

        CSubjectScratch newScratch;
        std::vector<SubjectSharedPtr>& newVisible = newScratch.Get();
        CollectVisible(player, newVisible);

        // 옛 · 새 시야를 한 번씩 적어 둔다 — 아래 두 번의 "다른 목록에 있나"가 목록 길이와 상관없이 바로 답해진다
        CVisibilityMarksLease marksLease;
        CVisibilityMarks& marks = marksLease.Get();
        marks.Begin(oldVisible.size() + newVisible.size());
        for (const SubjectSharedPtr& object : oldVisible)
            marks.Mark(object->GetObjID(), CVisibilityMarks::kOld);
        for (const SubjectSharedPtr& object : newVisible)
            marks.Mark(object->GetObjID(), CVisibilityMarks::kNew);

        // Pass 1: 새로 보이는 오브젝트 처리
        for (const SubjectSharedPtr& object : newVisible)
        {
            const ObjID id = object->GetObjID();
            const bool isNew = (marks.Flags(id) & CVisibilityMarks::kOld) == 0;
#ifdef _DEBUG
            ASSERT_CRASH(isNew == !ContainsId(oldVisible, id));
#endif

            if (isNew)
                CUserHelper::SendSUBJECT_ADD_NFY(playerSubject, object);

            if (id.GetCategory<EnumCategory>() == EnumCategory::eMonster)
            {
                CEntityBehaviorHelper::Wake(static_pointer_cast<CMonster>(object), player);
            }
            else if (id.GetCategory<EnumCategory>() == EnumCategory::eUser)
            {
                if (isNew) CUserHelper::SendSUBJECT_ADD_NFY(object, playerSubject);
                else       CUserHelper::SendSUBJECT_MOVE_NFY(object, playerSubject);
            }
        }

        // Pass 2: 시야에서 사라진 오브젝트 처리
        for (const SubjectSharedPtr& object : oldVisible)
        {
            const ObjID id = object->GetObjID();
            const bool stillVisible = (marks.Flags(id) & CVisibilityMarks::kNew) != 0;
#ifdef _DEBUG
            ASSERT_CRASH(stillVisible == ContainsId(newVisible, id));
#endif
            if (stillVisible)
                continue;

            CUserHelper::SendSUBJECT_REMOVE_NFY(playerSubject, id);

            if (id.GetCategory<EnumCategory>() == EnumCategory::eUser && object->IsRegistered())
                CUserHelper::SendSUBJECT_REMOVE_NFY(object, playerId);
        }
    }

    // ── Zone B 스레드에서 실행 ─────────────────────────────────────────────────────
    // HandleZoneTransfer가 비동기 메시지로 위임한 Zone B 진입 처리.
    // Zone B 큐의 FIFO 보장으로 이 함수가 새 패킷보다 반드시 먼저 실행된다.
    void HandleEnterZone(const ObjID& playerId, short nextX, short nextY)
    {
        const auto player = ::GetGameObject<CUser>(playerId);
        if (player == nullptr)
            return;  // Zone Transfer 중 접속 끊김 — 안전하게 종료

        // Zone B 섹터에 등록 및 좌표 원자적 갱신
        const bool valid = GSector->UpdateObjectSectorAndPosition(player, nextX, nextY);

        if (!valid)
            GetRandomPosition(const_cast<ObjID&>(playerId));


        // Zone B 이웃들에게 입장 알림 + 플레이어에게 주변 오브젝트 알림
        NotifyPlayerEnteredWorld(const_cast<ObjID&>(playerId), false);

        // Transferring 해제는 NotifyPlayerEnteredWorld 이후에 수행
        // — 이 시점부터 Zone B가 이 플레이어의 모든 패킷을 정상 처리한다
        player->SetTransferring(false);
        CAchieveHelper::OnEnterRegion(player);   // 개인 업적: 들어가 본 지역(걸어서 · 옛길 · 귀환)
    }
    // ──────────────────────────────────────────────────────────────────────────────
}
