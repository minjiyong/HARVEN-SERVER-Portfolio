#pragma once

#include "Base/Core/Types.h"

// ─────────────────────────────────────────────────────────────────────────────
// 행동 트리 — 결과 · 노드 종류 · 잎(할 일). REIW reiw-server-libs Base/BehaviorTree(BT_Task · BT_Composite · BT_Decorator ·
// BT_Builder)의 뜻과 이름을 따르되, 하르벤 실행 모델(Zone 스레드 하나가 혼자 돌리는 액터 · Zone 타이머로 깨우기)에 맞게 다시 짰다.
//
//  · 트리 정의(BT_Tree)는 불변이고 같은 종류의 개체가 함께 쓴다 — 개체마다 노드 객체를 만들지 않는다
//    (REIW는 몬스터마다 shared_ptr 노드 트리를 새로 만든다). 개체 몫은 노드마다 8바이트 상태(BT_Instance)뿐이다.
//  · 잠금이 없다 — 트리 상태는 소유 Zone 스레드만 만진다(REIW는 노드마다 recursive_mutex).
//  · 잎은 정적 함수를 가진 T_ 클래스(OnEnter · OnUpdate · OnLeave) + 숫자 인자(BT_Param) — 뜨거운 길에 std::function ·
//    dynamic_pointer_cast · 가상 호출이 없다.
//  · 폴링하지 않는다 — 달리는(Running) 잎이 "다음에 언제 깨워 달라"(Agent::WakeAt)를 남기고, 주인이 그 시각에 한 번 다시 돌린다
//    (REIW는 OnUpdate가 주기마다 불려 taskNextTime을 스스로 본다).
//  · 우선 선택(ReactiveSelector)이 달리던 뒤 가지를 끊으면 그 가지의 잎은 OnLeave(Invalid = 끊김)를 받는다 — 시전 취소 · 예고 끊기를
//    잎이 스스로 정리한다(REIW InitInstance는 상태만 되돌린다).
//
// Agent(트리를 돌리는 쪽)가 갖춰야 하는 것:
//    UInt32 Now() const              — 지금 시각(ms, 단조)
//    void   WakeAt(UInt32 at)        — 이 시각에 다시 돌려 달라(여럿이면 가장 이른 것)
//    UInt32 Random(UInt32 bound)     — [0, bound) 무작위(SelectorRandom)
// ─────────────────────────────────────────────────────────────────────────────

// 결과 — REIW와 같은 이름 · 같은 뜻
enum class EnumTaskStatus : UInt8
{
    Invalid = 0,     // 아직 시작하지 않음(다음 틱에 OnEnter부터) · OnLeave에 넘길 때는 "끊겼다"
    Success,
    Failure,
    Running,         // 다음 틱에 OnUpdate부터 이어서
    ForceShutdown,   // 트리를 멈춘다(주인이 재운다) — 묶음은 그대로 위로 올린다
};

// 노드 종류 — REIW EnumCompositeType + 장식 종류
enum class EnumCompositeType : UInt8
{
    Task = 0,           // 잎
    // ── 묶음(자식 여럿) ──
    Sequence,           // 차례로 — 실패하면 실패, 모두 성공하면 성공(달리던 자식부터 이어서)
    Selector,           // 차례로 — 성공하면 성공, 모두 실패하면 실패(달리던 자식부터 이어서)
    ReactiveSelector,   // 틱마다 첫 자식부터 다시 본다 — 앞(우선) 자식이 실패가 아니면 달리던 뒤 자식을 끊는다
    SelectorRandom,     // 가중치(BT_Node::weight) 무작위 순서로 하나 — 달리던 자식이 있으면 그것을 이어서
    Parallel,           // 모든 자식을 돌린다 — 결과는 첫 자식(주 행동)의 것, 첫 자식이 끝나면 달리던 나머지(곁 행동)를 끊는다
    // ── 장식(자식 하나) ──
    Inverter,           // 성공 ↔ 실패
    Succeeder,          // 끝나면 성공
    Failurer,           // 끝나면 실패
    Condition,          // 술어가 참일 때만 자식 — 틱마다 다시 보고, 거짓이 되면 달리던 자식을 끊고 실패
    Cooldown,           // 자식이 성공하면 param.a ms 동안 실패(기다리는 동안 풀리는 시각에 깨워 달라고 남긴다)
    TimeLimit,          // 자식이 param.a ms 넘게 달리면 끊고 실패
};

