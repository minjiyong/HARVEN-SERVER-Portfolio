#include "pch.h"
#include "Content/Skill/SkillGrow.h"
#include "Database/DBThread.h"
#include "User/User.h"
#include "Content/Skill/UserSkillHelper.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    using namespace SkillGrowth;

    bool Trace()
    {
        static const bool on = std::getenv("HARVEN_SKILL_TRACE") != nullptr;
        return on;
    }

    // 이 직업의 자라는 칸(QWERASDF 기본 배치)의 기술 id
    UInt16 SlotSkill(ClassId cls, int slot)
    {
        return SkillDefTable::Default(cls, static_cast<SkillKey>(static_cast<int>(SkillKey::Q) + slot));
    }

    bool OwnsGrowable(const UserSharedPtr& u, UInt16 id)
    {
        if (!Growable(id)) return false;
        const SkillRow* row = SkillTable::Find(id);
        return row != nullptr && row->cls == u->m_eClassId;
    }

    void Save(const UserSharedPtr& u, UInt16 id)
    {
        const State s = u->GrowState(id);
        DB_SKILL_INFO info;
        info.m_SkillId = id;
        info.m_Level = s.level;
        info.m_Pick1 = s.pick[0];
        info.m_Pick2 = s.pick[1];
        GDBThread->RequestSaveSkill(u->GetPlayerId(), info);
    }

    void SendAck(const UserSharedPtr& u, const SKILL_GROW_REQ_PACKET& req, SkillGrowResult r, UInt16 param, UInt16 skillId)
    {
        auto session = u->GetGameSession();
        if (!session) return;
        SKILL_GROW_ACK_PACKET p{};
        p.size = sizeof(p);
        p.type = static_cast<char>(PacketType::SKILL_GROW_ACK);
        p.seq = req.seq;
        p.op = req.op;
        p.result = static_cast<UInt8>(r);
        p.param = param;
        p.skill.skillId = skillId;
        p.skill.state = skillId < kIdSpan ? u->m_SkillGrow[skillId] : 0;
        p.points = SkillGrow::Points(u);
        session->PostSend(p);
        if (Trace())
            std::fprintf(stderr, "[skillgrow] seq=%u op=%u id=%u result=%u param=%u state=0x%02X points=%u\n",
                req.seq, req.op, skillId, static_cast<unsigned>(r), param, p.skill.state, p.points);
    }
}

namespace SkillGrow
{
    UInt16 Spent(const UserSharedPtr& u)
    {
        UInt32 spent = 0;
        for (int slot = 0; slot < kSlots; ++slot)
        {
            const UInt16 id = SlotSkill(u->m_eClassId, slot);
            if (id < kIdSpan) spent += CostTo(u->GrowState(id).level);
        }
        return static_cast<UInt16>(spent);
    }

    UInt16 Points(const UserSharedPtr& u)
    {
        const UInt16 total = PointsAt(u->GetStat()->GetLevel());
        const UInt16 spent = Spent(u);
        return total > spent ? static_cast<UInt16>(total - spent) : 0;
    }

    bool Load(const UserSharedPtr& u, const std::vector<DB_SKILL_INFO>& rows)
    {
        u->m_SkillGrow.fill(0);
        for (const DB_SKILL_INFO& r : rows)
        {
            // 다른 직업의 기록(직업을 바꿨던 캐릭터) · 범위 밖 값은 버린다
            if (!OwnsGrowable(u, r.m_SkillId) || r.m_Level < 1 || r.m_Level > kMaxLevel || r.m_Pick1 > 2 || r.m_Pick2 > 2)
                continue;
            State s;
            s.level = r.m_Level;
            s.pick[0] = r.m_Pick1;
            s.pick[1] = r.m_Pick2;
            u->m_SkillGrow[r.m_SkillId] = Pack(s);
        }
        if (Spent(u) > PointsAt(u->GetStat()->GetLevel()))
        {
            // 점수보다 많이 쓴 기록(수동 수정 · 규칙 변경) — 모두 돌려준다
            u->m_SkillGrow.fill(0);
            GDBThread->RequestResetSkills(u->GetPlayerId());
            return true;
        }
        return false;
    }

