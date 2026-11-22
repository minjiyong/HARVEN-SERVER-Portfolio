#pragma once

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Base/BehaviorTree/BT_Tree.h"

// ─────────────────────────────────────────────────────────────────────────────
// BT_Builder<TAgent> — 트리 정의를 만드는 흐름식 빌더. REIW core::BehaviorTreeBuilder와 같은 모양으로 쓴다:
//
//   auto tree = BT_Builder<CMonsterAgent>("Melee")
//       .ReactiveSelector()
//           .Sequence()
//               .Do<T_SelectTarget>()
//               .ReactiveSelector()
//                   .Do<T_UseSkill>()
//                   .Do<T_Attack>()
//                   .Do<T_Chase>()
//               .End()
//           .End()
//           .Do<T_Idle>()
//       .End()
//       .Build();
//
//  · 묶음(Sequence · Selector …)과 장식(Inverter · Condition · Cooldown …)은 End()로 닫는다(REIW와 같다). 장식은 자식이 정확히 하나.
//  · 잎은 Do<T>(인자) — T는 정적 OnEnter · OnUpdate · OnLeave를 가진 T_ 클래스(BT_TaskOf).
//  · Weight(w)는 다음에 더하는 노드의 몫(부모가 SelectorRandom일 때).
//  · Build()는 모양을 검사하고(빈 묶음 · 자식 없는 장식 · 닫지 않은 노드 · 노드 수 상한) 틀리면 이유를 찍고 멈춘다 —
//    트리는 서버가 뜰 때 한 번 만들므로 틀린 트리로 도는 것보다 바로 멈추는 편이 낫다.
// ─────────────────────────────────────────────────────────────────────────────
template <typename TAgent>
class BT_Builder
{
public:
    using Tree = BT_Tree<TAgent>;
    using Node = typename Tree::Node;
    using Predicate = typename Tree::Predicate;

    explicit BT_Builder(std::string name, size_t maxNodes = 64)
        : m_MaxNodes(maxNodes)
    {
        m_Tree = std::make_shared<Tree>();
        m_Tree->m_Name = std::move(name);
    }

    // ── 묶음 ──
    BT_Builder& Sequence()         { return Open(EnumCompositeType::Sequence); }
    BT_Builder& Selector()         { return Open(EnumCompositeType::Selector); }
    BT_Builder& ReactiveSelector() { return Open(EnumCompositeType::ReactiveSelector); }
    BT_Builder& SelectorRandom()   { return Open(EnumCompositeType::SelectorRandom); }
    BT_Builder& Parallel()         { return Open(EnumCompositeType::Parallel); }

    // ── 장식 ──
    BT_Builder& Inverter()  { return Open(EnumCompositeType::Inverter); }
    BT_Builder& Succeeder() { return Open(EnumCompositeType::Succeeder); }
    BT_Builder& Failurer()  { return Open(EnumCompositeType::Failurer); }
    BT_Builder& Condition(Predicate predicate, BT_Param param = {})
    {
        Open(EnumCompositeType::Condition, param);
        m_Tree->m_Nodes.back().predicate = predicate;
        if (predicate == nullptr)
            Fail("Condition의 술어가 비었다");
        return *this;
    }
    BT_Builder& Cooldown(UInt32 ms)  { return Open(EnumCompositeType::Cooldown, BT_Param{ static_cast<Int32>(ms), 0 }); }
    BT_Builder& TimeLimit(UInt32 ms) { return Open(EnumCompositeType::TimeLimit, BT_Param{ static_cast<Int32>(ms), 0 }); }

    // ── 잎 ──
    template <typename T>
    BT_Builder& Do(BT_Param param = {})
    {
        Add(EnumCompositeType::Task, param);
        m_Tree->m_Nodes.back().task = &BT_TaskOf<T, TAgent>();
        return *this;
    }
    BT_Builder& Wait(UInt32 ms) { return Do<BT_Wait>(BT_Param{ static_cast<Int32>(ms), 0 }); }

    // 다음에 더하는 노드의 몫(SelectorRandom의 자식)
    BT_Builder& Weight(UInt8 weight)
    {
        m_NextWeight = weight == 0 ? 1 : weight;
        return *this;
    }