// 노드마다의 숫자 인자(정의에 박힌다 — 사거리 · 칸 번호 · ms …)
struct BT_Param
{
    Int32 a = 0;
    Int32 b = 0;
};

// 개체 몫 노드 상태 — 8바이트. 묶음은 cursor(달리던 자식), 장식 · 잎은 time · count를 기억으로 쓴다.
struct BT_NodeState
{
    EnumTaskStatus status = EnumTaskStatus::Invalid;
    UInt8          cursor = 0;
    UInt16         count = 0;
    UInt32         time = 0;
};
static_assert(sizeof(BT_NodeState) == 8, "노드 상태는 8바이트(개체 몫을 작게)");

// 잎이 받는 것 — 정의의 인자 + 이 잎의 작은 기억(시각 · 횟수)
struct BT_Frame
{
    const BT_Param& param;
    BT_NodeState&   state;
};

// 잎 하나의 함수 묶음(T_ 클래스의 정적 함수에서 만든다 — BT_TaskOf)
template <typename TAgent>
struct BT_TaskDef
{
    const char* name = "";
    EnumTaskStatus (*onEnter)(TAgent&, BT_Frame&) = nullptr;                // 없으면 Success(곧바로 OnUpdate)
    EnumTaskStatus (*onUpdate)(TAgent&, BT_Frame&) = nullptr;               // 없으면 Success
    void (*onLeave)(TAgent&, BT_Frame&, EnumTaskStatus result) = nullptr;   // 끝났다(Success · Failure · ForceShutdown) · 끊겼다(Invalid)
};

// T_ 클래스 → 잎 정의. T는 정적 함수 OnEnter · OnUpdate · OnLeave 중 필요한 것만 둔다(REIW T_BaseTask의 가상 함수 자리).
//   static EnumTaskStatus OnEnter(TAgent&, BT_Frame&);
//   static EnumTaskStatus OnUpdate(TAgent&, BT_Frame&);
//   static void           OnLeave(TAgent&, BT_Frame&, EnumTaskStatus result);
//   static constexpr const char* kName = "T_Chase";   (없으면 "")
template <typename T, typename TAgent>
[[nodiscard]] const BT_TaskDef<TAgent>& BT_TaskOf() noexcept
{
    static const BT_TaskDef<TAgent> def = []()
    {
        BT_TaskDef<TAgent> d{};
        if constexpr (requires { T::kName; })
            d.name = T::kName;
        if constexpr (requires(TAgent& a, BT_Frame& f) { T::OnEnter(a, f); })
            d.onEnter = &T::OnEnter;
        if constexpr (requires(TAgent& a, BT_Frame& f) { T::OnUpdate(a, f); })
            d.onUpdate = &T::OnUpdate;
        if constexpr (requires(TAgent& a, BT_Frame& f, EnumTaskStatus r) { T::OnLeave(a, f, r); })
            d.onLeave = &T::OnLeave;
        return d;
    }();
    return def;
}

// 기본 잎 — param.a ms 기다린다(REIW Base/T_Wait). 기다리는 동안 끝나는 시각에 깨워 달라고 남긴다.
struct BT_Wait
{
    static constexpr const char* kName = "BT_Wait";

    template <typename TAgent>
    static EnumTaskStatus OnEnter(TAgent& agent, BT_Frame& f)
    {
        f.state.time = agent.Now() + static_cast<UInt32>(f.param.a > 0 ? f.param.a : 0);
        return EnumTaskStatus::Success;
    }

    template <typename TAgent>
    static EnumTaskStatus OnUpdate(TAgent& agent, BT_Frame& f)
    {
        if (static_cast<Int32>(agent.Now() - f.state.time) >= 0)
            return EnumTaskStatus::Success;
        agent.WakeAt(f.state.time);
        return EnumTaskStatus::Running;
    }
};
