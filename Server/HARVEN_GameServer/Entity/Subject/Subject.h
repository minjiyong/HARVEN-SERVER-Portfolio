#pragma once

#include "Entity/Transform.h"
#include "Content/Stat/Stat.h"
#include "Base/GameObject.h"
#include "Content/Buff/StatusSet.h"

class CSubject : public CGameObject, public CTransform
{
    std::string       m_Name;
    CStat::SharedPtr  m_pStat;
    bool              m_bFacingLeft = false; // false = facing right (default)
    // 마지막 걸음에 걸린 시간(ms). SUBJECT_MOVE_NFY.move_time으로 보내 클라이언트가 그
    // 속도로 보간하게 한다. 0 = 순간이동/재배치(보간 없이 바로 옮김).
    UInt16         m_LastStepMs = 0;
    // V2 상태이상(기절·빙결·화상·중독·버프…) — 소유 Zone 스레드 전용
    CStatusSet        m_Status;

public:
    using SharedPtr = SubjectSharedPtr;
    using WeakPtr = SubjectWeakPtr;

public:
    CSubject() = default;
    explicit CSubject(const ObjID& objID) : CGameObject(objID) {};

    std::string& GetName() { return m_Name; }
    CStat::SharedPtr GetStat() { return m_pStat; }
    [[nodiscard]] const CStat* GetStatConst() const noexcept { return m_pStat.get(); }

    bool IsFacingLeft() const           { return m_bFacingLeft; }
    void SetFacingLeft(bool facingLeft) { m_bFacingLeft = facingLeft; }

    // Update facing from a direction delta. dx=0 → no change.
    void UpdateFacing(int dx) { if (dx != 0) m_bFacingLeft = (dx < 0); }

    void SetName(const std::string& name) { m_Name = name; }

    [[nodiscard]] UInt16 GetLastStepMs() const noexcept { return m_LastStepMs; }
    [[nodiscard]] CStatusSet& Status() noexcept { return m_Status; }
    [[nodiscard]] const CStatusSet& Status() const noexcept { return m_Status; }
    void SetLastStepMs(UInt32 ms) noexcept { m_LastStepMs = static_cast<UInt16>(ms > 60000 ? 60000 : ms); }

    virtual void InitInstance() override;
};

CSubject::SharedPtr MakeNewSubject(const ObjID& e, const ObjID& OwnerID);