    BT_Builder& End()
    {
        if (m_Open.empty())
        {
            Fail("닫을 노드가 없는데 End()");
            return *this;
        }
        const UInt16 index = m_Open.back();
        m_Open.pop_back();
        const Node& n = m_Tree->m_Nodes[index];
        if (n.childCount == 0)
            Fail(std::string(KindName(n.type)) + "에 자식이 없다");
        if (IsDecorator(n.type) && n.childCount != 1)
            Fail(std::string(KindName(n.type)) + "(장식)의 자식은 하나여야 한다");
        if (n.type == EnumCompositeType::SelectorRandom && n.childCount > 16)
            Fail("SelectorRandom의 자식은 16개까지");
        return *this;
    }

    [[nodiscard]] std::shared_ptr<const Tree> Build()
    {
        if (!m_Open.empty())
            Fail("닫지 않은 노드가 " + std::to_string(m_Open.size()) + "개(End() 빠짐)");
        if (m_Tree->m_Nodes.empty())
            Fail("빈 트리");
        if (m_Tree->m_Nodes.size() > m_MaxNodes)
            Fail("노드 " + std::to_string(m_Tree->m_Nodes.size()) + "개 > 상한 " + std::to_string(m_MaxNodes));
        return m_Tree;
    }

private:
    [[nodiscard]] static bool IsDecorator(EnumCompositeType t) noexcept
    {
        return t >= EnumCompositeType::Inverter;
    }

    [[nodiscard]] static const char* KindName(EnumCompositeType t) noexcept
    {
        switch (t)
        {
        case EnumCompositeType::Task: return "Task";
        case EnumCompositeType::Sequence: return "Sequence";
        case EnumCompositeType::Selector: return "Selector";
        case EnumCompositeType::ReactiveSelector: return "ReactiveSelector";
        case EnumCompositeType::SelectorRandom: return "SelectorRandom";
        case EnumCompositeType::Parallel: return "Parallel";
        case EnumCompositeType::Inverter: return "Inverter";
        case EnumCompositeType::Succeeder: return "Succeeder";
        case EnumCompositeType::Failurer: return "Failurer";
        case EnumCompositeType::Condition: return "Condition";
        case EnumCompositeType::Cooldown: return "Cooldown";
        case EnumCompositeType::TimeLimit: return "TimeLimit";
        }
        return "?";
    }

    [[noreturn]] void FailNow(const std::string& why) const
    {
        std::fprintf(stderr, "[BT] 트리 '%s' 모양이 틀렸다: %s\n", m_Tree->m_Name.c_str(), why.c_str());
        std::fflush(stderr);
        std::abort();
    }

    void Fail(const std::string& why) const { FailNow(why); }

    // 노드를 더하고 지금 열린 부모에 붙인다
    void Add(EnumCompositeType type, BT_Param param)
    {
        auto& nodes = m_Tree->m_Nodes;
        if (nodes.size() >= Tree::kNone)
            Fail("노드가 너무 많다");
        const UInt16 index = static_cast<UInt16>(nodes.size());
        Node n{};
        n.type = type;
        n.param = param;
        n.weight = m_NextWeight;
        m_NextWeight = 1;
        nodes.push_back(n);

        if (m_Open.empty())
        {
            if (index != 0)
                Fail("뿌리가 둘 — 트리는 노드 하나에서 시작한다");
            return;
        }
        Node& parent = nodes[m_Open.back()];
        if (IsDecorator(parent.type) && parent.childCount >= 1)
            Fail(std::string(KindName(parent.type)) + "(장식)에 자식을 둘 붙였다 — End() 빠짐");
        if (parent.firstChild == Tree::kNone)
        {
            parent.firstChild = index;
        }
        else
        {
            UInt16 last = parent.firstChild;
            while (nodes[last].nextSibling != Tree::kNone)
                last = nodes[last].nextSibling;
            nodes[last].nextSibling = index;
        }
        ++parent.childCount;
        if (parent.childCount > 255)
            Fail("자식이 255개를 넘는다");
    }

    BT_Builder& Open(EnumCompositeType type, BT_Param param = {})
    {
        Add(type, param);
        m_Open.push_back(static_cast<UInt16>(m_Tree->m_Nodes.size() - 1));
        return *this;
    }

    std::shared_ptr<Tree> m_Tree;
    std::vector<UInt16>   m_Open;
    size_t                m_MaxNodes = 64;
    UInt8                 m_NextWeight = 1;
};
