#pragma once

class CGameSession;

struct NODE {
    short m_X, m_Y;
    int m_GCost, m_HCost, m_FCost;
    bool operator<(const NODE& rhs) const {
        return m_FCost > rhs.m_FCost;
    }
};

struct hash_pair {
    template <class T1, class T2>
    size_t operator()(const pair<T1, T2>& p) const {
        size_t h1 = hash<T1>{}(p.first);
        size_t h2 = hash<T2>{}(p.second);
        // Boost hash_combine style: reduces collisions for grid coordinates
        h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        return h1;
    }
};

// 한 번에 넓히는 칸 수 상한(REIW TridentNavigator와 같은 값). 닿을 수 없는 목표(마을 너머 · 막힌 칸)면 예전엔 이어진 땅을
// 끝까지 뒤져 Zone 스레드가 수백 ms 멈출 수 있었다 — 상한에 닿으면 길 없음(빈 경로)으로 돌려준다.
constexpr int kAStarMaxExpansions = 4096;

vector<NODE> FindPath(short startX, short startY, short goalX, short goalY);
