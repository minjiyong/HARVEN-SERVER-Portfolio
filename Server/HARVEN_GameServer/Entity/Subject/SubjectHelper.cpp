#include "pch.h"
#include "Entity/Subject/SubjectHelper.h"
#include "Content/Navigator/Collision.h"

namespace CSubjectHelper
{
    void MovePositionByDirection(short& x, short& y, char direction)
    {
        // 0:UP  1:DOWN  2:LEFT  3:RIGHT
        // 4:UP-LEFT  5:UP-RIGHT  6:DOWN-LEFT  7:DOWN-RIGHT
        // 대각선은 각 축을 독립적으로 처리 → 벽에 자연스럽게 슬라이딩
        switch (direction)
        {
        case 0: if (y > 0)            { --y; if (isCollision(x, y)) ++y; } break;
        case 1: if (y < W_HEIGHT - 1) { ++y; if (isCollision(x, y)) --y; } break;
        case 2: if (x > 0)            { --x; if (isCollision(x, y)) ++x; } break;
        case 3: if (x < W_WIDTH - 1)  { ++x; if (isCollision(x, y)) --x; } break;
        case 4: // UP-LEFT
            if (y > 0)           { --y; if (isCollision(x, y)) ++y; }
            if (x > 0)           { --x; if (isCollision(x, y)) ++x; }
            break;
        case 5: // UP-RIGHT
            if (y > 0)           { --y; if (isCollision(x, y)) ++y; }
            if (x < W_WIDTH - 1) { ++x; if (isCollision(x, y)) --x; }
            break;
        case 6: // DOWN-LEFT
            if (y < W_HEIGHT - 1){ ++y; if (isCollision(x, y)) --y; }
            if (x > 0)           { --x; if (isCollision(x, y)) ++x; }
            break;
        case 7: // DOWN-RIGHT
            if (y < W_HEIGHT - 1){ ++y; if (isCollision(x, y)) --y; }
            if (x < W_WIDTH - 1) { ++x; if (isCollision(x, y)) --x; }
            break;
        }
    }

    bool CanSee(const CSubject::SharedPtr& a, const CSubject::SharedPtr& b)
    {
        if (!a || !b) return false;
        if (abs(a->GetX() - b->GetX()) > GViewRange) return false;
        return abs(a->GetY() - b->GetY()) <= GViewRange;
    }

    bool IsAdjacent(const CSubject::SharedPtr& a, const CSubject::SharedPtr& b)
    {
        if (!a || !b) return false;
        const int dx = b->GetX() - a->GetX();
        const int dy = b->GetY() - a->GetY();
        return abs(dx) + abs(dy) == 1;
    }

}
