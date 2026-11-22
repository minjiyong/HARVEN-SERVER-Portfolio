#include "pch.h"
#include "Base/GameObjectManager.h"

bool CGameObjectManager::Insert(const ObjID& objID, CGameObject::SharedPtr gameObject)
{
    Shard& shard = ShardOf(objID);
    unique_lock lock(shard.m_Mutex);
    auto [iter, success] = shard.m_Objects.emplace(objID, gameObject);
    if (success && gameObject != nullptr)
        gameObject->SetRegistered(true);
    return success;
}

bool CGameObjectManager::Delete(const ObjID& objID)
{
    Shard& shard = ShardOf(objID);
    unique_lock lock(shard.m_Mutex);
    const auto it = shard.m_Objects.find(objID);
    if (it == shard.m_Objects.end())
        return false;
    if (it->second != nullptr)
        it->second->SetRegistered(false);
    shard.m_Objects.erase(it);
    return true;
}
