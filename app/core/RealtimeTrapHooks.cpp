// Interposers for the realtime trap. Linked ONLY into the rt_check executable
// (JAUDIOS_RT_CHECK=ON). Never ship this.
//
// - Replaces global operator new/delete (portable).
// - On glibc also interposes malloc/calloc/realloc/free/posix_memalign/aligned_alloc and
//   pthread_mutex_lock/trylock, which catches allocations and locks inside JUCE and libc++.

#include "RealtimeTrap.h"

#include <cstddef>
#include <cstdlib>
#include <new>

extern "C" { int jaudios_rt_trap_installed = 1; }

using namespace jaudios::rt::detail;

#if defined(__GLIBC__)
 #include <dlfcn.h>
 #include <execinfo.h>
 #include <pthread.h>
 #include <stdio.h>

extern "C"
{
    void* __libc_malloc (size_t);
    void* __libc_calloc (size_t, size_t);
    void* __libc_realloc (void*, size_t);
    void* __libc_memalign (size_t, size_t);
    void  __libc_free (void*);

    void* malloc (size_t n)                 { countAllocation(); return __libc_malloc (n); }
    void* calloc (size_t n, size_t s)       { countAllocation(); return __libc_calloc (n, s); }
    void* realloc (void* p, size_t n)       { countAllocation(); return __libc_realloc (p, n); }
    void* aligned_alloc (size_t a, size_t n){ countAllocation(); return __libc_memalign (a, n); }
    void* memalign (size_t a, size_t n)     { countAllocation(); return __libc_memalign (a, n); }
    void  free (void* p)                    { if (p != nullptr) countDeallocation(); __libc_free (p); }

    int posix_memalign (void** out, size_t a, size_t n)
    {
        countAllocation();
        *out = __libc_memalign (a, n);
        return *out != nullptr ? 0 : 12 /* ENOMEM */;
    }

    using MutexFn = int (*) (pthread_mutex_t*);

    static MutexFn realLock = nullptr;
    static MutexFn realTryLock = nullptr;

    static void printBacktrace (const char* what) noexcept
    {
        static int printed = 0;
        if (what[0] == 'm' && getenv ("JAUDIOS_RT_BACKTRACE")[0] != '2') // locks only with =2
            return;
        if (printed++ >= 16)
            return;
        void* frames[32];
        const int n = backtrace (frames, 32);
        dprintf (2, "\n[rt_check] %s on audio thread:\n", what);
        backtrace_symbols_fd (frames, n, 2);
    }

    __attribute__ ((constructor)) static void resolveRealMutexFunctions()
    {
        if (const char* env = getenv ("JAUDIOS_RT_BACKTRACE"); env != nullptr && env[0] == '1')
        {
            void* warmup[4];
            backtrace (warmup, 4); // first call loads libgcc; do it outside the realtime section
            jaudios::rt::detail::violationHook = printBacktrace;
        }

        realLock = reinterpret_cast<MutexFn> (dlsym (RTLD_NEXT, "pthread_mutex_lock"));
        realTryLock = reinterpret_cast<MutexFn> (dlsym (RTLD_NEXT, "pthread_mutex_trylock"));
    }

    int pthread_mutex_lock (pthread_mutex_t* m)
    {
        countMutexLock();
        return realLock != nullptr ? realLock (m) : 0;
    }

    int pthread_mutex_trylock (pthread_mutex_t* m)
    {
        countMutexLock();
        return realTryLock != nullptr ? realTryLock (m) : 0;
    }
}

static void* rawAlloc (std::size_t n) { return __libc_malloc (n == 0 ? 1 : n); }
static void* rawAlignedAlloc (std::size_t n, std::size_t a) { return __libc_memalign (a, n == 0 ? 1 : n); }
static void  rawFree (void* p) { __libc_free (p); }
#else
static void* rawAlloc (std::size_t n) { return std::malloc (n == 0 ? 1 : n); }
static void* rawAlignedAlloc (std::size_t n, std::size_t a) { return std::aligned_alloc (a, ((n + a - 1) / a) * a); }
static void  rawFree (void* p) { std::free (p); }
#endif

static void* checkedNew (std::size_t n)
{
    countAllocation();
    if (auto* p = rawAlloc (n))
        return p;
    throw std::bad_alloc();
}

static void* checkedAlignedNew (std::size_t n, std::align_val_t a)
{
    countAllocation();
    if (auto* p = rawAlignedAlloc (n, (std::size_t) a))
        return p;
    throw std::bad_alloc();
}

static void checkedDelete (void* p) noexcept
{
    if (p != nullptr)
        countDeallocation();
    rawFree (p);
}

void* operator new (std::size_t n) { return checkedNew (n); }
void* operator new[] (std::size_t n) { return checkedNew (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept { try { return checkedNew (n); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { try { return checkedNew (n); } catch (...) { return nullptr; } }
void* operator new (std::size_t n, std::align_val_t a) { return checkedAlignedNew (n, a); }
void* operator new[] (std::size_t n, std::align_val_t a) { return checkedAlignedNew (n, a); }

void operator delete (void* p) noexcept { checkedDelete (p); }
void operator delete[] (void* p) noexcept { checkedDelete (p); }
void operator delete (void* p, std::size_t) noexcept { checkedDelete (p); }
void operator delete[] (void* p, std::size_t) noexcept { checkedDelete (p); }
void operator delete (void* p, std::align_val_t) noexcept { checkedDelete (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { checkedDelete (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { checkedDelete (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { checkedDelete (p); }
