#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 퀘스트 묶음 — 탭 Quest · QuestObjective · Poi · Chapter(GameData/*.json) → 스냅샷 → 파사드 QuestData::*.
// 모양은 Bind_Monster.cpp(모범 표)를 따른다: 1) 파사드 2) Bind 3) Validate(R037~R063) 4) Dump.
//
// 목표 objs[kMaxObjs]는 자식 탭 QuestObjective(quest_id, seq 1~3). 표에 없는 칸은 기본값(Objective{} — kind None, count 0).
// Validate는 다른 묶음(몬스터 · 아이템 · NPC · 마을 · 지형)을 파사드로만 본다 — 그 묶음이 GameData로 옮겨 가도 그대로 돈다.
// ─────────────────────────────────────────────────────────────────────────────
#include <string>

#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Quest.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/LevelTable.h"
#include "Database/GameData/MonsterTable.h"
#include "Database/GameData/QuestData.h"
#include "Database/GameData/WorldData.h"
#include "Database/GameData/Item/ItemTableData.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace QuestData
{
    std::span<const QuestRow> Quests() noexcept { return GD().quest.rows; }

    size_t QuestCount() noexcept { return GD().quest.rows.size(); }

    std::span<const Poi> Pois() noexcept { return GD().quest.pois; }

    size_t PoiCount() noexcept { return GD().quest.pois.size(); }

    std::span<const Chapter> Chapters() noexcept { return GD().quest.chapters; }

    // 색인은 "id가 처음 나온 행" — 이관 전 constexpr 표의 순차 검색(첫 일치)과 같은 답. 색인 밖 id는 순차 검색.
    const QuestRow* Find(uint16_t id) noexcept
    {
        const SnapQuest& s = GD().quest;
        if (id < SnapQuest::kIndex)
            return s.byId[id] >= 0 ? &s.rows[static_cast<size_t>(s.byId[id])] : nullptr;
        for (const auto& q : s.rows)
            if (q.id == id)
                return &q;
        return nullptr;
    }

    // 장소는 9곳 남짓 — 이관 전과 같은 순차 검색(첫 일치)
    const Poi* FindPoi(uint16_t id) noexcept
    {
        for (const auto& p : GD().quest.pois)
            if (p.id == id)
                return &p;
        return nullptr;
    }
}

namespace
{
    using namespace GameDataLoad;
    using QuestData::ObjKind;

    constexpr const char* kRewardItemCol[3] = { "reward_item_knight", "reward_item_engraver", "reward_item_hunter" };

    // 목표 칸의 오류 위치 — 표에서 온 칸이면 QuestObjective 행, 빈 칸(기본값)이면 그 퀘스트 행
    std::string ObjWhere(const ValidateContext& v, const SnapQuest& s, size_t qi, int slot, const char* col)
    {
        const int16_t src = s.objSrc[qi][static_cast<size_t>(slot)];
        return src >= 0 ? v.Where("QuestObjective", static_cast<size_t>(src), col) : v.Where("Quest", qi, "id");
    }

