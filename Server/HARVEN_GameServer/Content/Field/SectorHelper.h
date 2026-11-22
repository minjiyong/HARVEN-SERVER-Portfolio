#pragma once

#include "Base/ObjID/ObjID.h"

#include <memory>
#include <vector>
#include <utility>

class CUser;

namespace CSectorHelper
{
    void UpdatePosition(ObjID& subjectId, short nextX, short nextY);
    void UpdatePosition(const SubjectSharedPtr& object, short nextX, short nextY);
    // 지금 Zone(GSector) 안의 걸을 수 있는 칸 하나. excludeSafeZone이면 마을 안전지대는 뺀다.
    [[nodiscard]] std::pair<short, short> PickRandomPositionInZone(bool excludeSafeZone);
    void GetRandomPosition(ObjID& subjectId);
    // 이 몬스터를 볼 수 있는 게임 중 플레이어를 outUsers 뒤에 붙인다
    void CollectUsers(const MonsterSharedPtr& monster, std::vector<SubjectSharedPtr>& outUsers);
    // 플레이어 시야에 든 객체를 outVisible 뒤에 붙인다
    void CollectVisible(const UserSharedPtr& player, std::vector<SubjectSharedPtr>& outVisible);
    // 몬스터 한 걸음 — 칸 · 좌표를 옮기고 지난 시야 목록과 비교해 ADD · MOVE · REMOVE를 보낸다
    void MoveMonster(const MonsterSharedPtr& monster, short nextX, short nextY);
    void NotifyPlayerEnteredWorld(ObjID& playerId, bool isRespawn);
    // (nextX, nextY)로 옮기면 다른 Zone으로 넘어가는가 — 넘어가면 HandlePlayerMove가 CUser를 새 Zone 스레드에 넘기므로
    // 부른 쪽은 그 뒤 CUser를 만지지 않는다(좌표 · 기술 · 퀘스트 상태 모두 새 Zone 스레드 것).
    [[nodiscard]] bool CrossesZone(const UserSharedPtr& player, short nextX, short nextY);
    // notifySelf=false: 순번 이동(USER_MOVE_REQ)은 본인에게 SUBJECT_MOVE_NFY 대신
    // USER_MOVE_ACK로 결과를 알려 주므로 본인 알림을 생략한다.
    void HandlePlayerMove(const UserSharedPtr& player, short nextX, short nextY, bool notifySelf = true);

    // Zone B 스레드에서 실행 — HandleZoneTransfer가 비동기로 위임하는 진입 처리
    void HandleEnterZone(const ObjID& playerId, short nextX, short nextY);
}