    void SendTree(const UserSharedPtr& u)
    {
        if (!u) return;
        auto session = u->GetGameSession();
        if (!session) return;
        SKILL_TREE_INF_PACKET p{};
        p.size = sizeof(p);
        p.type = static_cast<char>(PacketType::SKILL_TREE_INF);
        p.total = PointsAt(u->GetStat()->GetLevel());
        p.points = Points(u);
        p.count = 0;
        for (int slot = 0; slot < kSlots; ++slot)
        {
            const UInt16 id = SlotSkill(u->m_eClassId, slot);
            if (id == 0 || id >= kIdSpan) continue;
            p.skills[p.count].skillId = id;
            p.skills[p.count].state = u->m_SkillGrow[id];
            ++p.count;
        }
        session->PostSend(p);
    }

    void HandleRequest(const UserSharedPtr& u, const SKILL_GROW_REQ_PACKET& req)
    {
        if (!u) return;
        // 재전송(같은 seq): 다시 처리하지 않고 지금 상태로 결과만
        if (u->m_bGrowSeqSeen && req.seq == u->m_LastGrowSeq)
        {
            SendAck(u, req, SkillGrowResult::Ok, 0, req.skillId < kIdSpan ? req.skillId : 0);
            return;
        }
        const auto op = static_cast<SkillGrowOp>(req.op);
        auto done = [&](SkillGrowResult r, UInt16 param, UInt16 id)
        {
            u->m_LastGrowSeq = req.seq;
            u->m_bGrowSeqSeen = true;
            SendAck(u, req, r, param, id);
        };
        const UInt32 now = GetNowTime();
        if (u->GetStat()->IsDead() || u->IsTransferring() || u->GetSectorX() < 0)
            return done(SkillGrowResult::Busy, 0, 0);
        // 싸우는 중 · 시전 중에는 바꾸지 않는다(시전 중인 기술의 표가 바뀌지 않게 — 예약된 타격은 시전 때 상태를 들고 있다)
        if (now - u->m_LastCombatAt < 5000 || CUserSkillHelper::IsChanneling(u))
            return done(SkillGrowResult::InCombat, static_cast<UInt16>((5000 - (std::min<UInt32>)(5000, now - u->m_LastCombatAt)) / 100), 0);

        if (op == SkillGrowOp::ResetAll)
        {
            u->m_SkillGrow.fill(0);
            GDBThread->RequestResetSkills(u->GetPlayerId());
            done(SkillGrowResult::Ok, 0, 0);
            SendTree(u);
            return;
        }

        const UInt16 id = req.skillId;
        if (!OwnsGrowable(u, id))
            return done(SkillGrowResult::Unknown, 0, 0);
        const SkillRow* row = SkillTable::Find(id);
        if (u->GetStat()->GetLevel() < row->unlockLevel)
            return done(SkillGrowResult::Locked, row->unlockLevel, id);

        State s = u->GrowState(id);
        switch (op)
        {
        case SkillGrowOp::LevelUp:
        {
            if (s.level >= kMaxLevel) return done(SkillGrowResult::MaxLevel, kMaxLevel, id);
            const UInt8 cost = StepCost(s.level);
            if (Points(u) < cost) return done(SkillGrowResult::NoPoints, cost, id);
            ++s.level;
            break;
        }
        case SkillGrowOp::LevelDown:
        {
            if (s.level <= 1) return done(SkillGrowResult::MinLevel, 1, id);
            --s.level;
            // 갈래 레벨 아래로 내려가면 고른 갈래를 끈다(다시 올리면 다시 고른다)
            for (int tier = 0; tier < 2; ++tier)
                if (s.level < TierLevel(tier)) s.pick[tier] = 0;
            break;
        }
        case SkillGrowOp::Pick:
        {
            if (req.tier > 1 || req.choice > 2) return done(SkillGrowResult::Unknown, 0, id);
            if (s.level < TierLevel(req.tier)) return done(SkillGrowResult::TierLocked, TierLevel(req.tier), id);
            s.pick[req.tier] = req.choice;
            break;
        }
        default:
            return done(SkillGrowResult::Unknown, 0, id);
        }
        u->m_SkillGrow[id] = Pack(s);
        Save(u, id);
        done(SkillGrowResult::Ok, 0, id);
    }
}