    // 예전 QuestData::Detail::ValidObjective — 규칙 R038~R046(묶음 호출 R058)
    void ValidateObjective(ValidateContext& v, const SnapQuest& s, size_t qi, int slot)
    {
        const QuestData::QuestRow& q = s.rows[qi];
        const QuestData::Objective& o = q.objs[slot];
        const std::string tag = "퀘스트 " + std::to_string(q.id) + " 목표 " + std::to_string(slot + 1);
        auto fail = [&](const char* rule, const char* col, const std::string& why) { v.Fail(rule, ObjWhere(v, s, qi, slot, col), tag + ": " + why); };
        auto monster = [](uint16_t id) { return MonsterTable::Find(id); };

        if (o.zone != QuestData::kAnyZone && o.zone >= WorldData::kRegionCount)
            fail("R038", "zone", "zone " + std::to_string(o.zone) + " — 255(어디서나) 또는 0~" + std::to_string(WorldData::kRegionCount - 1));
        switch (o.kind)
        {
        case ObjKind::None:   // R039 — 구조로 보장(표에 None 행은 Bind가 거부, 빈 칸은 기본값 count 0). 이관 전 규칙을 그대로 둔다.
            if (o.count != 0) fail("R039", "count", "빈 목표(None)의 count는 0");
            break;
        case ObjKind::Kill:
            if (o.count == 0) fail("R040", "count", "Kill 목표의 count는 1 이상");
            if (monster(o.target) == nullptr) fail("R040", "target", "Kill 대상 몬스터 " + std::to_string(o.target) + "가 Monster에 없다");
            break;
        case ObjKind::KillIn:
            if (o.count == 0) fail("R041", "count", "KillIn 목표의 count는 1 이상");
            if (o.zone == QuestData::kAnyZone) fail("R041", "zone", "KillIn 목표는 지역(zone)을 정해야 한다(255 불가)");
            break;
        case ObjKind::Collect:
            if (o.count == 0) fail("R042", "count", "Collect 목표의 count는 1 이상");
            if (monster(o.target) == nullptr) fail("R042", "target", "Collect 대상 몬스터 " + std::to_string(o.target) + "가 Monster에 없다");
            if (o.chance == 0 || o.chance > 100) fail("R042", "chance", "Collect 확률은 1~100% — " + std::to_string(o.chance));
            if (o.label == nullptr) fail("R042", "label", "Collect 목표는 수집물 이름(label)이 있어야 한다");
            if (o.icon == QuestData::QuestIcon::None) fail("R042", "icon", "Collect 목표는 아이콘이 있어야 한다(None 불가)");
            break;
        case ObjKind::Talk:
            if (o.count != 1) fail("R043", "count", "Talk 목표의 count는 1");
            if (WorldData::FindNpc(o.target) == nullptr) fail("R043", "target", "Talk 대상 NPC " + std::to_string(o.target) + "가 Npc에 없다");
            break;
        case ObjKind::Explore:
            if (o.count != 1) fail("R044", "count", "Explore 목표의 count는 1");
            if (QuestData::FindPoi(o.target) == nullptr) fail("R044", "target", "Explore 대상 장소 " + std::to_string(o.target) + "가 Poi에 없다");
            break;
        case ObjKind::Boss:
        {
            if (o.count != 1) fail("R045", "count", "Boss 목표의 count는 1");
            const MonsterTableRow* m = monster(o.target);
            if (m == nullptr || !m->boss)
                fail("R045", "target", "Boss 대상 " + std::to_string(o.target) + (m == nullptr ? "가 Monster에 없다" : "는 보스가 아니다(Monster.boss)"));
            break;
        }
        case ObjKind::Equip:
            if (o.count == 0 || o.count > 8) fail("R046", "count", "Equip 목표의 count(부위 수)는 1~8 — " + std::to_string(o.count));
            break;
        default:
            fail("R058", "kind", "모르는 목표 종류");
            break;
        }
    }
}

namespace GameDataLoad
{
    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindQuest(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapQuest& s = snap.quest;
        s.byId.fill(-1);

        // Poi
        if (const Json* rows = ctx.Rows("Poi"))
        {
            s.pois.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Poi", i, (*rows)[i]);
                QuestData::Poi p{};
                r.Int("id", p.id).Str("name", p.name).Int("x", p.x).Int("y", p.y).Int("radius", p.radius).Str("flavor", p.flavor);
                r.Finish();
                s.pois.push_back(p);
            }
        }

        // Chapter — 행 i = 장 i(0번 빈 장 포함). 행 수는 구조 상수 kChapterCount + 1(넘거나 모자라면 로드 실패)
        if (const Json* rows = ctx.Rows("Chapter"))
        {
            for (size_t i = 0; i < rows->size(); ++i)
            {
                const size_t before = ctx.ErrorCount();
                RowReader r(ctx, "Chapter", i, (*rows)[i]);
                uint8_t id = 0;
                QuestData::Chapter c{};
                r.Int("id", id).Str("title", c.title).Str("place", c.place).Str("line", c.line);
                r.Finish();
                if (ctx.ErrorCount() == before && id != i)
                    ctx.Error(r.Where("id"), "장 id는 0부터 연속(행 " + std::to_string(i) + "번째 → id " + std::to_string(id) + ")");
                s.chapters.push_back(c);
            }
            if (rows->size() != static_cast<size_t>(QuestData::kChapterCount) + 1)
                ctx.Error("Chapter", "행 수 " + std::to_string(rows->size()) + " ≠ " + std::to_string(QuestData::kChapterCount + 1) +
                    "(구조 상수 QuestData::kChapterCount + 0번 빈 장)");
        }

