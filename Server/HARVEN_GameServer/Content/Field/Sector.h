#pragma once

#include "Base/GameObjectManager.h"
#include "Content/Field/ZoneLayout.h"

struct SectorCoord
{
    short x = -1;
    short y = -1;

    [[nodiscard]] constexpr bool IsAssigned() const noexcept
    {
        return x >= 0 && y >= 0;
    }

    constexpr bool operator==(const SectorCoord&) const noexcept = default;
};

// 스레드마다 다시 쓰는 객체 목록. 콜백 안에서 또 이웃을 훑어도(중첩) 서로 덮지 않게 깊이마다 하나씩 둔다.
// 이웃 탐색 · 시야 목록은 이동 · 공격마다 불리므로, 부를 때마다 벡터를 새로 할당하지 않으려고 쓴다.
// (저장소는 Sector.cpp 한 곳 — MinGW가 클래스 템플릿의 inline thread_local을 여러 번 정의하는 문제를 피한다)
class CSubjectScratch
{
public:
    CSubjectScratch();
    ~CSubjectScratch();
    CSubjectScratch(const CSubjectScratch&) = delete;
    CSubjectScratch& operator=(const CSubjectScratch&) = delete;

    [[nodiscard]] std::vector<SubjectSharedPtr>& Get() noexcept { return *m_pList; }

private:
    std::vector<SubjectSharedPtr>* m_pList;
};

class CSector
{
public:
    // 칸에 든 객체 하나. 찾을 때마다 전역 객체 표(CGameObjectManager)를 잠그고 뒤지지 않도록 약한 참조를 같이 둔다.
    // x · y = 객체의 지금 좌표(격자에 든 객체의 좌표는 UpdateObjectSectorAndPosition만 바꾼다 — 거기서 함께 맞춘다).
    // 반경 검사를 약한 참조를 풀기 전에 끝낼 수 있다(AppendWithin). m_Users 목록의 x · y는 쓰지 않는다(맞추지 않는다).
    struct Entry
    {
        ObjID          id;
        SubjectWeakPtr wpSubject;
        short          x = 0;
        short          y = 0;
    };
    using SectorObjects = std::vector<Entry>;   // 칸 하나에 든 객체는 몇 개뿐이라 배열이 해시 집합보다 빠르다
    using NeighborSnapshot = std::vector<SubjectSharedPtr>;

    // 이 Zone이 담당하는 섹터 범위: [offsetX .. offsetX+kLocalWidth) x [offsetY .. offsetY+kLocalHeight)
    // 좌표계는 전역 섹터 좌표 (0 ~ kTotalSectorCount-1) 를 그대로 사용한다.
    static constexpr short kLocalWidth  = CZoneLayout::SectorsPerZoneX;
    static constexpr short kLocalHeight = CZoneLayout::SectorsPerZoneY;

public:
    explicit CSector(short offsetX, short offsetY);

    [[nodiscard]] short GetOffsetX() const noexcept { return m_OffsetX; }
    [[nodiscard]] short GetOffsetY() const noexcept { return m_OffsetY; }

    [[nodiscard]] bool IsValidSector(short sectorX, short sectorY) const noexcept;
    [[nodiscard]] bool IsValidSector(const SectorCoord& sector) const noexcept;
    [[nodiscard]] SectorCoord GetSectorCoord(short worldX, short worldY) const noexcept;

    // 칸을 옮기고(필요할 때만) 좌표를 함께 바꾼다. 이 Zone 밖 좌표면 false
    bool UpdateObjectSectorAndPosition(const SubjectSharedPtr& subject, short worldX, short worldY);
    void RemoveObject(const ObjID& objectId, short& inOutSectorX, short& inOutSectorY);

    // 이웃 3×3 칸의 객체(category만, eNone이면 모두)를 out 뒤에 붙인다
    void AppendNeighbors(short sectorX, short sectorY, EnumCategory category, NeighborSnapshot& out) const;

    // AppendNeighbors 중 (x, y)에서 체비셰프 radius 안에 들 수 있는 칸만 훑는다 — 부른 쪽이 그 거리로 다시 거르면
    // 결과(객체와 순서)는 AppendNeighbors와 같다. 칸은 객체 좌표 / SECTOR_RANGE라 범위 밖 칸에는 걸릴 객체가 없다.
    // 시야(반경 9 — 칸 9개 중 평균 7.8개) · 범위 기술(반경 0~3 — 1~2.6개)에서 훑는 칸과 약한 참조 풀기가 준다(REIW 사분면 선택을 일반화).
    void AppendInRange(short sectorX, short sectorY, int x, int y, int radius, EnumCategory category, NeighborSnapshot& out) const;

