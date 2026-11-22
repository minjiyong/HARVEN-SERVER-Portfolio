#include "pch.h"
#include "Content/Field/Sector.h"

namespace
{
    using ScratchList = std::vector<SubjectSharedPtr>;

    // 깊이마다 목록 하나. deque — 깊이가 늘어 새 칸을 붙여도 앞 칸의 벡터가 옮겨지지 않는다(스레드가 끝날 때 함께 지운다).
    thread_local std::deque<ScratchList> LScratchPool;
    // 빠른 길: 얕은 깊이의 목록 주소. 생성자 · 소멸자가 없는 thread_local이라 읽을 때마다 초기화 확인(__tls_init)을 거치지 않는다
    // (deque를 바로 읽으면 부를 때마다 확인 두 번 + deque 칸 계산 — 이웃 훑기마다 불려 부하 때 서버 명령의 ≈ 1%였다).
    constexpr size_t kScratchFastDepth = 8;
    thread_local ScratchList* LScratchFast[kScratchFastDepth] = {};
    thread_local size_t LScratchDepth = 0;

    [[nodiscard]] ScratchList* PoolList(size_t depth)
    {
        while (LScratchPool.size() <= depth)
            LScratchPool.emplace_back();
        return &LScratchPool[depth];
    }
}

CSubjectScratch::CSubjectScratch()
{
    const size_t depth = LScratchDepth++;
    if (depth < kScratchFastDepth) [[likely]]
    {
        ScratchList*& list = LScratchFast[depth];
        if (list == nullptr) [[unlikely]]
            list = PoolList(depth);   // 이 깊이를 처음 쓴다
        m_pList = list;
        return;
    }
    m_pList = PoolList(depth);
}

CSubjectScratch::~CSubjectScratch()
{
    m_pList->clear();
    --LScratchDepth;
}

CSector::CSector(short offsetX, short offsetY)
    : m_OffsetX(offsetX), m_OffsetY(offsetY)
{
}

bool CSector::IsValidSector(short sectorX, short sectorY) const noexcept
{
    return sectorX >= m_OffsetX && sectorX < m_OffsetX + kLocalWidth
        && sectorY >= m_OffsetY && sectorY < m_OffsetY + kLocalHeight;
}

bool CSector::IsValidSector(const SectorCoord& sector) const noexcept
{
    return IsValidSector(sector.x, sector.y);
}

SectorCoord CSector::GetSectorCoord(short worldX, short worldY) const noexcept
{
    if (worldX < 0 || worldX >= W_WIDTH || worldY < 0 || worldY >= W_HEIGHT)
        return {};

    return SectorCoord{
        static_cast<short>(worldX / SECTOR_RANGE),
        static_cast<short>(worldY / SECTOR_RANGE)
    };
}

void CSector::AppendNeighbors(short sectorX, short sectorY, EnumCategory category, NeighborSnapshot& out) const
{
    if (IsValidSector(sectorX, sectorY) == false)
        return;

    // 존 경계(offsetX ~ offsetX+kLocalWidth-1)에서 자연스럽게 탐색이 멈춘다.
    // 다른 존 섹터는 이 배열에 존재하지 않으므로 존 경계 = 시야 차단이 자동 적용된다.
    const short minY = std::max<short>(m_OffsetY, static_cast<short>(sectorY - 1));
    const short maxY = std::min<short>(static_cast<short>(m_OffsetY + kLocalHeight - 1), static_cast<short>(sectorY + 1));
    const short minX = std::max<short>(m_OffsetX, static_cast<short>(sectorX - 1));
    const short maxX = std::min<short>(static_cast<short>(m_OffsetX + kLocalWidth - 1), static_cast<short>(sectorX + 1));

    for (short currentY = minY; currentY <= maxY; ++currentY)
        for (short currentX = minX; currentX <= maxX; ++currentX)
            if (MayHold(currentX, currentY, category))
                AppendCell(Cell(currentX, currentY), category, out);
}

void CSector::AppendInRange(short sectorX, short sectorY, int x, int y, int radius, EnumCategory category, NeighborSnapshot& out) const
{
    short minX = 0, maxX = 0, minY = 0, maxY = 0;
    if (!RangeCells(sectorX, sectorY, x, y, radius, minX, maxX, minY, maxY))
        return;
    for (short currentY = minY; currentY <= maxY; ++currentY)
        for (short currentX = minX; currentX <= maxX; ++currentX)
            if (MayHold(currentX, currentY, category))
                AppendCell(Cell(currentX, currentY), category, out);
}

