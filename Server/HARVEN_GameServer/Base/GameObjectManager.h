#pragma once

#include "Entity/Subject/Subject.h"
#include "User/User.h"
#include "Entity/Subject/Monster/Monster.h"

// 월드의 모든 객체(플레이어 · 몬스터 · 필드 아이템)를 ObjID로 찾는 전역 표.
// Zone 스레드 16개와 IO · DB · 서비스 스레드가 함께 읽으므로, 표 하나 · 잠금 하나 대신 ObjID 해시로 나눈
// 조각(kShardCount)마다 잠금을 따로 둔다 — 서로 다른 객체를 찾는 스레드끼리 같은 잠금 줄을 두고 다투지 않는다.
class CGameObjectManager
{
    static constexpr size_t kShardCount = 64;

    struct alignas(64) Shard
    {
        mutable shared_mutex                         m_Mutex;
        unordered_map<ObjID, CGameObject::SharedPtr> m_Objects;
    };

    std::array<Shard, kShardCount> m_Shards;

public:
    CGameObjectManager() = default;

    bool Insert(const ObjID& objID, CGameObject::SharedPtr gameObject);
    bool Delete(const ObjID& objID);

    template<typename _Ty = CGameObject> requires derived_from<_Ty, CGameObject>
    [[nodiscard]] shared_ptr<_Ty> Seek(const ObjID& objID)
    {
        const Shard& shard = ShardOf(objID);
        CGameObject::SharedPtr found;
        {
            shared_lock lock(shard.m_Mutex);
            const auto it = shard.m_Objects.find(objID);
            if (it == shard.m_Objects.end())
                return nullptr;
            found = it->second;
        }
        return Cast<_Ty>(objID, std::move(found));
    }

private:
    [[nodiscard]] Shard& ShardOf(const ObjID& objID) noexcept
    {
        size_t h = std::hash<ObjID>{}(objID);
        h ^= h >> 29;
        return m_Shards[h & (kShardCount - 1)];
    }

    template<typename _Ty>
    [[nodiscard]] static shared_ptr<_Ty> Cast(const ObjID& objID, CGameObject::SharedPtr&& gameObject)
    {
        if (gameObject == nullptr)
            return nullptr;

        const auto category = objID.GetCategory<EnumCategory>();
        if constexpr (same_as<_Ty, CGameObject>)
        {
            return std::move(gameObject);
        }
        else if constexpr (same_as<_Ty, CUser>)
        {
            if (category != EnumCategory::eUser)
                return nullptr;
            return static_pointer_cast<CUser>(std::move(gameObject));
        }
        else if constexpr (same_as<_Ty, CMonster>)
        {
            if (category != EnumCategory::eMonster)
                return nullptr;
            return static_pointer_cast<CMonster>(std::move(gameObject));
        }
        else if constexpr (same_as<_Ty, CSubject>)
        {
            // 관리자에 들어오는 객체는 종류가 ObjID에 찍혀 있다(CUser · CMonster · CItem) — dynamic_cast 없이 바로 바꾼다
            switch (category)
            {
            case EnumCategory::eUser:    return static_pointer_cast<CUser>(std::move(gameObject));
            case EnumCategory::eMonster: return static_pointer_cast<CMonster>(std::move(gameObject));
            default:                     return dynamic_pointer_cast<CSubject>(std::move(gameObject));
            }
        }
        else
        {
            return dynamic_pointer_cast<_Ty>(std::move(gameObject));
        }
    }
};

extern shared_ptr<CGameObjectManager> GGameObjectManager;

template <typename _Ty>
std::shared_ptr<_Ty> GetGameObject(const ObjID& objID)
{
    return GGameObjectManager->Seek<_Ty>(objID);
}
