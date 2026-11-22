#include "pch.h"
#include "Entity/Subject/Subject.h"
#include "Entity/Subject/Monster/Monster.h"

void CSubject::InitInstance()
{
    CGameObject::InitInstance();
    m_pStat = std::make_shared<CStat>();
}

CSubject::SharedPtr MakeNewSubject(const ObjID& e, const ObjID& OwnerID)
{
    CSubject::SharedPtr newSubject{};

    switch (e.GetCategory())
    {
    case EnumCategory::eMonster:
        newSubject = std::make_shared<CMonster>(e);
        if (newSubject)
        {
            newSubject->SetOwnerID(OwnerID);
            newSubject->InitInstance();
        }
        break;
    }

    return newSubject;
}