void CSector::AppendWithin(short sectorX, short sectorY, int x, int y, int radius, EnumCategory category, NeighborSnapshot& out) const
{
    short minX = 0, maxX = 0, minY = 0, maxY = 0;
    if (!RangeCells(sectorX, sectorY, x, y, radius, minX, maxX, minY, maxY))
        return;
    radius = std::max(radius, 0);
    for (short currentY = minY; currentY <= maxY; ++currentY)
        for (short currentX = minX; currentX <= maxX; ++currentX)
        {
            if (!MayHold(currentX, currentY, category))
                continue;
            for (const Entry& entry : Cell(currentX, currentY))
            {
                if (category != EnumCategory::eNone && entry.id.GetCategory<EnumCategory>() != category)
                    continue;
#ifdef _DEBUG
                // 칸에 적어 둔 좌표 = 객체 좌표(격자에 든 객체의 좌표는 UpdateObjectSectorAndPosition만 바꾼다)이고 그 칸 안이다 — 거르기 전에(범위 밖도) 본다
                ASSERT_CRASH(entry.x / SECTOR_RANGE == currentX && entry.y / SECTOR_RANGE == currentY);
                if (const SubjectSharedPtr check = Resolve(entry))
                    ASSERT_CRASH(check->GetX() == entry.x && check->GetY() == entry.y);
#endif
                if (std::abs(entry.x - x) > radius || std::abs(entry.y - y) > radius)
                    continue;   // 칸에 적어 둔 좌표로 — 약한 참조를 풀지 않는다
                if (SubjectSharedPtr subject = Resolve(entry))
                    out.push_back(std::move(subject));
            }
        }
}

#ifdef _DEBUG
void CSector::CheckUserCount(short sectorX, short sectorY) const
{
    UInt32 users = 0;
    for (const Entry& entry : Cell(sectorX, sectorY))
        if (entry.id.GetCategory<EnumCategory>() == EnumCategory::eUser)
            ++users;
    ASSERT_CRASH(users == UsersIn(sectorX, sectorY));
}
#endif

SubjectSharedPtr CSector::FindNeighbor(short sectorX, short sectorY, const ObjID& id) const
{
    if (IsValidSector(sectorX, sectorY) == false)
        return nullptr;

    const short minY = std::max<short>(m_OffsetY, static_cast<short>(sectorY - 1));
    const short maxY = std::min<short>(static_cast<short>(m_OffsetY + kLocalHeight - 1), static_cast<short>(sectorY + 1));
    const short minX = std::max<short>(m_OffsetX, static_cast<short>(sectorX - 1));
    const short maxX = std::min<short>(static_cast<short>(m_OffsetX + kLocalWidth - 1), static_cast<short>(sectorX + 1));

    for (short currentY = minY; currentY <= maxY; ++currentY)
        for (short currentX = minX; currentX <= maxX; ++currentX)
            for (const Entry& entry : Cell(currentX, currentY))
                if (entry.id == id)
                {
                    if (SubjectSharedPtr subject = Resolve(entry))
                        return subject;
                }
    return nullptr;
}

bool CSector::InsertEntry(SectorObjects& list, const ObjID& id, const SubjectSharedPtr& subject, short x, short y)
{
    for (Entry& entry : list)
    {
        if (entry.id == id)
        {
            entry.wpSubject = subject;
            entry.x = x;
            entry.y = y;
            return false;
        }
    }
    list.push_back(Entry{ id, subject, x, y });
    return true;
}

bool CSector::EraseEntry(SectorObjects& list, const ObjID& id)
{
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].id == id)
        {
            list[i] = std::move(list.back());
            list.pop_back();
            return true;
        }
    }
    return false;
}

bool CSector::UpdateObjectSectorAndPosition(const SubjectSharedPtr& subject, short worldX, short worldY)
{
    if (subject == nullptr)
        return false;

    const SectorCoord nextSector = GetSectorCoord(worldX, worldY);
    if (IsValidSector(nextSector) == false)
        return false;

    const ObjID& id = subject->GetObjID();
    const bool isUser = id.GetCategory<EnumCategory>() == EnumCategory::eUser;
    short& sectorX = subject->RefSectorX();
    short& sectorY = subject->RefSectorY();
    if (sectorX != nextSector.x || sectorY != nextSector.y)
    {
        const bool alreadyInZone = IsValidSector(sectorX, sectorY);
        if (alreadyInZone && EraseEntry(Cell(sectorX, sectorY), id) && isUser)
            --UsersIn(sectorX, sectorY);

        if (InsertEntry(Cell(nextSector.x, nextSector.y), id, subject, worldX, worldY) && isUser)
            ++UsersIn(nextSector.x, nextSector.y);
        if (!alreadyInZone && isUser)
            InsertEntry(m_Users, id, subject, worldX, worldY);

        sectorX = nextSector.x;
        sectorY = nextSector.y;
    }
    else
    {
        // 같은 칸 안 걸음 — 칸에 적어 둔 좌표만 맞춘다(칸 하나에는 객체가 몇 개뿐)
        for (Entry& entry : Cell(sectorX, sectorY))
        {
            if (entry.id == id)
            {
                entry.x = worldX;
                entry.y = worldY;
                break;
            }
        }
    }

    subject->RefX() = worldX;
    subject->RefY() = worldY;
    return true;
}

void CSector::RemoveObject(const ObjID& objectId, short& inOutSectorX, short& inOutSectorY)
{
    if (IsValidSector(inOutSectorX, inOutSectorY) == false)
        return;

    const bool isUser = objectId.GetCategory<EnumCategory>() == EnumCategory::eUser;
    if (EraseEntry(Cell(inOutSectorX, inOutSectorY), objectId) && isUser)
        --UsersIn(inOutSectorX, inOutSectorY);
    if (isUser)
        EraseEntry(m_Users, objectId);
    inOutSectorX = -1;
    inOutSectorY = -1;
}
