#include "RealtimeTrap.h"

#include <atomic>

namespace jaudios::rt
{

namespace
{
#if defined(__GNUC__)
    // initial-exec: static TLS, so reading the flag from inside malloc never allocates.
    __attribute__ ((tls_model ("initial-exec"))) thread_local bool insideSection = false;
#else
    thread_local bool insideSection = false;
#endif

    std::atomic<std::uint64_t> allocations { 0 };
    std::atomic<std::uint64_t> deallocations { 0 };
    std::atomic<std::uint64_t> mutexLocks { 0 };
} // namespace

ScopedRealtimeSection::ScopedRealtimeSection() noexcept : wasInside (insideSection) { insideSection = true; }
ScopedRealtimeSection::~ScopedRealtimeSection() noexcept { insideSection = wasInside; }

bool isInRealtimeSection() noexcept { return insideSection; }

Stats getStats() noexcept
{
    return { allocations.load(), deallocations.load(), mutexLocks.load() };
}

void resetStats() noexcept
{
    allocations = 0;
    deallocations = 0;
    mutexLocks = 0;
}

namespace detail
{
    ViolationHook violationHook = nullptr;

    static void report (const char* what) noexcept
    {
        if (violationHook != nullptr)
        {
            insideSection = false; // the hook itself may allocate (backtrace printing)
            violationHook (what);
            insideSection = true;
        }
    }

    void countAllocation() noexcept   { if (insideSection) { allocations.fetch_add (1, std::memory_order_relaxed); report ("allocation"); } }
    void countDeallocation() noexcept { if (insideSection) { deallocations.fetch_add (1, std::memory_order_relaxed); report ("deallocation"); } }
    void countMutexLock() noexcept    { if (insideSection) { mutexLocks.fetch_add (1, std::memory_order_relaxed); report ("mutex lock"); } }
} // namespace detail

} // namespace jaudios::rt

// Set by RealtimeTrapHooks.cpp's static initializer, which only rt_check binaries link. (A weak
// undefined symbol would be simpler, but Apple's linker rejects it when nothing defines it.)
namespace jaudios::rt
{
namespace
{
    std::atomic<bool> trapInstalled { false };
}

bool isTrapInstalled() noexcept { return trapInstalled.load(); }

namespace detail
{
    void markTrapInstalled() noexcept { trapInstalled = true; }
}
} // namespace jaudios::rt
