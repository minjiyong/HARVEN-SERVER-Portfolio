#pragma once
// 서버 객체의 공유 포인터 별칭(REIW GameServer_DefType.h) — 함수 인자 · 지역 변수는 shared_ptr<CUser> 대신 UserSharedPtr로 쓴다.

#include <memory>

class CGameObject;
class CSubject;
class CUser;
class CMonster;
class CItem;
class CGameSession;
class CDBConnection;

using GameObjectSharedPtr   = std::shared_ptr<CGameObject>;
using GameObjectWeakPtr     = std::weak_ptr<CGameObject>;
using SubjectSharedPtr      = std::shared_ptr<CSubject>;
using SubjectWeakPtr        = std::weak_ptr<CSubject>;
using UserSharedPtr         = std::shared_ptr<CUser>;
using UserWeakPtr           = std::weak_ptr<CUser>;
using MonsterSharedPtr      = std::shared_ptr<CMonster>;
using MonsterWeakPtr        = std::weak_ptr<CMonster>;
using ItemSharedPtr         = std::shared_ptr<CItem>;
using ItemWeakPtr           = std::weak_ptr<CItem>;
using SessionSharedPtr      = std::shared_ptr<CGameSession>;
using SessionWeakPtr        = std::weak_ptr<CGameSession>;
using DBConnectionSharedPtr = std::shared_ptr<CDBConnection>;
