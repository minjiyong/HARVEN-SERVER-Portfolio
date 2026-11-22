#include "pch.h"
#include "Base/GameObject.h"
#include "Base/GameObjectManager.h"

CGameObject::CGameObject(const ObjID& objID)
{
    SetObjID(objID);
}

CGameObject::CGameObject(const ObjID& objID, const ObjID& ownerID)
{
    SetObjID(objID);
    SetOwnerID(ownerID);
}

void CGameObject::InitInstance()
{
    SetParent(nullptr);
}


void CGameObject::SetParent(const SharedPtr& parent) noexcept
{
    m_wpParent = parent;
}