        // Quest — 목표는 아래 QuestObjective에서
        if (const Json* rows = ctx.Rows("Quest"))
        {
            s.rows.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Quest", i, (*rows)[i]);
                QuestData::QuestRow q{};
                r.Int("id", q.id)
                 .Enum("kind", q.kind)
                 .Int("chapter", q.chapter)
                 .Str("name", q.name)
                 .Int("giver", q.giver)
                 .Int("turn_in", q.turnIn)
                 .Int("min_level", q.minLevel)
                 .Int("prereq", q.prereq)
                 .Mask<ClassId>("class_mask", q.classMask)
                 .Str("summary", q.summary)
                 .Str("offer", q.offer)
                 .Str("during", q.during)
                 .Str("done", q.done)
                 .Int("reward_exp", q.reward.exp)
                 .Int("reward_exp_pct", q.reward.expPct)
                 .Int("reward_adena", q.reward.adena)
                 .Int(kRewardItemCol[0], q.reward.item[0])
                 .Int(kRewardItemCol[1], q.reward.item[1])
                 .Int(kRewardItemCol[2], q.reward.item[2])
                 .Int("reward_count", q.reward.count)
                 .Enum("reward_rarity", q.reward.rarity)
                 .Int("cooldown_min", q.cooldownMin);
                r.Finish();
                if (q.id < SnapQuest::kIndex && s.byId[q.id] < 0)
                    s.byId[q.id] = static_cast<int16_t>(s.rows.size());
                s.rows.push_back(q);
            }
        }
        s.objSrc.assign(s.rows.size(), { -1, -1, -1 });

        // QuestObjective — (quest_id, seq 1~kMaxObjs) → objs[seq-1]. 칸 수는 구조 상수(넘으면 로드 실패, 자르지 않는다)
        if (const Json* rows = ctx.Rows("QuestObjective"))
        {
            for (size_t i = 0; i < rows->size(); ++i)
            {
                const size_t before = ctx.ErrorCount();
                RowReader r(ctx, "QuestObjective", i, (*rows)[i]);
                uint16_t questId = 0;
                uint8_t seq = 0;
                QuestData::Objective o{};
                r.Int("quest_id", questId)
                 .Int("seq", seq)
                 .Enum("kind", o.kind)
                 .Int("target", o.target)
                 .Int("count", o.count)
                 .Int("zone", o.zone)
                 .Int("chance", o.chance)
                 .StrOpt("label", o.label)
                 .Enum("icon", o.icon);
                r.Finish();
                if (ctx.ErrorCount() != before)
                    continue;
                // R039(구조로 보장): 빈 목표는 행을 두지 않는다 — None 행은 거부(빈 칸은 기본값 Objective{} = None · count 0)
                if (o.kind == ObjKind::None)
                {
                    ctx.Error(r.Where("kind"), "목표 종류 None은 표에 쓰지 않는다 — 목표가 없으면 행을 두지 않는다", "R039");
                    continue;
                }
                if (seq < 1 || seq > QuestData::kMaxObjs)
                {
                    ctx.Error(r.Where("seq"), "seq는 1~" + std::to_string(QuestData::kMaxObjs) + "(구조 상수 QuestData::kMaxObjs) — " + std::to_string(seq));
                    continue;
                }
                size_t qi = s.rows.size();
                if (questId < SnapQuest::kIndex)
                    qi = s.byId[questId] >= 0 ? static_cast<size_t>(s.byId[questId]) : s.rows.size();
                else
                    for (size_t k = 0; k < s.rows.size(); ++k)
                        if (s.rows[k].id == questId) { qi = k; break; }
                if (qi == s.rows.size())
                {
                    ctx.Error(r.Where("quest_id"), "Quest에 없는 퀘스트 " + std::to_string(questId));
                    continue;
                }
                int16_t& src = s.objSrc[qi][static_cast<size_t>(seq - 1)];
                if (src >= 0)
                {
                    ctx.Error(r.Where("seq"), "퀘스트 " + std::to_string(questId) + " 목표 seq " + std::to_string(seq) + "가 겹친다(" + ctx.Where("QuestObjective", static_cast<size_t>(src)) + ")");
                    continue;
                }
                s.rows[qi].objs[static_cast<size_t>(seq - 1)] = o;
                src = static_cast<int16_t>(i);
            }
            // seq는 퀘스트마다 1부터 연속 — 빈 칸 뒤에 목표가 있으면 오류(이관 전 표에는 그런 행이 없다: 덤프가 확인)
            for (size_t qi = 0; qi < s.rows.size(); ++qi)
            {
                bool ended = false;
                for (int k = 0; k < QuestData::kMaxObjs; ++k)
                {
                    const int16_t src = s.objSrc[qi][static_cast<size_t>(k)];
                    if (src < 0) { ended = true; continue; }
                    if (ended)
                    {
                        ctx.Error(ctx.Where("QuestObjective", static_cast<size_t>(src), "seq"),
                            "퀘스트 " + std::to_string(s.rows[qi].id) + "의 목표 seq가 1부터 연속이 아니다(" + std::to_string(k + 1) + " 앞이 비었다)");
                        break;
                    }
                }
            }
        }
    }

    // ── 3) Validate — 예전 QuestData::Detail::Validate()(static_assert R063)의 규칙 R037~R062 ──
    void ValidateQuest(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const SnapQuest& s = snap.quest;

        // 장소 — R037(묶음 호출 R047) · R048
        for (size_t i = 0; i < s.pois.size(); ++i)
        {
            const QuestData::Poi& p = s.pois[i];
            const std::string at = "장소 " + std::to_string(p.id) + "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
            if (p.radius == 0)
                v.Fail("R037", v.Where("Poi", i, "radius"), at + ": 반경은 1 이상");
            if (WorldData::IsBlocked(p.x, p.y))
                v.Fail("R037", v.Where("Poi", i, "x"), at + ": 막힌 칸 — 걸을 수 있는 칸이어야 한다");
            if (WorldData::IsInTown(p.x, p.y))
                v.Fail("R037", v.Where("Poi", i, "x"), at + ": 마을 안 — 탐험 장소는 마을 밖");
            for (size_t j = 0; j < i; ++j)
                if (s.pois[j].id == p.id)
                {
                    v.Fail("R048", v.Where("Poi", i, "id"), "장소 id " + std::to_string(p.id) + "가 겹친다(" + v.Where("Poi", j) + ")");
                    break;
                }
        }

        // 퀘스트 — R049~R060(목표는 R058 → R038~R046)
        for (size_t i = 0; i < s.rows.size(); ++i)
        {
            const QuestData::QuestRow& q = s.rows[i];
            const std::string tag = "퀘스트 " + std::to_string(q.id);
            if (q.id == 0 || q.id > 255)
                v.Fail("R049", v.Where("Quest", i, "id"), tag + ": id는 1~255(완료 기록이 id 비트로 저장된다)");
            for (size_t j = 0; j < i; ++j)
                if (s.rows[j].id == q.id)
                {
                    v.Fail("R050", v.Where("Quest", i, "id"), tag + ": id가 겹친다(" + v.Where("Quest", j) + ")");
                    break;
                }
            if (WorldData::FindNpc(q.giver) == nullptr)
                v.Fail("R051", v.Where("Quest", i, "giver"), tag + ": 주는 NPC " + std::to_string(q.giver) + "가 Npc에 없다");
            if (q.turnIn != 0 && WorldData::FindNpc(q.turnIn) == nullptr)
                v.Fail("R052", v.Where("Quest", i, "turn_in"), tag + ": 보고할 NPC " + std::to_string(q.turnIn) + "가 Npc에 없다");
            if (q.turnIn == 0 && QuestData::ObjCount(q) == 0)
                v.Fail("R053", v.Where("Quest", i, "turn_in"), tag + ": 보고할 NPC도 목표도 없으면 끝낼 수 없다");
            if (q.prereq != 0 && (q.prereq == q.id || QuestData::Find(q.prereq) == nullptr))
                v.Fail("R054", v.Where("Quest", i, "prereq"), tag + ": 선행 퀘스트 " + std::to_string(q.prereq) + (q.prereq == q.id ? "가 자기 자신이다" : "가 Quest에 없다"));
            if (q.minLevel < 1 || q.minLevel > LevelTable::kMaxLevel)
                v.Fail("R055", v.Where("Quest", i, "min_level"), tag + ": 최소 레벨 " + std::to_string(q.minLevel) + " — 1~" + std::to_string(LevelTable::kMaxLevel));
            if ((q.kind == QuestData::QuestKind::Main) != (q.chapter != 0) || q.chapter > QuestData::kChapterCount)
                v.Fail("R056", v.Where("Quest", i, "chapter"), tag + ": 장 " + std::to_string(q.chapter) + " — 메인(Main)만 1~" +
                    std::to_string(QuestData::kChapterCount) + ", 그 밖은 0");
            if ((q.kind == QuestData::QuestKind::Daily) != (q.cooldownMin > 0))
                v.Fail("R057", v.Where("Quest", i, "cooldown_min"), tag + ": 다시 받기까지 " + std::to_string(q.cooldownMin) + "분 — 반복 의뢰(Daily)만 > 0");
            for (int k = 0; k < QuestData::kMaxObjs; ++k)
                ValidateObjective(v, s, i, k);
            for (int c = 0; c < 3; ++c)
            {
                const uint16_t item = q.reward.item[c];
                if (item != 0 && ItemTableData::Find(item) == nullptr)
                    v.Fail("R059", v.Where("Quest", i, kRewardItemCol[c]), tag + ": 보상 아이템 " + std::to_string(item) + "가 Item에 없다");
            }
            if (q.reward.count == 0)
                v.Fail("R060", v.Where("Quest", i, "reward_count"), tag + ": 보상 개수는 1 이상");
        }

        // R061: 메인 이야기는 id 순서대로 이어진다(앞 메인 퀘스트가 곧 선행 퀘스트) — 표 행 순서(= id 순)로
        uint16_t prevMain = 0;
        for (size_t i = 0; i < s.rows.size(); ++i)
        {
            const QuestData::QuestRow& q = s.rows[i];
            if (q.kind != QuestData::QuestKind::Main) continue;
            if (q.prereq != prevMain)
                v.Fail("R061", v.Where("Quest", i, "prereq"), "메인 퀘스트 " + std::to_string(q.id) + "의 선행 퀘스트는 앞 메인 퀘스트 " +
                    std::to_string(prevMain) + "여야 한다(지금 " + std::to_string(q.prereq) + ")");
            prevMain = q.id;
        }

        // R062: 마지막 퀘스트(kFinalQuest — 엔딩 · 전 서버 공지)는 메인이고 목표를 이루는 순간 끝난다(turn_in = 0). 같은 id가 여럿이면 마지막 행(이관 전과 같다)
        size_t finalAt = s.rows.size();
        for (size_t i = 0; i < s.rows.size(); ++i)
            if (s.rows[i].id == QuestData::kFinalQuest) finalAt = i;
        if (finalAt == s.rows.size())
            v.Fail("R062", "Quest", "마지막 퀘스트 " + std::to_string(QuestData::kFinalQuest) + "(QuestData::kFinalQuest)가 없다");
        else if (s.rows[finalAt].kind != QuestData::QuestKind::Main)
            v.Fail("R062", v.Where("Quest", finalAt, "kind"), "마지막 퀘스트 " + std::to_string(QuestData::kFinalQuest) + "는 메인(Main)이어야 한다");
        else if (s.rows[finalAt].turnIn != 0)
            v.Fail("R062", v.Where("Quest", finalAt, "turn_in"), "마지막 퀘스트 " + std::to_string(QuestData::kFinalQuest) + "는 보고할 NPC 없이(turn_in = 0) 끝난다");
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpQuest(Canon::Book& book, const GameDataSnapshot& snap)
    {
        const SnapQuest& s = snap.quest;
        Canon::WriteQuest(book, s.rows);
        Canon::WritePoi(book, s.pois);
        Canon::WriteChapter(book, s.chapters);
    }
}
