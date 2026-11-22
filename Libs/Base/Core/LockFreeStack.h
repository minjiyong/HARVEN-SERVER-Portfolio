#pragma once

#include <atomic>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// CLockFreeStack — Windows SList(SLIST_HEADER · InterlockedPush/PopEntrySList)와 같은 lock-free 스택.
//
// · 여러 스레드가 동시에 넣고 뺀다(잠금 없음). 노드는 쓰는 쪽이 들고 있는 LockFreeEntry(침투형 — 할당 없음).
// · 머리 = {맨 위 노드, 꼬리표} 16바이트를 CAS 한 번으로 바꾼다. 꼬리표는 바꿀 때마다 늘어 ABA(같은 주소가 빠졌다가
//   다시 들어와 머리가 "그대로"처럼 보이는 일)를 막는다 — SList의 Sequence와 같은 역할.
// · 노드 메모리는 스택을 쓰는 동안 운영체제에 돌려주지 않는다(풀 전용 — CObjectPool). 빼는 쪽이 그사이 빠져나간 노드의
//   next를 읽어도 안전하고, 그 값이 낡았으면 CAS가 실패해 다시 읽는다.
// · x86-64: cmpxchg16b(-mcx16)를 바로 쓴다. 그 밖(예: aarch64)은 std::atomic 16바이트(libatomic).
// ─────────────────────────────────────────────────────────────────────────────
// x86-64는 모든 번역 단위가 -mcx16으로 빌드돼야 한다 — 섞이면 같은 클래스가 두 가지 모양(cmpxchg16b · libatomic)으로 만들어진다
#if defined(__x86_64__) && !defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
#error "x86-64에서는 -mcx16으로 빌드한다(CMakeLists.txt가 넣는다)"
#endif

struct LockFreeEntry
{
    std::atomic<LockFreeEntry*> next{ nullptr };
};

class CLockFreeStack
{
public:
    CLockFreeStack() noexcept = default;
    CLockFreeStack(const CLockFreeStack&) = delete;
    CLockFreeStack& operator=(const CLockFreeStack&) = delete;

    void Push(LockFreeEntry* entry) noexcept
    {
        Head current = Load();
        while (true)
        {
            entry->next.store(current.top, std::memory_order_relaxed);
            const Head desired{ entry, current.tag + 1 };
            if (CompareExchange(current, desired))   // 실패하면 current가 지금 머리로 바뀐다
                return;
        }
    }

    [[nodiscard]] LockFreeEntry* Pop() noexcept
    {
        Head current = Load();
        while (current.top != nullptr)
        {
            const Head desired{ current.top->next.load(std::memory_order_relaxed), current.tag + 1 };
            if (CompareExchange(current, desired))
                return current.top;
        }
        return nullptr;
    }

    // 지금 비었는가(다른 스레드가 바로 바꿀 수 있는 근사값)
    [[nodiscard]] bool EmptyApprox() const noexcept { return Load().top == nullptr; }

private:
    struct alignas(16) Head
    {
        LockFreeEntry* top = nullptr;
        std::uint64_t  tag = 0;
    };
    static_assert(sizeof(Head) == 16);

#if defined(__x86_64__)
    using Raw = unsigned __int128;

    [[nodiscard]] static Raw ToRaw(const Head& h) noexcept
    {
        return static_cast<Raw>(reinterpret_cast<std::uintptr_t>(h.top)) | (static_cast<Raw>(h.tag) << 64);
    }
    [[nodiscard]] static Head FromRaw(Raw r) noexcept
    {
        return Head{ reinterpret_cast<LockFreeEntry*>(static_cast<std::uintptr_t>(r)), static_cast<std::uint64_t>(r >> 64) };
    }

    // 16바이트를 한 번에 읽는다(같은 값으로 바꾸는 CAS — 머리가 무엇이든 그 값을 돌려받는다)
    [[nodiscard]] Head Load() const noexcept
    {
        Raw* raw = const_cast<Raw*>(&m_Head);
        return FromRaw(__sync_val_compare_and_swap(raw, Raw{ 0 }, Raw{ 0 }));
    }

    // 머리가 expected면 desired로 바꾸고 true. 아니면 expected를 지금 머리로 바꾸고 false. 전체 장벽(lock cmpxchg16b).
    bool CompareExchange(Head& expected, const Head& desired) noexcept
    {
        const Raw before = ToRaw(expected);
        const Raw seen = __sync_val_compare_and_swap(&m_Head, before, ToRaw(desired));
        if (seen == before)
            return true;
        expected = FromRaw(seen);
        return false;
    }

    alignas(16) Raw m_Head = 0;
#else
    [[nodiscard]] Head Load() const noexcept { return m_Head.load(std::memory_order_acquire); }

    bool CompareExchange(Head& expected, const Head& desired) noexcept
    {
        return m_Head.compare_exchange_weak(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    std::atomic<Head> m_Head{};
#endif
};