    // AppendInRange에서 (x, y)와 체비셰프 거리 radius 안인 객체만 — 칸에 적어 둔 좌표로 거르므로 범위 밖 객체는 약한 참조를 풀지 않는다.
    // 부른 쪽이 "체비셰프 radius 안"(또는 그보다 좁게)으로 다시 거르던 곳이면 결과(객체와 순서)가 AppendInRange + 그 검사와 같다.
    // 시야(CollectVisible · CollectUsers)처럼 걸음마다 불리는 곳 — 훑는 칸(최대 30×30칸)의 객체 중 절반쯤이 시야(19×19칸) 밖이다.
    // (어느 훑기든 eUser면 플레이어 항목이 없는 칸은 건너뛴다 — MayHold)
    void AppendWithin(short sectorX, short sectorY, int x, int y, int radius, EnumCategory category, NeighborSnapshot& out) const;

    // 이웃 3×3 칸(이 Zone 안)에 있는 id의 객체. 없으면 nullptr
    [[nodiscard]] SubjectSharedPtr FindNeighbor(short sectorX, short sectorY, const ObjID& id) const;

    // 이웃 3×3 칸의 객체를 먼저 모은 뒤(콜백 안에서 칸이 바뀌어도 안전) 하나씩 부른다.
    // category를 주면 그 종류만 — 플레이어에게만 알리는 곳은 몬스터를 건너뛴다.
    template<typename Callback>
    requires std::invocable<Callback&, const SubjectSharedPtr&>
    void ForEachNeighborObject(short sectorX, short sectorY, Callback&& callback) const
    {
        ForEachNeighborObject(sectorX, sectorY, EnumCategory::eNone, std::forward<Callback>(callback));
    }

    template<typename Callback>
    requires std::invocable<Callback&, const SubjectSharedPtr&>
    void ForEachNeighborObject(short sectorX, short sectorY, EnumCategory category, Callback&& callback) const
    {
        CSubjectScratch scratch;
        NeighborSnapshot& snapshot = scratch.Get();
        AppendNeighbors(sectorX, sectorY, category, snapshot);
        for (const auto& object : snapshot)
            callback(object);
    }

    // ForEachNeighborObject의 반경판 — 콜백이 (x, y)에서 radius 안인지 다시 보는 곳만 쓴다(AppendInRange)
    template<typename Callback>
    requires std::invocable<Callback&, const SubjectSharedPtr&>
    void ForEachObjectInRange(short sectorX, short sectorY, int x, int y, int radius, EnumCategory category, Callback&& callback) const
    {
        CSubjectScratch scratch;
        NeighborSnapshot& snapshot = scratch.Get();
        AppendInRange(sectorX, sectorY, x, y, radius, category, snapshot);
        for (const auto& object : snapshot)
            callback(object);
    }

    // ForEachObjectInRange의 정확한 반경판(AppendWithin) — 콜백이 체비셰프 radius 안인지 다시 보던 곳
    template<typename Callback>
    requires std::invocable<Callback&, const SubjectSharedPtr&>
    void ForEachObjectWithin(short sectorX, short sectorY, int x, int y, int radius, EnumCategory category, Callback&& callback) const
    {
        CSubjectScratch scratch;
        NeighborSnapshot& snapshot = scratch.Get();
        AppendWithin(sectorX, sectorY, x, y, radius, category, snapshot);
        for (const auto& object : snapshot)
            callback(object);
    }

    // 칸 하나의 객체(category만). 퀘스트 보스 분배 · NPC 주변 플레이어처럼 3×3이 아닌 범위를 훑을 때
    template<typename Callback>
    requires std::invocable<Callback&, const SubjectSharedPtr&>
    void ForEachObjectInCell(short sectorX, short sectorY, EnumCategory category, Callback&& callback) const
    {
        if (!IsValidSector(sectorX, sectorY) || !MayHold(sectorX, sectorY, category))
            return;
        CSubjectScratch scratch;
        NeighborSnapshot& snapshot = scratch.Get();
        AppendCell(Cell(sectorX, sectorY), category, snapshot);
        for (const auto& object : snapshot)
            callback(object);
    }

    // 이 Zone(지역) 전체의 플레이어(Phase 3-B 일반 채팅 — "같은 지역 전체"). Zone 스레드에서만 부른다.
    // 칸을 훑지 않고 Zone에 든 플레이어 목록을 쓴다. 먼저 모은 뒤 부른다(콜백 안에서 목록이 바뀌어도 안전).
    template<typename Callback>
    requires std::invocable<Callback&, const SubjectSharedPtr&>
    void ForEachUserInZone(Callback&& callback) const
    {
        CSubjectScratch scratch;
        NeighborSnapshot& users = scratch.Get();
        AppendCell(m_Users, EnumCategory::eUser, users);
        for (const auto& user : users)
            callback(user);
    }

private:
    using SectorGrid = std::array<std::array<SectorObjects, kLocalWidth>, kLocalHeight>;
    using UserCountGrid = std::array<std::array<UInt32, kLocalWidth>, kLocalHeight>;

