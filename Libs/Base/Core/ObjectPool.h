#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#include "Base/Core/LockFreeStack.h"

// ─────────────────────────────────────────────────────────────────────────────
// CObjectPool<T> — 스레드 캐시(매거진) + lock-free 창고. 한 스레드가 만들고 다른 스레드가 돌려주는 객체
// (IO 스레드가 받은 패킷 묶음 → Zone 스레드가 처리 후 반납, 작업 큐 노드)를 malloc 없이 돌려 쓴다.
//
// · 스레드마다 매거진(포인터 kMagazineSize개 배열) 둘을 든다(Bonwick 매거진). 꺼내기 · 넣기는 대부분 이 스레드 배열만
//   만진다 — 원자 연산 없음. 매거진이 비거나 차면 창고(CLockFreeStack, SList와 같은 방식)와 매거진 하나를 통째로 바꾼다.
//   그래서 객체 하나당 lock-free 연산은 1/kMagazineSize번 — SList에 객체를 하나씩 넣고 빼는 것보다 적다.
// · 메모리는 프로세스가 끝날 때까지 돌려주지 않는다(최대 동시 사용량만큼만 남는다). 스레드가 끝나면 그 매거진은 창고로 간다.
// · Acquire는 자리에 T를 만들고(생성자 인자), Release는 T를 부순 뒤 자리를 돌려받는다 — 남은 상태가 다음 사용으로 새지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, std::size_t kMagazineSize = 64>
class CObjectPool
{
public:
    template <typename... Args>
    [[nodiscard]] static T* Acquire(Args&&... args)
    {
        void* slot = LCacheDead ? nullptr : Cache().Pop();
        if (slot == nullptr)
            slot = ::operator new(sizeof(T), std::align_val_t{ alignof(T) });
        return ::new (slot) T(std::forward<Args>(args)...);
    }

    static void Release(T* object) noexcept
    {
        if (object == nullptr)
            return;
        object->~T();
        if (LCacheDead)
        {
            // 이 스레드의 캐시는 이미 부서졌다(스레드 · 프로세스가 끝나는 중 — 정적 객체 소멸자가 반납) — 창고 대신 바로 돌려준다
            ::operator delete(object, std::align_val_t{ alignof(T) });
            return;
        }
        Cache().Push(object);
    }

private:
    struct Magazine
    {
        LockFreeEntry entry;              // 창고(CLockFreeStack)에 걸리는 고리 — 맨 앞이라 Magazine*과 주소가 같다
        std::size_t   count = 0;
        void*         slots[kMagazineSize];
    };
    static_assert(offsetof(Magazine, entry) == 0);

    [[nodiscard]] static Magazine* FromEntry(LockFreeEntry* entry) noexcept { return reinterpret_cast<Magazine*>(entry); }

    // 객체가 든 매거진 · 빈 매거진 창고(모든 스레드 공용)
    [[nodiscard]] static CLockFreeStack& FullDepot() noexcept { static CLockFreeStack s_Full; return s_Full; }
    [[nodiscard]] static CLockFreeStack& EmptyDepot() noexcept { static CLockFreeStack s_Empty; return s_Empty; }

    [[nodiscard]] static Magazine* TakeEmptyMagazine()
    {
        if (LockFreeEntry* entry = EmptyDepot().Pop())
            return FromEntry(entry);
        return new Magazine();
    }

    // 이 스레드의 매거진 둘 — loaded에서 꺼내고 넣는다. previous는 꽉 찼거나 빈 것(바꿔 끼우기용)
    struct ThreadCache
    {
        Magazine* loaded = nullptr;
        Magazine* previous = nullptr;

        ThreadCache() noexcept
        {
            // 창고가 이 캐시보다 먼저 만들어져야 스레드가 끝날 때(이 캐시가 부서질 때) 창고가 살아 있다
            (void)FullDepot();
            (void)EmptyDepot();
        }

        ~ThreadCache()
        {
            for (Magazine* magazine : { loaded, previous })
            {
                if (magazine == nullptr)
                    continue;
                if (magazine->count > 0)
                    FullDepot().Push(&magazine->entry);
                else
                    EmptyDepot().Push(&magazine->entry);
            }
            loaded = previous = nullptr;
            LCacheDead = true;   // 이 뒤의 Acquire · Release는 캐시를 거치지 않는다(창고에 넘긴 매거진을 다시 만지지 않게)
        }

        [[nodiscard]] void* Pop()
        {
            if (loaded != nullptr && loaded->count > 0)
                return loaded->slots[--loaded->count];
            if (previous != nullptr && previous->count > 0)
            {
                std::swap(loaded, previous);
                return loaded->slots[--loaded->count];
            }
            // 둘 다 비었다 — 창고에서 찬 매거진을 받고, 빈 것 하나는 창고로
            LockFreeEntry* entry = FullDepot().Pop();
            if (entry == nullptr)
                return nullptr;
            if (previous != nullptr)
                EmptyDepot().Push(&previous->entry);
            previous = loaded;
            loaded = FromEntry(entry);
            return loaded->slots[--loaded->count];
        }

        void Push(void* slot)
        {
            if (loaded == nullptr)
                loaded = TakeEmptyMagazine();
            if (loaded->count < kMagazineSize)
            {
                loaded->slots[loaded->count++] = slot;
                return;
            }
            if (previous != nullptr && previous->count == 0)
            {
                std::swap(loaded, previous);
                loaded->slots[loaded->count++] = slot;
                return;
            }
            // 둘 다 찼다 — 찬 매거진 하나를 창고로, 빈 것을 새로 끼운다
            if (previous != nullptr)
                FullDepot().Push(&previous->entry);
            previous = loaded;
            loaded = TakeEmptyMagazine();
            loaded->slots[loaded->count++] = slot;
        }
    };

    [[nodiscard]] static ThreadCache& Cache()
    {
        thread_local ThreadCache s_Cache;
        return s_Cache;
    }

    // 소멸자가 없는 스레드 지역 값 — 캐시가 부서진 뒤에도 읽을 수 있다
    static inline thread_local bool LCacheDead = false;
};
