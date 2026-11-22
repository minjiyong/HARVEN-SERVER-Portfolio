#pragma once
// 불변 스냅샷 — 한 판(세대)의 기획 데이터 전부. GameData::LoadFromDir가 만들고 GD()로 읽는다.
// 묶음마다 Snap_<묶음>.h의 구조체 하나(Phase 2 일꾼은 자기 Snap 헤더만 고친다).
#include <cstdint>
#include <string>
#include <vector>

#include "Database/GameData/Load/StringArena.h"
#include "Database/GameData/Load/TableShape.h"
#include "Database/GameData/Load/Snap_Town.h"
#include "Database/GameData/Load/Snap_Region.h"
#include "Database/GameData/Load/Snap_Monster.h"
#include "Database/GameData/Load/Snap_MonsterAI.h"
#include "Database/GameData/Load/Snap_Item.h"
#include "Database/GameData/Load/Snap_Econ.h"
#include "Database/GameData/Load/Snap_Level.h"
#include "Database/GameData/Load/Snap_Skill.h"
#include "Database/GameData/Load/Snap_Growth.h"
#include "Database/GameData/Load/Snap_Npc.h"
#include "Database/GameData/Load/Snap_Quest.h"
#include "Database/GameData/Load/Snap_Party.h"
#include "Database/GameData/Load/Snap_Guild.h"
#include "Database/GameData/Load/Snap_Achieve.h"

struct GameDataSnapshot
{
    std::string version;    // manifest.json 판 해시
    StringArena strings;    // 모든 const char* 필드가 가리키는 곳
    std::vector<GameDataLoad::TableShape> shape;   // 표 이름 순(핫 리로드 구조 비교)

    SnapTown town;
    SnapRegion region;
    SnapMonster monster;
    SnapMonsterAI monsterAI;
    SnapItem item;
    SnapEcon econ;
    SnapLevel level;
    SnapSkill skill;
    SnapGrowth growth;
    SnapNpc npc;
    SnapQuest quest;
    SnapParty party;
    SnapGuild guild;
    SnapAchieve achieve;
};

namespace GameDataLoad
{
    // 이 스레드에서 GD()가 볼 스냅샷을 잠시 바꾼다 — 아직 올리지 않은 새 판을 검증 · 덤프할 때(파사드가 새 판을 읽게).
    void SetThreadSnapshot(const GameDataSnapshot* snap) noexcept;
    [[nodiscard]] const GameDataSnapshot* ThreadSnapshot() noexcept;

    class ScopedSnapshot
    {
    public:
        explicit ScopedSnapshot(const GameDataSnapshot* snap) noexcept : m_prev(ThreadSnapshot()) { SetThreadSnapshot(snap); }
        ~ScopedSnapshot() { SetThreadSnapshot(m_prev); }
        ScopedSnapshot(const ScopedSnapshot&) = delete;
        ScopedSnapshot& operator=(const ScopedSnapshot&) = delete;

    private:
        const GameDataSnapshot* m_prev;
    };
}