    [[nodiscard]] const SectorObjects& Cell(short sectorX, short sectorY) const { return m_Sectors[sectorY - m_OffsetY][sectorX - m_OffsetX]; }
    [[nodiscard]] SectorObjects& Cell(short sectorX, short sectorY) { return m_Sectors[sectorY - m_OffsetY][sectorX - m_OffsetX]; }
    [[nodiscard]] UInt32 UsersIn(short sectorX, short sectorY) const { return m_CellUsers[sectorY - m_OffsetY][sectorX - m_OffsetX]; }
    [[nodiscard]] UInt32& UsersIn(short sectorX, short sectorY) { return m_CellUsers[sectorY - m_OffsetY][sectorX - m_OffsetX]; }

    // 이 칸을 훑어 볼 까닭이 있는가 — 플레이어만 찾는데 칸에 플레이어 항목이 하나도 없으면 건너뛴다(몬스터 항목을 하나씩 넘기지 않는다).
    // 결과(객체와 순서)는 그대로다: 건너뛴 칸에서는 어차피 아무것도 나오지 않는다.
    [[nodiscard]] bool MayHold(short sectorX, short sectorY, EnumCategory category) const
    {
#ifdef _DEBUG
        CheckUserCount(sectorX, sectorY);
#endif
        return category != EnumCategory::eUser || UsersIn(sectorX, sectorY) != 0;
    }
#ifdef _DEBUG
    void CheckUserCount(short sectorX, short sectorY) const;
#endif

    // 3×3 이웃 ∩ (x, y)에서 반경 radius가 닿는 칸 ∩ 이 Zone(AppendInRange · AppendWithin 공용). 칸이 하나도 없으면 false.
    // 3×3 밖 칸은 예전처럼 보지 않는다 — 결과가 AppendNeighbors + 반경 검사와 같다.
    bool RangeCells(short sectorX, short sectorY, int x, int y, int radius, short& minX, short& maxX, short& minY, short& maxY) const
    {
        if (IsValidSector(sectorX, sectorY) == false)
            return false;
        // 아래로 내림하는 나눗셈(x - radius가 음수여도 칸 번호가 맞게)
        const auto cellOf = [](int v) { return (v >= 0 ? v : v - (SECTOR_RANGE - 1)) / SECTOR_RANGE; };
        radius = std::max(radius, 0);
        minX = static_cast<short>(std::max(std::max<int>(m_OffsetX, sectorX - 1), cellOf(x - radius)));
        maxX = static_cast<short>(std::min(std::min<int>(m_OffsetX + kLocalWidth - 1, sectorX + 1), cellOf(x + radius)));
        minY = static_cast<short>(std::max(std::max<int>(m_OffsetY, sectorY - 1), cellOf(y - radius)));
        maxY = static_cast<short>(std::min(std::min<int>(m_OffsetY + kLocalHeight - 1, sectorY + 1), cellOf(y + radius)));
        return minX <= maxX && minY <= maxY;
    }

    // 관리자에서 빠진(접속 종료 · 습득된) 객체는 칸에 남아 있어도 건너뛴다 — 예전 "ID로 다시 찾기"와 같은 결과
    [[nodiscard]] static SubjectSharedPtr Resolve(const Entry& entry)
    {
        SubjectSharedPtr subject = entry.wpSubject.lock();
        if (subject != nullptr && !subject->IsRegistered())
            subject.reset();
        return subject;
    }

    static void AppendCell(const SectorObjects& cell, EnumCategory category, NeighborSnapshot& out)
    {
        for (const Entry& entry : cell)
        {
            if (category != EnumCategory::eNone && entry.id.GetCategory<EnumCategory>() != category)
                continue;
            if (SubjectSharedPtr subject = Resolve(entry))
                out.push_back(std::move(subject));
        }
    }

    // 새로 붙였으면 true(이미 있던 id면 약한 참조 · 좌표만 바꾸고 false)
    static bool InsertEntry(SectorObjects& list, const ObjID& id, const SubjectSharedPtr& subject, short x, short y);
    // 지웠으면 true
    static bool EraseEntry(SectorObjects& list, const ObjID& id);

private:
    short m_OffsetX;
    short m_OffsetY;
    SectorGrid m_Sectors;
    UserCountGrid m_CellUsers{};   // 칸마다 든 플레이어 항목 수(MayHold) — 칸에 넣고 뺄 때 함께 맞춘다
    SectorObjects m_Users;         // 이 Zone 섹터에 든 플레이어(일반 채팅 범위)
};
