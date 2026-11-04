#pragma once

#include <cstdint>

/** Realtime-safety instrumentation used by tools/rt_check.sh.

    Every audio-thread entry point (AudioEngine::renderBlock) opens a ScopedRealtimeSection. That
    costs one thread_local store. When the binary is linked with RealtimeTrapHooks.cpp
    (JAUDIOS_RT_CHECK=ON, rt_check target only), malloc/free/operator new/delete and
    pthread_mutex_lock are interposed and every call made inside a realtime section is counted.
*/
namespace jaudios::rt
{

struct Stats
{
    std::uint64_t allocations = 0;   // malloc / calloc / realloc / operator new / aligned variants
    std::uint64_t deallocations = 0; // free / operator delete
    std::uint64_t mutexLocks = 0;    // pthread_mutex_lock / trylock (glibc only)
};

class ScopedRealtimeSection
{
public:
    ScopedRealtimeSection() noexcept;
    ~ScopedRealtimeSection() noexcept;
    ScopedRealtimeSection (const ScopedRealtimeSection&) = delete;
    ScopedRealtimeSection& operator= (const ScopedRealtimeSection&) = delete;

private:
    bool wasInside;
};

bool isInRealtimeSection() noexcept;

/** True only in binaries linked with the interposing hooks. */
bool isTrapInstalled() noexcept;

Stats getStats() noexcept;
void resetStats() noexcept;

namespace detail
{
    /** Optional diagnostic callback (installed by the hooks when JAUDIOS_RT_BACKTRACE=1). */
    using ViolationHook = void (*) (const char* what) noexcept;
    extern ViolationHook violationHook;

    void countAllocation() noexcept;
    void countDeallocation() noexcept;
    void countMutexLock() noexcept;
    void markTrapInstalled() noexcept; // called by RealtimeTrapHooks.cpp at static-init time
}

} // namespace jaudios::rt
