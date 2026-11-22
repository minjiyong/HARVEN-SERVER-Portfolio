// ─────────────────────────────────────────────────────────────────────────────
// concurrency_tests — 잠금 없는 기반 코드 시험(서버 없이 돈다).
//   CLockFreeStack(SList 대체) · CObjectPool(매거진 풀) · CSafeMessageQueue(Zone 메일함)
//
//   ConcurrencyTests            정확성 시험(잃어버림 · 중복 · 순서 · 깨우기 누락). 실패하면 종료 코드 1
//   ConcurrencyTests --bench    성능 비교(같은 일을 malloc · 뮤텍스 · SList 방식과 견준다)
// TSan 빌드(linux-tsan 프리셋)에서도 돌린다 — 경쟁 상태가 있으면 TSan이 잡는다.
// ─────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "Base/Core/LockFreeStack.h"
#include "Base/Core/ObjectPool.h"
#include "WorkerThread/SafeMessageQueue.h"
#include "ThirdParty/moodycamel/blockingconcurrentqueue.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    int GFailures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            ++GFailures;
    }

#if defined(__SANITIZE_THREAD__)
    constexpr int kScale = 10;   // TSan은 10배쯤 느리다
#else
    constexpr int kScale = 1;
#endif

    // 시험이 멈추면(깨우기 누락) 여기서 끝낸다
    void Watchdog(std::atomic<bool>& done, int seconds, const char* what)
    {
        const auto until = Clock::now() + std::chrono::seconds(seconds);
        while (!done.load())
        {
            if (Clock::now() > until)
            {
                std::printf("FAIL: %s — %d초 안에 끝나지 않았다(멈춤)\n", what, seconds);
                std::fflush(stdout);
                std::_Exit(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    // ── CLockFreeStack ──────────────────────────────────────────────────────────
    struct StackNode
    {
        LockFreeEntry entry;
        std::atomic<int> inStack{ 1 };
    };
    static_assert(offsetof(StackNode, entry) == 0);

    void TestLockFreeStack()
    {
        constexpr int kThreads = 4;
        constexpr int kNodes = 256;
        const int kRounds = 200000 / kScale;

        CLockFreeStack stack;
        std::vector<StackNode> nodes(kNodes);
        for (auto& node : nodes)
            stack.Push(&node.entry);

        std::atomic<int> errors{ 0 };
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t)
            threads.emplace_back([&]()
            {
                std::vector<StackNode*> held;
                for (int i = 0; i < kRounds; ++i)
                {
                    // 몇 개 빼서 들고 있다가 돌려준다 — 같은 주소가 빠졌다 다시 들어가는 ABA 상황을 많이 만든다
                    for (int k = 0; k < 3; ++k)
                        if (LockFreeEntry* entry = stack.Pop())
                        {
                            auto* node = reinterpret_cast<StackNode*>(entry);
                            if (node->inStack.exchange(0) != 1)
                                errors.fetch_add(1);   // 스택에 없는 노드가 나왔다(중복)
                            held.push_back(node);
                        }
                    for (StackNode* node : held)
                    {
                        node->inStack.store(1);
                        stack.Push(&node->entry);
                    }
                    held.clear();
                }
            });
        for (auto& thread : threads)
            thread.join();

        int remaining = 0;
        while (LockFreeEntry* entry = stack.Pop())
        {
            ++remaining;
            if (reinterpret_cast<StackNode*>(entry)->inStack.exchange(0) != 1)
                errors.fetch_add(1);
        }
        Check(errors.load() == 0, "CLockFreeStack — 여러 스레드가 넣고 빼도 같은 노드가 두 번 나오지 않는다");
        Check(remaining == kNodes, "CLockFreeStack — 끝나면 노드 수가 그대로다(잃어버림 없음)");
        Check(stack.EmptyApprox(), "CLockFreeStack — 다 빼면 비었다");
    }

    // ── CObjectPool ─────────────────────────────────────────────────────────────
    struct PoolObject
    {
        static inline std::atomic<int> s_Live{ 0 };
        std::uint64_t value;
        std::atomic<int> owned{ 0 };
        char padding[200];

        explicit PoolObject(std::uint64_t v) : value(v) { s_Live.fetch_add(1); }
        ~PoolObject() { s_Live.fetch_sub(1); }
    };

    void TestObjectPool()
    {
        // 만드는 스레드(IO처럼)와 돌려주는 스레드(Zone처럼)가 다르다 — 매거진이 스레드 사이를 돈다
        constexpr int kProducers = 3;
        const int kPerProducer = 300000 / kScale;

        std::mutex lock;
        std::deque<PoolObject*> handoff;
        std::atomic<int> producersLeft{ kProducers };
        std::atomic<int> errors{ 0 };
        std::atomic<bool> done{ false };
        std::thread watchdog(Watchdog, std::ref(done), 120, "CObjectPool");

        std::vector<std::thread> threads;
        for (int p = 0; p < kProducers; ++p)
            threads.emplace_back([&, p]()
            {
                for (int i = 0; i < kPerProducer; ++i)
                {
                    const std::uint64_t value = (static_cast<std::uint64_t>(p) << 32) | static_cast<std::uint32_t>(i);
                    PoolObject* object = CObjectPool<PoolObject>::Acquire(value);
                    if (object->owned.exchange(1) != 0 || object->value != value)
                        errors.fetch_add(1);   // 아직 쓰는 객체가 또 나왔다 · 생성자 값이 아니다
                    std::lock_guard guard(lock);
                    handoff.push_back(object);
                }
                producersLeft.fetch_sub(1);
            });
        // 돌려주는 쪽 둘
        for (int c = 0; c < 2; ++c)
            threads.emplace_back([&]()
            {
                while (true)
                {
                    PoolObject* object = nullptr;
                    {
                        std::lock_guard guard(lock);
                        if (!handoff.empty())
                        {
                            object = handoff.front();
                            handoff.pop_front();
                        }
                    }
                    if (object == nullptr)
                    {
                        if (producersLeft.load() == 0)
                        {
                            std::lock_guard guard(lock);
                            if (handoff.empty())
                                break;
                        }
                        std::this_thread::yield();
                        continue;
                    }
                    object->owned.store(0);
                    CObjectPool<PoolObject>::Release(object);
                }
            });
        for (auto& thread : threads)
            thread.join();
        done.store(true);
        watchdog.join();

        Check(errors.load() == 0, "CObjectPool — 스레드 사이로 돌려도 쓰는 중인 객체를 다시 내주지 않는다");
        Check(PoolObject::s_Live.load() == 0, "CObjectPool — Release가 소멸자를 부른다(살아 있는 객체 0)");

        // 돌려받은 자리를 다시 쓰는가(새로 할당하지 않는가)
        std::vector<PoolObject*> first;
        for (int i = 0; i < 100; ++i)
            first.push_back(CObjectPool<PoolObject>::Acquire(1));
        std::vector<void*> addresses(first.begin(), first.end());
        for (PoolObject* object : first)
            CObjectPool<PoolObject>::Release(object);
        int reused = 0;
        std::vector<PoolObject*> second;
        for (int i = 0; i < 100; ++i)
        {
            second.push_back(CObjectPool<PoolObject>::Acquire(2));
            for (void* address : addresses)
                if (address == second.back())
                {
                    ++reused;
                    break;
                }
        }
        for (PoolObject* object : second)
            CObjectPool<PoolObject>::Release(object);
        Check(reused == 100, "CObjectPool — 돌려받은 자리를 다시 쓴다");
    }

    // ── CSafeMessageQueue ───────────────────────────────────────────────────────
    struct Message
    {
        std::uint32_t producer = 0;
        std::uint32_t seq = 0;
    };

    void TestMailboxOrderAndWakeups()
    {
        constexpr int kProducers = 4;
        const std::uint32_t kPerProducer = 200000 / kScale;

        CSafeMessageQueue<Message> queue;
        std::atomic<bool> done{ false };
        std::thread watchdog(Watchdog, std::ref(done), 120, "CSafeMessageQueue 순서 · 깨우기");

        std::vector<std::thread> producers;
        for (int p = 0; p < kProducers; ++p)
            producers.emplace_back([&, p]()
            {
                for (std::uint32_t i = 0; i < kPerProducer; ++i)
                {
                    queue.Push(Message{ static_cast<std::uint32_t>(p), i });
                    // 가끔 쉬어 주인이 잠들게 한다 — 잠든 주인을 깨우는 길을 많이 지난다
                    if ((i & 0x3FF) == 0)
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            });

        std::vector<std::uint32_t> next(kProducers, 0);
        std::uint64_t received = 0;
        int orderErrors = 0;
        const std::uint64_t total = static_cast<std::uint64_t>(kProducers) * kPerProducer;
        Message message;
        while (received < total)
        {
            if (!queue.TryPop(message))
            {
                queue.Wait();   // 끝없이 — 깨우기를 잃어버리면 감시자가 멈춤으로 잡는다
                continue;
            }
            if (message.seq != next[message.producer])
                ++orderErrors;
            next[message.producer] = message.seq + 1;
            ++received;
        }
        for (auto& producer : producers)
            producer.join();
        done.store(true);
        watchdog.join();

        Check(orderErrors == 0, "CSafeMessageQueue — 한 스레드가 넣은 순서대로 나온다");
        Check(received == total && queue.Empty(), "CSafeMessageQueue — 넣은 것을 모두 받고(깨우기 누락 없음) 비었다");
    }

    void TestMailboxCausalOrder()
    {
        // A가 넣고 → B에게 알리고 → B가 넣는다. 주인은 언제나 A의 것을 먼저 받아야 한다(Zone 이동: 입장 처리 → 그 뒤 패킷).
        const int kRounds = 100000 / kScale;
        CSafeMessageQueue<Message> queue;
        std::atomic<int> turn{ -1 };
        std::atomic<bool> done{ false };
        std::thread watchdog(Watchdog, std::ref(done), 120, "CSafeMessageQueue 인과 순서");

        std::thread a([&]()
        {
            for (int i = 0; i < kRounds; ++i)
            {
                queue.Push(Message{ 0, static_cast<std::uint32_t>(i) });
                turn.store(i, std::memory_order_release);
                while (turn.load(std::memory_order_acquire) != -1 - i - 1) std::this_thread::yield();   // B가 넣을 때까지
            }
        });
        std::thread b([&]()
        {
            for (int i = 0; i < kRounds; ++i)
            {
                while (turn.load(std::memory_order_acquire) != i) std::this_thread::yield();
                queue.Push(Message{ 1, static_cast<std::uint32_t>(i) });
                turn.store(-1 - i - 1, std::memory_order_release);
            }
        });

        int errors = 0;
        int received = 0;
        bool expectA = true;
        Message message;
        while (received < kRounds * 2)
        {
            if (!queue.TryPop(message))
            {
                queue.Wait(1000);
                continue;
            }
            if (message.producer != (expectA ? 0u : 1u) || static_cast<int>(message.seq) != received / 2)
                ++errors;
            expectA = !expectA;
            ++received;
        }
        a.join();
        b.join();
        done.store(true);
        watchdog.join();
        Check(errors == 0, "CSafeMessageQueue — 다른 스레드가 넣었어도 먼저 넣은 것이 먼저 나온다(인과 순서)");
    }

    void TestMailboxTimedWait()
    {
        CSafeMessageQueue<int> queue;
        const auto start = Clock::now();
        queue.Wait(5000);   // 5ms
        const auto waited = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
        Check(waited >= 4000 && waited < 500000, "CSafeMessageQueue — 비었으면 정한 시간만큼 자고 일어난다");

        // 시간이 다 된 순간과 넣기가 겹쳐도 다음 Wait가 영원히 자거나 깨우기가 남지 않는다
        std::atomic<bool> done{ false };
        std::thread watchdog(Watchdog, std::ref(done), 60, "CSafeMessageQueue 시간 기다림");
        int popped = 0;
        const int kRounds = 20000 / kScale;
        std::thread producer([&]()
        {
            for (int i = 0; i < kRounds; ++i)
            {
                queue.Push(i);
                std::this_thread::sleep_for(std::chrono::microseconds(i % 7 * 10));
            }
        });
        int value = 0;
        while (popped < kRounds)
        {
            if (queue.TryPop(value))
            {
                ++popped;
                continue;
            }
            queue.Wait(popped % 3 == 0 ? 1 : 30);   // 아주 짧은 시간 — 시간 끝 · 넣기 겹침을 많이 만든다
        }
        producer.join();
        // 남은 깨우기가 없다면 빈 큐의 Wait(2ms)는 2ms 가까이 잔다
        const auto start2 = Clock::now();
        queue.Wait(2000);
        const auto waited2 = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start2).count();
        done.store(true);
        watchdog.join();
        Check(popped == kRounds, "CSafeMessageQueue — 짧은 기다림과 넣기가 겹쳐도 하나도 잃지 않는다");
        Check(waited2 >= 1500, "CSafeMessageQueue — 짝 맞춤 뒤 남은 깨우기가 없다(빈 큐에서 바로 깨지 않는다)");
    }

    // ── 성능 비교(--bench) ───────────────────────────────────────────────────────
    struct alignas(64) Blob
    {
        char bytes[1024];   // 받은 패킷 묶음 크기
        Blob() noexcept {}
    };

    template <typename AcquireFn, typename ReleaseFn>
    double CrossThreadBench(AcquireFn acquire, ReleaseFn release, int count)
    {
        // 한 스레드가 만들고 다른 스레드가 돌려준다(IO → Zone). 사이는 잠금 없는 큐
        moodycamel::ConcurrentQueue<Blob*> handoff(4096);
        std::atomic<bool> producing{ true };
        const auto start = Clock::now();
        std::thread consumer([&]()
        {
            Blob* blobs[64];
            while (true)
            {
                const size_t n = handoff.try_dequeue_bulk(blobs, 64);
                for (size_t i = 0; i < n; ++i)
                    release(blobs[i]);
                if (n == 0 && !producing.load(std::memory_order_acquire) && handoff.size_approx() == 0)
                    break;
            }
        });
        for (int i = 0; i < count; ++i)
        {
            Blob* blob = acquire();
            blob->bytes[0] = static_cast<char>(i);
            while (!handoff.enqueue(blob)) {}
        }
        producing.store(false, std::memory_order_release);
        consumer.join();
        return std::chrono::duration<double, std::nano>(Clock::now() - start).count() / count;
    }

    template <typename AcquireFn, typename ReleaseFn>
    double SameThreadBench(AcquireFn acquire, ReleaseFn release, int count)
    {
        Blob* held[32];
        const auto start = Clock::now();
        for (int i = 0; i < count; i += 32)
        {
            for (auto& blob : held)
                blob = acquire();
            for (auto* blob : held)
                release(blob);
        }
        return std::chrono::duration<double, std::nano>(Clock::now() - start).count() / count;
    }

    // Windows SList와 같은 방식 — 객체 하나마다 lock-free 스택에 넣고 뺀다
    struct SListBlob
    {
        LockFreeEntry entry;
        Blob blob;
    };
    CLockFreeStack GSListFree;
    Blob* SListAcquire()
    {
        if (LockFreeEntry* entry = GSListFree.Pop())
            return &reinterpret_cast<SListBlob*>(entry)->blob;
        return &(new SListBlob())->blob;
    }
    void SListRelease(Blob* blob)
    {
        auto* node = reinterpret_cast<SListBlob*>(reinterpret_cast<char*>(blob) - offsetof(SListBlob, blob));
        GSListFree.Push(&node->entry);
    }

    std::mutex GMutexPoolLock;
    std::vector<Blob*> GMutexPool;
    Blob* MutexAcquire()
    {
        {
            std::lock_guard guard(GMutexPoolLock);
            if (!GMutexPool.empty())
            {
                Blob* blob = GMutexPool.back();
                GMutexPool.pop_back();
                return blob;
            }
        }
        return new Blob();
    }
    void MutexRelease(Blob* blob)
    {
        std::lock_guard guard(GMutexPoolLock);
        GMutexPool.push_back(blob);
    }

    // 5번 재서 가운데 값(스레드가 코어보다 많은 기계에서는 한 번 잰 값이 크게 흔들린다)
    template <typename Fn>
    double Median5(Fn fn)
    {
        double samples[5];
        for (double& sample : samples)
            sample = fn();
        std::sort(std::begin(samples), std::end(samples));
        return samples[2];
    }

    template <typename QueuePush, typename QueueRun>
    double MailboxBench(QueuePush push, QueueRun run, int producers, int perProducer)
    {
        const auto start = Clock::now();
        std::vector<std::thread> threads;
        for (int p = 0; p < producers; ++p)
            threads.emplace_back([&]() { for (int i = 0; i < perProducer; ++i) push(i); });
        run(static_cast<std::uint64_t>(producers) * perProducer);
        for (auto& thread : threads)
            thread.join();
        return std::chrono::duration<double, std::nano>(Clock::now() - start).count() / (producers * perProducer);
    }

    void RunBench()
    {
        const int kCount = 1'000'000;
        const int kProducers = 2;   // IO 스레드 둘이 한 Zone에 넣는 모양
        std::printf("── 객체 풀(1KB 묶음, 한 개당 ns — 작을수록 좋다) ──\n");
        std::printf("%-34s %10s %10s\n", "", "같은 스레드", "IO→Zone");
        auto row = [&](const char* name, auto acquire, auto release)
        {
            // 첫 판은 데우기(풀 채우기)
            (void)SameThreadBench(acquire, release, kCount / 10);
            const double same = Median5([&]() { return SameThreadBench(acquire, release, kCount); });
            (void)CrossThreadBench(acquire, release, kCount / 10);
            const double cross = Median5([&]() { return CrossThreadBench(acquire, release, kCount); });
            std::printf("%-34s %10.1f %10.1f\n", name, same, cross);
        };
        row("malloc · free", []() { return new Blob(); }, [](Blob* b) { delete b; });
        row("뮤텍스 + vector", MutexAcquire, MutexRelease);
        row("SList 방식(CLockFreeStack 하나씩)", SListAcquire, SListRelease);
        row("CObjectPool(매거진 + CLockFreeStack)",
            []() { return CObjectPool<Blob>::Acquire(); }, [](Blob* b) { CObjectPool<Blob>::Release(b); });

        std::printf("\n── Zone 메일함(생산자 %d → 주인 1, std::function 작업, 한 개당 ns, 5번 중 가운데) ──\n", kProducers);
        using Task = std::function<void()>;
        const int kPer = 300'000;
        std::uint64_t sink = 0;
        {
            std::mutex lock;
            std::condition_variable cv;
            std::queue<Task> queue;
            const double ns = Median5([&]() { return MailboxBench(
                [&](int i) { { std::lock_guard g(lock); queue.push([&sink, i]() { sink += i; }); } cv.notify_one(); },
                [&](std::uint64_t total)
                {
                    std::queue<Task> local;
                    for (std::uint64_t done = 0; done < total;)
                    {
                        {
                            std::unique_lock g(lock);
                            cv.wait(g, [&]() { return !queue.empty(); });
                            local.swap(queue);
                        }
                        for (; !local.empty(); local.pop(), ++done)
                            local.front()();
                    }
                }, kProducers, kPer); });
            std::printf("%-44s %8.1f\n", "예전(뮤텍스 + condition_variable + queue)", ns);
        }
        {
            moodycamel::BlockingConcurrentQueue<Task> queue;
            const double ns = Median5([&]() { return MailboxBench(
                [&](int i) { queue.enqueue([&sink, i]() { sink += i; }); },
                [&](std::uint64_t total)
                {
                    Task task;
                    for (std::uint64_t done = 0; done < total; ++done)
                    {
                        queue.wait_dequeue(task);
                        task();
                    }
                }, kProducers, kPer); });
            std::printf("%-44s %8.1f   (생산자 사이 순서를 지키지 않는다)\n", "moodycamel BlockingConcurrentQueue", ns);
        }
        {
            CSafeMessageQueue<Task> queue;
            const double ns = Median5([&]() { return MailboxBench(
                [&](int i) { queue.Push([&sink, i]() { sink += i; }); },
                [&](std::uint64_t total)
                {
                    Task task;
                    for (std::uint64_t done = 0; done < total;)
                    {
                        if (!queue.TryPop(task)) { queue.Wait(); continue; }
                        task();
                        ++done;
                    }
                }, kProducers, kPer); });
            std::printf("%-44s %8.1f\n", "CSafeMessageQueue(Vyukov MPSC + 풀 노드)", ns);
        }
        std::printf("(sink %llu)\n", static_cast<unsigned long long>(sink));
    }
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0)
    {
        RunBench();
        return 0;
    }
    TestLockFreeStack();
    TestObjectPool();
    TestMailboxOrderAndWakeups();
    TestMailboxCausalOrder();
    TestMailboxTimedWait();
    std::printf("%s (%d failed)\n", GFailures == 0 ? "ALL PASS" : "FAILED", GFailures);
    return GFailures == 0 ? 0 : 1;
}
