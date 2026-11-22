#pragma once

#include "Entity/Subject/Subject.h"

namespace CSubjectHelper
{
    void MovePositionByDirection(short& x, short& y, char direction);
    [[nodiscard]] bool CanSee(const CSubject::SharedPtr& a, const CSubject::SharedPtr& b);
    [[nodiscard]] bool IsAdjacent(const CSubject::SharedPtr& a, const CSubject::SharedPtr& b);
}
