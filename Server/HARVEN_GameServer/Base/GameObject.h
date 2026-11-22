#pragma once

#include "Base/ObjID/ObjID.h"
#include "Network/GameSession.h"

class CGameObject : public ObjID, public enable_shared_from_this<CGameObject>
{
    GameObjectWeakPtr m_wpParent;
    ObjID m_AccountID;
    ObjID m_OwnerID;
    // CGameObjectManager에 들어 있는 동안 true — 섹터가 들고 있는 약한 참조가 "아직 월드에 있는 객체"인지 본다
    std::atomic<bool> m_bRegistered{ false };

public:
    using SharedPtr = GameObjectSharedPtr;
    using WeakPtr = GameObjectWeakPtr;

public:
    CGameObject() = default;
    explicit CGameObject(const ObjID& objID);
    explicit CGameObject(const ObjID& objID, const ObjID& ownerID);

    virtual ~CGameObject() = default;

    // 복사본은 아직 관리자에 들어가지 않은 새 객체다
    CGameObject(const CGameObject& o)
        : ObjID(o), enable_shared_from_this<CGameObject>(), m_wpParent(o.m_wpParent), m_AccountID(o.m_AccountID), m_OwnerID(o.m_OwnerID) {}
    CGameObject& operator=(const CGameObject& o)
    {
        ObjID::operator=(o);
        m_wpParent = o.m_wpParent;
        m_AccountID = o.m_AccountID;
        m_OwnerID = o.m_OwnerID;
        return *this;
    }

    [[nodiscard]] bool IsRegistered() const noexcept { return m_bRegistered.load(std::memory_order_acquire); }
    void SetRegistered(bool registered) noexcept { m_bRegistered.store(registered, std::memory_order_release); }

    [[nodiscard]] ObjID& GetAccountID()       noexcept { return m_AccountID; }
    [[nodiscard]] const ObjID& GetAccountID() const noexcept { return m_AccountID; }
    [[nodiscard]] ObjID& GetOwnerID()         noexcept { return m_OwnerID; }
    [[nodiscard]] const ObjID& GetOwnerID()   const noexcept { return m_OwnerID; }
    
    void    SetAccountID(const decltype(m_AccountID)& o) noexcept { m_AccountID = o; }
    void    SetOwnerID(const decltype(m_OwnerID)& o) noexcept { m_OwnerID = o; }

    template<typename TObject> requires derived_from<TObject, CGameObject>
    [[nodiscard]] shared_ptr<TObject> GetParent() const noexcept
    {
        return dynamic_pointer_cast<TObject>(m_wpParent.lock());
    }

    void SetParent(const SharedPtr& parent) noexcept;

    template<typename TObject = CGameObject> requires derived_from<TObject, CGameObject>
    [[nodiscard]] shared_ptr<TObject> self()
    {
        return dynamic_pointer_cast<TObject>(shared_from_this());
    }

    virtual void InitInstance();
};
