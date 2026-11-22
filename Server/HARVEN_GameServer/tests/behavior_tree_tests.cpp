// 행동 트리 시험 — Libs/Base/BehaviorTree만(서버 코드 없이). 규칙마다 작은 트리를 만들어 결과 · 잎 호출 순서 · 깨우기 요청을 본다.
//   cmake --build <폴더> --target BehaviorTreeTests && <폴더>/Server/HARVEN_GameServer/tests/BehaviorTreeTests
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "Base/BehaviorTree/BehaviorTree.h"

namespace
{
    int g_Failed = 0;
    int g_Checked = 0;

#define CHECK(expr) \
    do { ++g_Checked; if (!(expr)) { ++g_Failed; std::fprintf(stderr, "  실패 %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (false)

    struct Agent
    {
        UInt32 now = 1000;
        UInt32 wake = 0;
        bool   hasWake = false;
        std::mt19937 rng{ 7 };
        std::vector<std::string> log;
        // 잎이 돌려줄 결과(이름 → 결과) — 시험마다 바꾼다
        int counterA = 0;
        bool gate = true;

        [[nodiscard]] UInt32 Now() const { return now; }
        void WakeAt(UInt32 at)
        {
            if (!hasWake || static_cast<Int32>(at - wake) < 0)
            {
                wake = at;
                hasWake = true;
            }
        }
        UInt32 Random(UInt32 bound) { return static_cast<UInt32>(rng() % bound); }
        void ClearWake() { hasWake = false; wake = 0; }
        std::string Joined() const
        {
            std::string s;
            for (const auto& l : log) { if (!s.empty()) s += ' '; s += l; }
            return s;
        }
    };

    // param.a = 몇 번 Running을 낸 뒤 끝날지, param.b = 끝 결과(0 성공 · 1 실패 · 2 강제 종료)
    template <char Tag>
    struct T_Step
    {
        static EnumTaskStatus OnEnter(Agent& a, BT_Frame& f)
        {
            a.log.push_back(std::string(1, Tag) + "+");
            f.state.count = 0;
            return EnumTaskStatus::Success;
        }
        static EnumTaskStatus OnUpdate(Agent& a, BT_Frame& f)
        {
            a.log.push_back(std::string(1, Tag));
            if (f.state.count < static_cast<UInt16>(f.param.a))
            {
                ++f.state.count;
                return EnumTaskStatus::Running;
            }
            return f.param.b == 0 ? EnumTaskStatus::Success : (f.param.b == 1 ? EnumTaskStatus::Failure : EnumTaskStatus::ForceShutdown);
        }
        static void OnLeave(Agent& a, BT_Frame&, EnumTaskStatus r)
        {
            a.log.push_back(std::string(1, Tag) + (r == EnumTaskStatus::Invalid ? "x" : "-"));
        }
    };
    using A = T_Step<'A'>;
    using B = T_Step<'B'>;
    using C = T_Step<'C'>;

    // OnEnter에서 실패 — OnLeave(Failure)는 불리고 OnUpdate는 안 불린다
    struct T_EnterFail
    {
        static EnumTaskStatus OnEnter(Agent& a, BT_Frame&) { a.log.push_back("E+"); return EnumTaskStatus::Failure; }
        static EnumTaskStatus OnUpdate(Agent& a, BT_Frame&) { a.log.push_back("E"); return EnumTaskStatus::Success; }
        static void OnLeave(Agent& a, BT_Frame&, EnumTaskStatus) { a.log.push_back("E-"); }
    };

    struct T_Count
    {
        static EnumTaskStatus OnUpdate(Agent& a, BT_Frame&) { ++a.counterA; return EnumTaskStatus::Success; }
    };

    bool Gate(Agent& a, const BT_Param&) { return a.gate; }

    using Run = BT_Instance<32>;

    void TestSequenceResume()
    {
        auto tree = BT_Builder<Agent>("seq")
            .Sequence()
                .Do<A>(BT_Param{ 0, 0 })
                .Do<B>(BT_Param{ 2, 0 })     // 두 번 달린 뒤 성공
                .Do<C>(BT_Param{ 0, 0 })
            .End()
            .Build();
        Agent a; Run r;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);
        CHECK(a.Joined() == "A+ A A- B+ B");
        a.log.clear();
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);   // A를 다시 돌리지 않고 B부터
        CHECK(a.Joined() == "B");
        a.log.clear();
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Success);
        CHECK(a.Joined() == "B B- C+ C C-");
        a.log.clear();
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);   // 끝난 뒤에는 처음부터
        CHECK(a.Joined() == "A+ A A- B+ B");
    }

    void TestSelectorFallThrough()
    {
        auto tree = BT_Builder<Agent>("sel")
            .Selector()
                .Do<A>(BT_Param{ 0, 1 })     // 실패
                .Do<T_EnterFail>()
                .Do<B>(BT_Param{ 1, 0 })     // 한 번 달린 뒤 성공
                .Do<C>()
            .End()
            .Build();
        Agent a; Run r;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);
        CHECK(a.Joined() == "A+ A A- E+ E- B+ B");
        a.log.clear();
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Success);
        CHECK(a.Joined() == "B B-");
    }

    void TestReactivePreempt()
    {
        // 앞 가지(조건)가 참이 되면 달리던 뒤 가지를 끊는다(OnLeave(Invalid) = "Bx")
        auto tree = BT_Builder<Agent>("reactive")
            .ReactiveSelector()
                .Condition(&Gate)
                    .Do<A>(BT_Param{ 5, 0 })
                .End()
                .Do<B>(BT_Param{ 9, 0 })
            .End()
            .Build();
        Agent a; Run r;
        a.gate = false;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);
        CHECK(a.Joined() == "B+ B");
        a.log.clear();
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);
        CHECK(a.Joined() == "B");
        a.log.clear();
        a.gate = true;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);
        CHECK(a.Joined() == "A+ A Bx");
        a.log.clear();
        // 조건이 거짓이 되면 달리던 A를 끊고 B를 새로 시작
        a.gate = false;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);
        CHECK(a.Joined() == "Ax B+ B");
    }

    void TestSelectorRandomWeights()
    {
        auto tree = BT_Builder<Agent>("random")
            .SelectorRandom()
                .Weight(1).Do<A>()
                .Weight(3).Do<B>()
            .End()
            .Build();
        Agent a; Run r;
        int countA = 0, countB = 0;
        for (int i = 0; i < 4000; ++i)
        {
            a.log.clear();
            CHECK(tree->Tick(a, r) == EnumTaskStatus::Success);
            if (a.log.size() > 1 && a.log[1] == "A") ++countA;
            if (a.log.size() > 1 && a.log[1] == "B") ++countB;
        }
        CHECK(countA + countB == 4000);
        CHECK(countB > countA * 2);   // 3:1 근처
        CHECK(countA > 600);

        // 고른 것이 실패하면 남은 것을 고른다(같은 틱)
        auto tree2 = BT_Builder<Agent>("random2")
            .SelectorRandom()
                .Do<A>(BT_Param{ 0, 1 })
                .Do<B>(BT_Param{ 0, 1 })
                .Do<C>()
            .End()
            .Build();
        Run r2;
        for (int i = 0; i < 50; ++i)
            CHECK(tree2->Tick(a, r2) == EnumTaskStatus::Success);
    }

    void TestParallelMain()
    {
        // 첫 자식(주)이 끝나면 곁 자식을 끊는다. 곁 자식은 끝나도 다음 틱에 다시 시작한다.
        auto tree = BT_Builder<Agent>("parallel")
            .Parallel()
                .Do<A>(BT_Param{ 2, 0 })
                .Do<B>(BT_Param{ 5, 0 })
                .Do<T_Count>()
            .End()
            .Build();
        Agent a; Run r;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);
        a.log.clear();
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Success);
        CHECK(a.Joined() == "A A- B Bx");
        CHECK(a.counterA == 3);   // 곁 행동은 틱마다 다시 돈다
    }

    void TestCooldown()
    {
        auto tree = BT_Builder<Agent>("cooldown")
            .Selector()
                .Cooldown(500)
                    .Do<T_Count>()
                .End()
                .Do<B>()
            .End()
            .Build();
        Agent a; Run r;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Success);
        CHECK(a.counterA == 1);
        a.ClearWake();
        a.now += 100;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Success);   // 쿨다운 중 — B가 대신
        CHECK(a.counterA == 1);
        CHECK(a.hasWake && a.wake == 1500);                    // 풀리는 시각에 깨워 달라고 남겼다
        a.now = 1500;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Success);
        CHECK(a.counterA == 2);

    }

    // 오래 안 쓴 쿨다운 — 풀린 뒤 한 번이라도 들여다봤으면 시계(UInt32 ms)가 반 바퀴(24.8일) 넘게 돌아도 풀린 채여야 한다
    // (풀린 시각은 지운다 — 차이 비교로는 오래된 시각이 "미래"로 보인다). 깨어 있는 몬스터는 늘 틱이 돌고, 잠들면 상태를 버린다.
    void TestCooldownClockWrap()
    {
        auto tree = BT_Builder<Agent>("cooldown-wrap")
            .Selector()
                .Cooldown(500)
                    .Condition(&Gate)
                        .Do<T_Count>()
                    .End()
                .End()
                .Do<B>()
            .End()
            .Build();
        Agent a; Run r;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Success);
        CHECK(a.counterA == 1);                                // 썼다 — 1500까지 쿨다운
        a.gate = false;
        a.now = 1600;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Success);   // 풀렸지만 조건이 막혀 B — 쿨다운 시각은 지워졌다
        CHECK(a.counterA == 1);
        a.gate = true;
        a.now += 0x90000000u;                                  // 반 바퀴 넘게
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Success);
        CHECK(a.counterA == 2);                                // 곧바로 다시 쓴다
    }

    void TestTimeLimitAndWait()
    {
        auto tree = BT_Builder<Agent>("limit")
            .TimeLimit(300)
                .Wait(1000)
            .End()
            .Build();
        Agent a; Run r;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Running);
        CHECK(a.hasWake && a.wake == 1300);   // 둘 중 이른 것(제한 300 < 기다림 1000)
        a.now = 1300;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::Failure);

        auto wait = BT_Builder<Agent>("wait").Wait(200).Build();
        Run w;
        a.ClearWake();
        CHECK(wait->Tick(a, w) == EnumTaskStatus::Running);
        CHECK(a.wake == a.now + 200);
        a.now += 199;
        CHECK(wait->Tick(a, w) == EnumTaskStatus::Running);
        a.now += 1;
        CHECK(wait->Tick(a, w) == EnumTaskStatus::Success);
    }

    void TestDecorators()
    {
        auto inv = BT_Builder<Agent>("inv").Inverter().Do<A>(BT_Param{ 0, 1 }).End().Build();
        auto suc = BT_Builder<Agent>("suc").Succeeder().Do<A>(BT_Param{ 0, 1 }).End().Build();
        auto fai = BT_Builder<Agent>("fai").Failurer().Do<A>().End().Build();
        Agent a; Run r1, r2, r3;
        CHECK(inv->Tick(a, r1) == EnumTaskStatus::Success);
        CHECK(suc->Tick(a, r2) == EnumTaskStatus::Success);
        CHECK(fai->Tick(a, r3) == EnumTaskStatus::Failure);
        // Running은 그대로 올라간다(이어서 돌 수 있게)
        auto runInv = BT_Builder<Agent>("runInv").Inverter().Do<A>(BT_Param{ 1, 0 }).End().Build();
        Run r4;
        CHECK(runInv->Tick(a, r4) == EnumTaskStatus::Running);
        CHECK(runInv->Tick(a, r4) == EnumTaskStatus::Failure);
    }

    void TestForceShutdownAndAbort()
    {
        auto tree = BT_Builder<Agent>("shutdown")
            .Sequence()
                .Do<A>()
                .Do<B>(BT_Param{ 0, 2 })   // 강제 종료
                .Do<C>()
            .End()
            .Build();
        Agent a; Run r;
        CHECK(tree->Tick(a, r) == EnumTaskStatus::ForceShutdown);
        CHECK(a.Joined() == "A+ A A- B+ B B-");

        // Abort: 달리던 잎만 OnLeave(Invalid), 그 뒤 처음부터
        auto run = BT_Builder<Agent>("abort").Sequence().Do<A>().Do<B>(BT_Param{ 3, 0 }).End().Build();
        Run r2;
        a.log.clear();
        CHECK(run->Tick(a, r2) == EnumTaskStatus::Running);
        a.log.clear();
        run->Abort(a, r2);
        CHECK(a.Joined() == "Bx");
        a.log.clear();
        CHECK(run->Tick(a, r2) == EnumTaskStatus::Running);
        CHECK(a.Joined() == "A+ A A- B+ B");
    }

    void TestSharedDefinition()
    {
        // 정의 하나를 개체 여럿이 함께 쓴다 — 상태는 개체마다
        auto tree = BT_Builder<Agent>("shared").Sequence().Do<A>(BT_Param{ 1, 0 }).Do<B>().End().Build();
        Agent a1, a2; Run r1, r2;
        CHECK(tree->Tick(a1, r1) == EnumTaskStatus::Running);
        CHECK(tree->Tick(a2, r2) == EnumTaskStatus::Running);
        CHECK(tree->Tick(a1, r1) == EnumTaskStatus::Success);
        a2.log.clear();
        CHECK(tree->Tick(a2, r2) == EnumTaskStatus::Success);
        CHECK(a2.Joined() == "A A- B+ B B-");
        CHECK(tree->Size() == 3);
    }
}

int main()
{
    TestSequenceResume();
    TestSelectorFallThrough();
    TestReactivePreempt();
    TestSelectorRandomWeights();
    TestParallelMain();
    TestCooldown();
    TestCooldownClockWrap();
    TestTimeLimitAndWait();
    TestDecorators();
    TestForceShutdownAndAbort();
    TestSharedDefinition();
    std::printf("[BehaviorTreeTests] %d/%d 통과\n", g_Checked - g_Failed, g_Checked);
    return g_Failed == 0 ? 0 : 1;
}
