#include "pch.h"
#include <bit>
#include "Content/Navigator/AStar.h"
#include "Content/Navigator/Collision.h"
#include "Database/GameData/WorldData.h"

namespace
{
    // A* 한 번 동안 연 칸의 기록(g · 어디서 왔나 · 닫힘). 스레드마다 하나를 다시 쓰고, 세대 번호로 한 번에 비운다.
    // 예전에는 부를 때마다 unordered_map 둘 · unordered_set 하나를 새로 만들어, 칸마다 노드 할당 · 해제와 해시 표 다시 만들기가
    // 길 찾기 비용의 대부분이었다(부하 때 서버 명령의 ≈ 6%). 결과(길 · 차례)는 같다 — 같은 질문(있나 · g · 어디서 · 닫혔나)에 같은 답을 한다.
    class CNodeTable
    {
    public:
        struct Slot
        {
            UInt32 stamp = 0;   // 이 세대가 아니면 빈 칸
            UInt32 key = 0;     // 칸(x, y)
            UInt32 from = 0;    // 어디서 왔나(x, y)
            Int32  g = 0;
            bool   closed = false;
        };

        // 새 찾기 — 앞 찾기의 칸은 세대가 달라 모두 빈 칸이 된다
        void Begin()
        {
            if (m_Slots.empty())
                m_Slots.resize(kCapacity);
            if (++m_Stamp == 0)   // 세대 번호가 한 바퀴 돌았다(약 43억 번에 한 번) — 실제로 비운다
            {
                std::fill(m_Slots.begin(), m_Slots.end(), Slot{});
                m_Stamp = 1;
            }
        }

        // 칸의 기록 — 없으면 nullptr
        [[nodiscard]] Slot* Find(UInt32 key)
        {
            for (size_t i = Hash(key);; i = (i + 1) & kMask)
            {
                Slot& slot = m_Slots[i];
                if (slot.stamp != m_Stamp)
                    return nullptr;
                if (slot.key == key)
                    return &slot;
            }
        }

        // 칸의 기록 — 없으면 새로 만들고 added = true(g · from은 부른 쪽이 채운다)
        [[nodiscard]] Slot& FindOrAdd(UInt32 key, bool& added)
        {
            for (size_t i = Hash(key);; i = (i + 1) & kMask)
            {
                Slot& slot = m_Slots[i];
                if (slot.stamp != m_Stamp)
                {
                    slot = Slot{ m_Stamp, key, 0, 0, false };
                    added = true;
                    return slot;
                }
                if (slot.key == key)
                {
                    added = false;
                    return slot;
                }
            }
        }

    private:
        // 한 찾기에 드는 칸은 많아야 시작 1 + 넓히기 상한 × 이웃 4 — 표는 그 두 배쯤이라 반 남짓 이하로만 찬다(빈 칸이 늘 있다)
        static constexpr size_t kCapacity = std::bit_ceil(static_cast<size_t>(kAStarMaxExpansions) * 4 * 2);
        static constexpr size_t kMask = kCapacity - 1;
        static constexpr int    kShift = 32 - std::countr_zero(kCapacity);
        static_assert(kCapacity > static_cast<size_t>(kAStarMaxExpansions) * 4 + 1);

        [[nodiscard]] static size_t Hash(UInt32 key) noexcept { return static_cast<size_t>((key * 0x9E3779B1u) >> kShift); }

        std::vector<Slot> m_Slots;   // 첫 찾기에 kCapacity칸(≈ 640KB)을 한 번만 잡는다
        UInt32 m_Stamp = 0;
    };

    thread_local CNodeTable LNodes;
    thread_local vector<NODE> LOpenSet;   // 연 칸 힙 — priority_queue<NODE>와 같은 push_heap · pop_heap(같은 비교 · 같은 차례), 용량만 다시 쓴다

    [[nodiscard]] UInt32 NodeKey(short x, short y) noexcept
    {
        return (static_cast<UInt32>(static_cast<UInt16>(x)) << 16) | static_cast<UInt16>(y);
    }
}

vector<NODE> FindPath(short startX, short startY, short goalX, short goalY)
{
    if (startX == goalX && startY == goalY) return {};

    CNodeTable& nodes = LNodes;
    vector<NODE>& openSet = LOpenSet;
    nodes.Begin();
    openSet.clear();

    auto Heuristic = [](short x1, short y1, short x2, short y2) {
        return abs(x1 - x2) + abs(y1 - y2);
    };

    int hCost = Heuristic(startX, startY, goalX, goalY);
    openSet.push_back({ startX, startY, 0, hCost, hCost });
    push_heap(openSet.begin(), openSet.end(), less<NODE>{});
    {
        bool added = false;
        CNodeTable::Slot& start = nodes.FindOrAdd(NodeKey(startX, startY), added);
        start.from = NodeKey(startX, startY);
        start.g = 0;
    }

    short dx[] = { 0, 0, -1, 1 };
    short dy[] = { -1, 1, 0, 0 };

    int expansions = 0;

    while (!openSet.empty()) {
        pop_heap(openSet.begin(), openSet.end(), less<NODE>{});
        NODE current = openSet.back();
        openSet.pop_back();

        CNodeTable::Slot* currentSlot = nodes.Find(NodeKey(current.m_X, current.m_Y));   // 연 칸은 모두 표에 있다
        if (currentSlot->closed)
            continue;
        currentSlot->closed = true;

        if (++expansions > kAStarMaxExpansions)
            return {};   // 너무 멀거나 닿을 수 없다 — 부른 쪽(TryMoveUsingFreshPath)이 잠깐 다시 찾지 않는다

        if (current.m_X == goalX && current.m_Y == goalY) {
            vector<NODE> path;
            while (!(current.m_X == startX && current.m_Y == startY)) {
                path.push_back({ current.m_X, current.m_Y, 0, 0, 0 });
                const UInt32 from = nodes.Find(NodeKey(current.m_X, current.m_Y))->from;
                current.m_X = static_cast<short>(static_cast<UInt16>(from >> 16));
                current.m_Y = static_cast<short>(static_cast<UInt16>(from & 0xFFFF));
            }
            return path;
        }

        const int newCost = currentSlot->g + 1;   // 표는 찾는 동안 옮겨지지 않는다(크기 고정) — 이웃을 넣어도 이 칸 주소는 그대로
        for (int i = 0; i < 4; ++i) {
            short nextX = current.m_X + dx[i];
            short nextY = current.m_Y + dy[i];

            if (nextX < 0 || nextX >= W_WIDTH || nextY < 0 || nextY >= W_HEIGHT) continue;
            if (isCollision(nextX, nextY)) continue;
            // A*는 몬스터 전용이다 — 마을(안전지대)은 몬스터에게 막힌 칸으로 본다.
            if (WorldData::IsSafeZone(nextX, nextY)) continue;

            bool added = false;
            CNodeTable::Slot& next = nodes.FindOrAdd(NodeKey(nextX, nextY), added);
            if (added || newCost < next.g) {
                next.g = newCost;
                int hCost = Heuristic(nextX, nextY, goalX, goalY);
                openSet.push_back({ nextX, nextY, newCost, hCost, newCost + hCost });
                push_heap(openSet.begin(), openSet.end(), less<NODE>{});
                next.from = NodeKey(current.m_X, current.m_Y);
            }
        }
    }

    return {};
}
