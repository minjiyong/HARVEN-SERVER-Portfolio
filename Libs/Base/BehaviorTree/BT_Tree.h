#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "Base/BehaviorTree/BT_Task.h"

// ─────────────────────────────────────────────────────────────────────────────
// BT_Tree<TAgent> — 불변 트리 정의(노드 배열). BT_Builder가 만들고, 같은 종류의 개체가 모두 함께 쓴다.
// BT_Instance<N> — 개체 하나의 노드 상태(노드 N개까지, 노드마다 8바이트). 개체가 깨어 있는 동안만 들고 있으면 된다.
//
// 틱 규칙(REIW BT_Task::Tick · BT_Selector · BT_Sequence와 같다):
//  · 잎: 달리는 중이 아니면 OnEnter(실패 · 강제 종료면 OnLeave 후 그 결과) → OnUpdate → 끝났으면 OnLeave.
//  · Sequence · Selector는 달리던 자식부터 이어서 돈다. 끝나면(성공 · 실패) 처음으로 돌아간다.
//  · 결과가 끝(성공 · 실패)인 노드는 다음 틱에 처음부터(OnEnter) 다시 시작한다.
// 재귀 깊이 = 트리 깊이(몬스터 트리는 6단 안팎).
// ─────────────────────────────────────────────────────────────────────────────

template <size_t N>
class BT_Instance
{
public:
    static constexpr size_t kCapacity = N;

    void Reset() noexcept { m_States.fill(BT_NodeState{}); }

    [[nodiscard]] BT_NodeState* Data() noexcept { return m_States.data(); }
    [[nodiscard]] const BT_NodeState& At(size_t index) const noexcept { return m_States[index]; }
    [[nodiscard]] BT_NodeState& At(size_t index) noexcept { return m_States[index]; }

private:
    std::array<BT_NodeState, N> m_States{};
};

template <typename TAgent>
class BT_Builder;

template <typename TAgent>
class BT_Tree
{
public:
    using Predicate = bool (*)(TAgent&, const BT_Param&);
    static constexpr UInt16 kNone = 0xFFFF;

    struct Node
    {
        EnumCompositeType          type = EnumCompositeType::Task;
        UInt8                      weight = 1;          // 부모가 SelectorRandom일 때 뽑힐 몫
        UInt16                     firstChild = kNone;
        UInt16                     nextSibling = kNone;
        UInt16                     childCount = 0;
        BT_Param                   param{};
        const BT_TaskDef<TAgent>*  task = nullptr;      // Task
        Predicate                  predicate = nullptr; // Condition
    };

    [[nodiscard]] const std::string& Name() const noexcept { return m_Name; }
    [[nodiscard]] size_t Size() const noexcept { return m_Nodes.size(); }
    [[nodiscard]] const Node& NodeAt(size_t index) const noexcept { return m_Nodes[index]; }

    // 뿌리부터 한 번 돈다. 뿌리의 결과를 돌려준다(Running이면 Agent::WakeAt으로 남긴 시각에 다시 부른다).
    template <size_t N>
    EnumTaskStatus Tick(TAgent& agent, BT_Instance<N>& run) const
    {
        static_assert(N > 0);
        if (m_Nodes.empty() || m_Nodes.size() > N)
            return EnumTaskStatus::Failure;   // 만들 때(BT_Builder::Build) 이미 막는다 — 여기는 방어
        return TickNode(agent, run.Data(), 0);
    }

    // 달리던 가지를 모두 끊는다(잎은 OnLeave(Invalid)) — 개체를 재우거나 없앨 때. 그 뒤 상태는 처음과 같다.
    template <size_t N>
    void Abort(TAgent& agent, BT_Instance<N>& run) const
    {
        if (m_Nodes.empty() || m_Nodes.size() > N)
            return;
        AbortNode(agent, run.Data(), 0);
    }

private:
    friend class BT_Builder<TAgent>;

    [[nodiscard]] static bool Due(UInt32 now, UInt32 at) noexcept { return static_cast<Int32>(now - at) >= 0; }

    [[nodiscard]] UInt16 ChildAt(const Node& n, UInt16 index) const noexcept
    {
        UInt16 c = n.firstChild;
        for (UInt16 i = 0; i < index && c != kNone; ++i)
            c = m_Nodes[c].nextSibling;
        return c;
    }

