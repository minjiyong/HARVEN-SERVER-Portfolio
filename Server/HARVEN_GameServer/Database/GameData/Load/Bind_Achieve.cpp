#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 개인 업적 묶음 — 탭 Achievement → 스냅샷(색인 Catalog) → 파사드 AchieveRule::Get · IsHuntMaterial.
// 모양은 Bind_Guild.cpp와 같다.
//   1) 파사드 — 색인 · 사냥 재료(구조 상수 · 색인 만들기는 헤더 AchieveRules.h)
//   2) Bind    — 열마다 RowReader → id 순으로 정렬해 색인(BuildCatalog). 사냥 재료는 econ 묶음(TierLoot — 이 묶음보다 먼저 묶인다)에서
//   3) Validate — R136(표 구조) · R137(kind · param) · R138(목표가 닿는다) · R139(묶음 · 단계 · 목표 오름) · R140(보상)
//   4) Dump    — Canon_Achieve.h(datatool export와 같은 바이트)
// ─────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "Database/GameData/AchieveRules.h"
#include "Database/GameData/EnchantRules.h"
#include "Database/GameData/Item/ItemTableData.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/LevelTable.h"
#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Achieve.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/MonsterTable.h"
#include "Database/GameData/QuestData.h"
#include "Database/GameData/WorldData.h"

static_assert(AchieveRule::kRegionBits == WorldData::kRegionCount, "들어가 본 지역 비트(u16)는 지역 수와 같다 — 지역을 늘리면 진행 기록 판을 올린다");
static_assert(AchieveRule::kMaxGradeParam == static_cast<int>(ItemGrade::eMythic), "eGear param 위 = 마지막 등급");

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace AchieveRule
{
    const Catalog& Get() noexcept { return GD().achieve.cat; }

    bool IsHuntMaterial(uint16_t itemId) noexcept
    {
        const auto& v = GD().achieve.huntMaterials;
        return itemId != 0 && std::binary_search(v.begin(), v.end(), itemId);
    }
}

namespace GameDataLoad
{
    namespace
    {
        std::string N(uint64_t v) { return std::to_string(v); }

        // 들어갈 수 있는 지역 수 — 마을 지역 + 옛길 목적지(teleport_fee > 0 — 도착 칸은 R077이 그 지역 안 · 걸을 수 있는 칸으로 본다)
        int ReachableRegions()
        {
            int n = 0;
            for (const WorldData::RegionRow& r : WorldData::Regions())
                if (r.teleportFee > 0 || r.zoneId == WorldData::kTownZoneId)
                    ++n;
            return n;
        }

        // 강화로 오를 수 있는 가장 높은 단계 — +L → +L+1의 성공 몫과 가호가 모두 0인 첫 L(없으면 끝없음).
        // 표 마지막 행이 그 위 단계 모두라 마지막 행까지만 보면 된다.
        uint64_t EnchantReach(const SnapItem& item)
        {
            for (const auto& e : item.enchant)
                if (e.successTickets == 0 && e.graceAttempts == 0)
                    return e.level;
            return item.enchant.empty() ? 0 : UINT64_MAX;
        }
    }

    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindAchieve(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapAchieve& a = snap.achieve;
        std::vector<std::pair<AchieveRule::Row, uint32_t>> read;
        if (const Json* rows = ctx.Rows("Achievement"))
        {
            read.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Achievement", i, (*rows)[i]);
                AchieveRule::Row x;
                r.Int("id", x.id).Enum("category", x.category).Int("group", x.group).Int("step", x.step).Str("name", x.name)
                 .Str("desc", x.desc).Enum("kind", x.kind).Int("param", x.param).Int("target", x.target).Int("points", x.points)
                 .Int("reward_item", x.rewardItem).Int("reward_count", x.rewardCount).Int("reward_adena", x.rewardAdena);
                r.Finish();
                read.emplace_back(x, static_cast<uint32_t>(i));
            }
        }
        // id 순(정본 · 패킷 · 진행 기록이 모두 id 순) — 같은 id는 표 순서대로(R136이 잡는다)
        std::stable_sort(read.begin(), read.end(), [](const auto& l, const auto& r) { return l.first.id < r.first.id; });
        a.cat.rows.clear();
        a.src.clear();
        for (const auto& [row, src] : read)
        {
            a.cat.rows.push_back(row);
            a.src.push_back(src);
        }
        AchieveRule::BuildCatalog(a.cat);

