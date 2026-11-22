#include "pch.h"
#include "Entity/Transform.h"

void CTransform::Reset() noexcept
{
    m_X       = -1;
    m_Y       = -1;
    m_SectorX = -1;
    m_SectorY = -1;
}
