#pragma once

#include <atomic>
#include <cstdint>
#include <new>
#include <utility>

#include "Base/Core/ObjectPool.h"

// lightweightsemaphore.h는 assert · 이 매크로를 concurrentqueue.h에서 받는다고 가정한다 — 혼자 쓸 때는 미리 갖춘다
#include <cassert>
#ifndef MOODYCAMEL_DELETE_FUNCTION
#define MOODYCAMEL_DELETE_FUNCTION = delete
#endif
#include "ThirdParty/moodycamel/lightweightsemaphore.h"
// 시간 기다림(Wait(timeout))은 단조 시계여야 한다 — 벽시계면 NTP가 시계를 뒤로 돌릴 때 그만큼 더 잔다.
// lightweightsemaphore.h는 glibc 2.30+ · _GNU_SOURCE(g++ · clang++ 기본)에서 sem_clockwait(CLOCK_MONOTONIC)을 고른다
#ifndef MOODYCAMEL_LIGHTWEIGHTSEMAPHORE_MONOTONIC
#error "LightweightSemaphore가 벽시계로 기다린다 — glibc 2.30 이상 · _GNU_SOURCE가 필요하다"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// CSafeMessageQueue<T> — 여러 스레드가 넣고 한 스레드(주인)가 꺼내는 작업 큐(MPSC). Zone · 서비스 스레드의 메일함.
//
// · 넣기는 잠금 없이 원자 교환 한 번(Vyukov MPSC). 넣은 순서가 그대로 한 줄이다 — 다른 스레드가 넣었어도 "먼저 넣은 것이
//   먼저"가 지켜진다(Zone 이동: A가 B에 넣은 입장 처리가 그 뒤 IO 스레드가 B에 넣은 패킷보다 먼저 돈다). 생산자마다 따로
//   줄을 세우는 큐(moodycamel ConcurrentQueue)는 이 순서를 지키지 않아 여기엔 쓰지 않는다.
// · 노드는 CObjectPool에서 꺼내 쓴다(넣을 때마다 malloc하지 않는다).
// · 주인이 할 일이 없어 잘 때만 깨운다: 주인이 "잔다"를 적은 뒤 큐를 한 번 더 보고, 넣는 쪽은 넣은 뒤 그 표시를 본다
//   (둘 다 seq_cst — 데커 방식이라 "넣었는데 아무도 안 깨움"이 없다). 깨우기는 moodycamel LightweightSemaphore
//   (잠깐 돌며 기다린 뒤 futex로 잔다). 주인이 일하는 동안 넣기는 세마포어를 건드리지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
class CSafeMessageQueue
{
public:
    // maxSpins: 잠들기 전에 깨우기를 돌며 기다리는 횟수(moodycamel 기본 10000은 스레드가 많은 서버에서 CPU를 태운다)
    explicit CSafeMessageQueue(int maxSpins = 64)
        : m_Sema(0, maxSpins)
    {
        Node* stub = CObjectPool<Node>::Acquire();
        m_Head.store(stub, std::memory_order_relaxed);
        m_pTail = stub;
    }

    ~CSafeMessageQueue()
    {
        T value;
        while (TryPop(value)) {}
        CObjectPool<Node>::Release(m_pTail);
    }

    CSafeMessageQueue(const CSafeMessageQueue&) = delete;
    CSafeMessageQueue& operator=(const CSafeMessageQueue&) = delete;

    // 아무 스레드
    void Push(T value)
    {
        Node* node = CObjectPool<Node>::Acquire();
        ::new (node->storage) T(std::move(value));
        Node* prev = m_Head.exchange(node, std::memory_order_acq_rel);
        // 이 저장과 아래 m_bSleeping 읽기는 seq_cst — 주인의 "잔다 적기 → 큐 보기"와 엇갈려 둘 다 못 보는 일이 없다
        prev->next.store(node, std::memory_order_seq_cst);
        if (m_bSleeping.load(std::memory_order_seq_cst) && m_bSleeping.exchange(false, std::memory_order_seq_cst))
            m_Sema.signal();
    }

    // ── 주인 스레드만 ──────────────────────────────────────────────────────────
    [[nodiscard]] bool TryPop(T& out)
    {
        Node* tail = m_pTail;
        Node* next = tail->next.load(std::memory_order_acquire);
        if (next == nullptr)
            return false;   // 비었다(또는 넣는 쪽이 교환과 연결 사이 — 연결한 뒤 깨운다)
        T* value = next->Value();
        out = std::move(*value);
        value->~T();
        m_pTail = next;                        // next가 새 머리 자리(값 없는 노드)가 된다
        CObjectPool<Node>::Release(tail);
        return true;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return m_pTail->next.load(std::memory_order_acquire) == nullptr;
    }

    // 큐가 비어 있으면 무언가 들어오거나 timeoutUsecs(음수면 끝없이)가 지날 때까지 잔다. 깨어난 뒤 큐는 비어 있을 수도 있다.
    void Wait(std::int64_t timeoutUsecs = -1)
    {
        m_bSleeping.store(true, std::memory_order_seq_cst);
        if (m_pTail->next.load(std::memory_order_seq_cst) != nullptr)
        {
            // 그새 들어왔다. 넣는 쪽이 표시를 먼저 가져갔다면 그쪽이 보낼(보낸) 깨우기를 하나 받아 짝을 맞춘다
            if (!m_bSleeping.exchange(false, std::memory_order_seq_cst))
                m_Sema.wait();
            return;
        }
        const bool signaled = timeoutUsecs < 0 ? m_Sema.wait() : m_Sema.wait(timeoutUsecs);
        if (!signaled && !m_bSleeping.exchange(false, std::memory_order_seq_cst))
            m_Sema.wait();   // 시간이 다 된 순간 넣는 쪽이 표시를 가져갔다 — 곧 오는 깨우기를 받는다
    }

private:
    struct Node
    {
        std::atomic<Node*> next{ nullptr };
        alignas(T) unsigned char storage[sizeof(T)];

        [[nodiscard]] T* Value() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }
    };

    // 쓰는 쪽이 다른 값은 캐시 줄을 나눈다 — 주인이 꺼낼 때마다 바꾸는 m_pTail과 넣는 쪽이 넣을 때마다 읽는 m_bSleeping이
    // 한 줄에 있으면 넣기 · 꺼내기마다 그 줄이 코어 사이를 오간다
    alignas(64) std::atomic<Node*> m_Head{ nullptr };      // 넣는 쪽(여럿)이 바꾼다
    alignas(64) Node* m_pTail = nullptr;                    // 주인만
    alignas(64) std::atomic<bool> m_bSleeping{ false };    // 주인이 잘 때만 바꾼다(넣는 쪽은 읽기만)
    moodycamel::LightweightSemaphore m_Sema;
};