        // 사냥 정수 · 결정 — TierLoot의 정수 · 결정 · 정예 결정(econ 묶음이 먼저 묶였다)
        a.huntMaterials.clear();
        for (const auto& t : snap.econ.tiers)
            for (const ItemTableId id : { t.essenceItem, t.crystalItem, t.eliteCrystalItem })
                if (id != ITEM_TABLE_ID_NONE)
                    a.huntMaterials.push_back(id);
        std::sort(a.huntMaterials.begin(), a.huntMaterials.end());
        a.huntMaterials.erase(std::unique(a.huntMaterials.begin(), a.huntMaterials.end()), a.huntMaterials.end());
    }

    // ── 3) Validate ──────────────────────────────────────────────────────────
    void ValidateAchieve(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const SnapAchieve& a = snap.achieve;
        const auto& rows = a.cat.rows;
        auto where = [&](size_t i, const char* key) { return v.Where("Achievement", a.src[i], key); };

        // R136 — 표 구조: id 1~1023 · 겹침 없음 · 이름 · 설명(비어 있지 않음 · 바이트) · target ≥ 1 · (kind, param) 종류 ≤ 128
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const AchieveRule::Row& r = rows[i];
            if (r.id < 1 || r.id > AchieveRule::kMaxId)
                v.Fail("R136", where(i, "id"), "업적 id는 1 ~ " + N(AchieveRule::kMaxId) + "(진행 기록의 비트 칸 — AchieveRule::kMaxId) — 지금 " + N(r.id));
            else if (i > 0 && rows[i - 1].id == r.id)
                v.Fail("R136", where(i, "id"), "id " + N(r.id) + "가 겹친다");
            if (r.name == nullptr || r.name[0] == '\0' || Utf8Bytes(r.name) > static_cast<size_t>(AchieveRule::kNameBytes))
                v.Fail("R136", where(i, "name"), "이름은 1 ~ " + N(AchieveRule::kNameBytes) + "바이트 — 지금 " + N(r.name != nullptr ? Utf8Bytes(r.name) : 0));
            if (r.desc == nullptr || r.desc[0] == '\0' || Utf8Bytes(r.desc) > static_cast<size_t>(AchieveRule::kDescBytes))
                v.Fail("R136", where(i, "desc"), "설명은 1 ~ " + N(AchieveRule::kDescBytes) + "바이트 — 지금 " + N(r.desc != nullptr ? Utf8Bytes(r.desc) : 0));
            if (r.target == 0)
                v.Fail("R136", where(i, "target"), "목표는 1 이상");
        }
        if (a.cat.keys.size() > static_cast<size_t>(AchieveRule::kMaxStatKeys))
            v.Fail("R136", "Achievement", "세는 (kind, param) 종류 " + N(a.cat.keys.size()) + " > " + N(AchieveRule::kMaxStatKeys)
                + "(AchieveRule::kMaxStatKeys — 진행 기록 VARBINARY(4000) 안)");

        // R137 — kind · param: 처치 param = 0 또는 허수아비가 아닌 종 · 보스 param = 0 또는 보스 종 · 장비 param = 등급(0~5) ·
        //        퀘스트 param = 0 또는 퀘스트 id · 그 밖 kind는 0
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const AchieveRule::Row& r = rows[i];
            if (r.param == 0)
                continue;
            switch (r.kind)
            {
            case AchKind::eKill:
            {
                const MonsterTableRow* m = MonsterTable::Find(r.param);
                if (m == nullptr)
                    v.Fail("R137", where(i, "param"), "eKill의 param은 몬스터 종(0 = 아무 몬스터) — 없는 종 " + N(r.param));
                else if (m->dummy)
                    v.Fail("R137", where(i, "param"), "허수아비(" + N(r.param) + ")는 처치되지 않는다 — 영영 이룰 수 없다");
                break;
            }
            case AchKind::eKillBoss:
            {
                const MonsterTableRow* m = MonsterTable::Find(r.param);
                if (m == nullptr || !m->boss)
                    v.Fail("R137", where(i, "param"), "eKillBoss의 param은 보스 종(Monster.boss, 0 = 아무 보스) — " + N(r.param) + (m == nullptr ? "은 없는 종" : "은 보스가 아니다"));
                break;
            }
            case AchKind::eGear:
                if (r.param > AchieveRule::kMaxGradeParam)
                    v.Fail("R137", where(i, "param"), "eGear의 param은 등급(ItemGrade 0 ~ " + N(AchieveRule::kMaxGradeParam) + ") — 지금 " + N(r.param));
                break;
            case AchKind::eQuest:
                if (QuestData::Find(r.param) == nullptr)
                    v.Fail("R137", where(i, "param"), "eQuest의 param은 퀘스트 id(0 = 아무거나) — 없는 퀘스트 " + N(r.param));
                break;
            default:
                v.Fail("R137", where(i, "param"), "이 kind는 param을 쓰지 않는다(0이어야 한다) — 지금 " + N(r.param));
                break;
            }
        }

        // R138 — 목표가 닿는다: 레벨 ≤ 최고 레벨 · 강화 단계까지 오를 수 있다 · 지역 수 ≤ 들어갈 수 있는 지역
        const int reachable = ReachableRegions();
        const uint64_t enchantReach = EnchantReach(snap.item);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const AchieveRule::Row& r = rows[i];
            if (r.kind == AchKind::eLevel && r.target > LevelTable::kMaxLevel)
                v.Fail("R138", where(i, "target"), "레벨 목표가 최고 레벨(" + N(LevelTable::kMaxLevel) + ")보다 높다");
            else if (r.kind == AchKind::eEnchant && r.target > enchantReach)
                v.Fail("R138", where(i, "target"), "강화가 +" + N(enchantReach) + "에서 멈춘다(Enchant 성공 몫 · 가호가 모두 0) — 목표 +" + N(r.target) + "는 닿지 않는다");
            else if (r.kind == AchKind::eZone && r.target > static_cast<uint32_t>(reachable))
                v.Fail("R138", where(i, "target"), "지역 목표가 들어갈 수 있는 지역 수(" + N(reachable) + " — 마을 지역 + 옛길 목적지)보다 크다");
        }

        // R139 — 묶음 · 단계: 같은 group은 step 1부터 빈틈없이 · 같은 분야 · 앞 단계와 같은 (kind, param)을 세면 target이 커진다
        //        (묶음 안에서 param이 오르는 연작 — 고급 → 희귀 → 영웅 장비 — 은 target이 같아도 된다)
        std::map<uint16_t, std::vector<size_t>> groups;
        for (size_t i = 0; i < rows.size(); ++i)
            groups[rows[i].group].push_back(i);
        for (auto& [group, members] : groups)
        {
            std::stable_sort(members.begin(), members.end(), [&](size_t l, size_t r) { return rows[l].step < rows[r].step; });
            const AchieveRule::Row& first = rows[members.front()];
            for (size_t k = 0; k < members.size(); ++k)
            {
                const size_t i = members[k];
                const AchieveRule::Row& r = rows[i];
                if (r.step != k + 1)
                {
                    v.Fail("R139", where(i, "step"), "묶음 " + N(group) + "의 단계는 1부터 빈틈없이 — " + N(k + 1) + "이어야 한다(지금 " + N(r.step) + ")");
                    break;
                }
                if (k == 0)
                    continue;
                const AchieveRule::Row& prev = rows[members[k - 1]];
                if (r.category != first.category)
                    v.Fail("R139", where(i, "category"), "묶음 " + N(group) + "의 업적은 같은 분야(단계 1과 다르다 — 한 줄로 이어 보인다)");
                else if (r.kind == prev.kind && r.param == prev.param && r.target <= prev.target)
                    v.Fail("R139", where(i, "target"), "묶음 " + N(group) + " 단계 " + N(r.step) + "의 목표(" + N(r.target) + ")가 같은 것을 세는 앞 단계(" +
                        N(prev.target) + ")보다 커야 한다");
            }
        }

        // R140 — 보상: 아이템이 있으면 표에 있고 화폐가 아니며 1 ≤ 개수 ≤ max_stack, 없으면 개수 0 · 그로시 ≤ 지갑 상한(20억)
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const AchieveRule::Row& r = rows[i];
            if (r.rewardAdena > AchieveRule::kMaxRewardAdena)
                v.Fail("R140", where(i, "reward_adena"), "보상 그로시 " + N(r.rewardAdena) + " > 지갑 상한 " + N(AchieveRule::kMaxRewardAdena) + " — 영영 받을 수 없다");
            if (r.rewardItem == ITEM_TABLE_ID_NONE)
            {
                if (r.rewardCount != 0)
                    v.Fail("R140", where(i, "reward_count"), "보상 아이템이 없으면 개수는 0 — 지금 " + N(r.rewardCount));
                continue;
            }
            const ItemTableRow* item = ItemTableData::Find(r.rewardItem);
            if (item == nullptr)
                v.Fail("R140", where(i, "reward_item"), "없는 아이템 " + N(r.rewardItem));
            else if (item->type == ItemType::eCurrency)
                v.Fail("R140", where(i, "reward_item"), "그로시 아이템은 보상 아이템이 아니다 — reward_adena에");
            else if (r.rewardCount < 1 || r.rewardCount > item->maxStack)
                v.Fail("R140", where(i, "reward_count"), "보상 개수는 1 ~ max_stack(" + N(item->maxStack) + ") — 한 칸에 들어가야 한다(지금 " + N(r.rewardCount) + ")");
        }
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpAchieve(Canon::Book& book, const GameDataSnapshot& snap)
    {
        Canon::WriteAchievements(book, snap.achieve.cat.rows);
    }
}