    EnumTaskStatus TickTask(TAgent& agent, const Node& n, BT_NodeState& st, bool resume) const
    {
        BT_Frame frame{ n.param, st };
        if (!resume)
        {
            const EnumTaskStatus enter = n.task->onEnter != nullptr ? n.task->onEnter(agent, frame) : EnumTaskStatus::Success;
            if (enter == EnumTaskStatus::Failure || enter == EnumTaskStatus::ForceShutdown)
            {
                if (n.task->onLeave != nullptr)
                    n.task->onLeave(agent, frame, enter);
                return enter;
            }
        }
        const EnumTaskStatus update = n.task->onUpdate != nullptr ? n.task->onUpdate(agent, frame) : EnumTaskStatus::Success;
        if (update != EnumTaskStatus::Running && n.task->onLeave != nullptr)
            n.task->onLeave(agent, frame, update);
        return update;
    }

    EnumTaskStatus TickNode(TAgent& agent, BT_NodeState* s, UInt16 index) const
    {
        const Node& n = m_Nodes[index];
        BT_NodeState& st = s[index];
        const bool resume = st.status == EnumTaskStatus::Running;
        if (!resume)
        {
            st.cursor = 0;
            st.count = 0;
            if (n.type != EnumCompositeType::Cooldown)   // Cooldown의 time은 "다시 쓸 수 있는 시각" — 다시 시작해도 지운다면 쿨다운이 아니다
                st.time = 0;
        }

        EnumTaskStatus result = EnumTaskStatus::Failure;
        switch (n.type)
        {
        case EnumCompositeType::Task:
            result = TickTask(agent, n, st, resume);
            break;

        case EnumCompositeType::Sequence:
        case EnumCompositeType::Selector:
        {
            // Sequence는 성공이면 다음, Selector는 실패면 다음 — 그 밖의 결과에서 멈춘다
            const EnumTaskStatus pass = n.type == EnumCompositeType::Sequence ? EnumTaskStatus::Success : EnumTaskStatus::Failure;
            result = pass;
            UInt16 i = st.cursor;
            for (UInt16 c = ChildAt(n, st.cursor); c != kNone; c = m_Nodes[c].nextSibling, ++i)
            {
                const EnumTaskStatus r = TickNode(agent, s, c);
                if (r == pass)
                    continue;
                if (r == EnumTaskStatus::Running)
                    st.cursor = static_cast<UInt8>(i);
                result = r;
                break;
            }
            break;
        }

        case EnumCompositeType::ReactiveSelector:
        {
            // 매번 처음부터. 이긴(실패가 아닌) 자식보다 뒤에서 달리던 자식은 끊는다.
            const UInt16 running = resume ? st.cursor : kNone;
            result = EnumTaskStatus::Failure;
            UInt16 i = 0;
            for (UInt16 c = n.firstChild; c != kNone; c = m_Nodes[c].nextSibling, ++i)
            {
                const EnumTaskStatus r = TickNode(agent, s, c);
                if (r == EnumTaskStatus::Failure)
                    continue;
                if (running != kNone && running > i)
                    AbortNode(agent, s, ChildAt(n, running));
                if (r == EnumTaskStatus::Running)
                    st.cursor = static_cast<UInt8>(i);
                result = r;
                break;
            }
            break;
        }

        case EnumCompositeType::SelectorRandom:
        {
            // 달리던 자식이 있으면 그것부터(이어서). 끝났거나 실패면 나머지를 가중치 무작위 순서로.
            UInt16 tried = 0;   // 비트 i = 이번 틱에 이미 돌려 본 자식(자식은 16개까지 — 만들 때 막는다)
            result = EnumTaskStatus::Failure;
            if (resume)
            {
                const UInt16 c = ChildAt(n, st.cursor);
                const EnumTaskStatus r = TickNode(agent, s, c);
                tried = static_cast<UInt16>(1u << st.cursor);
                if (r != EnumTaskStatus::Failure)
                {
                    result = r;
                    break;
                }
            }
            for (;;)
            {
                UInt32 total = 0;
                UInt16 i = 0;
                for (UInt16 c = n.firstChild; c != kNone; c = m_Nodes[c].nextSibling, ++i)
                    if ((tried & (1u << i)) == 0)
                        total += m_Nodes[c].weight;
                if (total == 0)
                    break;
                UInt32 roll = agent.Random(total);
                i = 0;
                UInt16 pick = kNone, pickIndex = 0;
                for (UInt16 c = n.firstChild; c != kNone; c = m_Nodes[c].nextSibling, ++i)
                {
                    if ((tried & (1u << i)) != 0)
                        continue;
                    if (roll < m_Nodes[c].weight)
                    {
                        pick = c;
                        pickIndex = i;
                        break;
                    }
                    roll -= m_Nodes[c].weight;
                }
                if (pick == kNone)
                    break;
                tried = static_cast<UInt16>(tried | (1u << pickIndex));
                const EnumTaskStatus r = TickNode(agent, s, pick);
                if (r == EnumTaskStatus::Failure)
                    continue;
                if (r == EnumTaskStatus::Running)
                    st.cursor = static_cast<UInt8>(pickIndex);
                result = r;
                break;
            }
            break;
        }

        case EnumCompositeType::Parallel:
        {
            // 모두 돌리고 첫 자식(주 행동)의 결과를 쓴다. 곁 행동은 끝나도 다음 틱에 다시 시작한다(끝없이 곁에서 돈다).
            result = EnumTaskStatus::Running;
            bool first = true;
            for (UInt16 c = n.firstChild; c != kNone; c = m_Nodes[c].nextSibling)
            {
                const EnumTaskStatus r = TickNode(agent, s, c);
                if (first)
                {
                    result = r;
                    first = false;
                }
                else if (r == EnumTaskStatus::ForceShutdown)
                {
                    result = r;
                }
            }
            if (result != EnumTaskStatus::Running)
                for (UInt16 c = n.firstChild; c != kNone; c = m_Nodes[c].nextSibling)
                    AbortNode(agent, s, c);
            break;
        }

        case EnumCompositeType::Inverter:
        case EnumCompositeType::Succeeder:
        case EnumCompositeType::Failurer:
        {
            const EnumTaskStatus r = TickNode(agent, s, n.firstChild);
            result = r;
            if (r == EnumTaskStatus::Success || r == EnumTaskStatus::Failure)
            {
                if (n.type == EnumCompositeType::Inverter)
                    result = r == EnumTaskStatus::Success ? EnumTaskStatus::Failure : EnumTaskStatus::Success;
                else
                    result = n.type == EnumCompositeType::Succeeder ? EnumTaskStatus::Success : EnumTaskStatus::Failure;
            }
            break;
        }

        case EnumCompositeType::Condition:
        {
            if (n.predicate != nullptr && n.predicate(agent, n.param))
            {
                result = TickNode(agent, s, n.firstChild);
            }
            else
            {
                if (resume)
                    AbortNode(agent, s, n.firstChild);
                result = EnumTaskStatus::Failure;
            }
            break;
        }

        case EnumCompositeType::Cooldown:
        {
            const UInt32 now = agent.Now();
            if (!resume && st.time != 0)
            {
                if (!Due(now, st.time))
                {
                    agent.WakeAt(st.time);
                    result = EnumTaskStatus::Failure;
                    break;
                }
                st.time = 0;   // 풀렸다 — 지운다(시계가 한 바퀴 돌아 오래된 시각이 미래로 보이는 일이 없게)
            }
            result = TickNode(agent, s, n.firstChild);
            if (result == EnumTaskStatus::Success)
            {
                st.time = agent.Now() + static_cast<UInt32>(n.param.a > 0 ? n.param.a : 0);
                if (st.time == 0)
                    st.time = 1;   // 0은 "쿨다운 없음" 표시라 피한다
            }
            break;
        }

        case EnumCompositeType::TimeLimit:
        {
            const UInt32 now = agent.Now();
            if (!resume)
                st.time = now + static_cast<UInt32>(n.param.a > 0 ? n.param.a : 0);
            else if (Due(now, st.time))
            {
                AbortNode(agent, s, n.firstChild);
                result = EnumTaskStatus::Failure;
                break;
            }
            result = TickNode(agent, s, n.firstChild);
            if (result == EnumTaskStatus::Running)
                agent.WakeAt(st.time);
            break;
        }
        }

        st.status = result;
        return result;
    }

    void AbortNode(TAgent& agent, BT_NodeState* s, UInt16 index) const
    {
        if (index == kNone)
            return;
        BT_NodeState& st = s[index];
        if (st.status != EnumTaskStatus::Running)
            return;
        const Node& n = m_Nodes[index];
        if (n.type == EnumCompositeType::Task)
        {
            if (n.task->onLeave != nullptr)
            {
                BT_Frame frame{ n.param, st };
                n.task->onLeave(agent, frame, EnumTaskStatus::Invalid);
            }
        }
        else
        {
            for (UInt16 c = n.firstChild; c != kNone; c = m_Nodes[c].nextSibling)
                AbortNode(agent, s, c);
        }
        st.status = EnumTaskStatus::Invalid;
    }

    std::string        m_Name;
    std::vector<Node>  m_Nodes;
};
