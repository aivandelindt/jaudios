#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <type_traits>

namespace jaudios
{

/** Bounded single-producer / single-consumer queue built on juce::AbstractFifo.

    All storage is allocated up-front (it lives inside the object), so push/pop never allocate
    and never lock. Safe to use from the realtime thread on either end, provided there is exactly
    one producer thread and one consumer thread.
*/
template <typename T, int Capacity>
class SpscQueue
{
    static_assert (std::is_nothrow_copy_assignable_v<T>, "T must be nothrow copy-assignable");
    static_assert (Capacity > 1);

public:
    /** Returns false (and drops the item) if the queue is full. */
    bool push (const T& item) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 + scope.blockSize2 != 1)
            return false;

        storage[(size_t) (scope.blockSize1 == 1 ? scope.startIndex1 : scope.startIndex2)] = item;
        return true;
    }

    bool pop (T& out) noexcept
    {
        const auto scope = fifo.read (1);
        if (scope.blockSize1 + scope.blockSize2 != 1)
            return false;

        out = storage[(size_t) (scope.blockSize1 == 1 ? scope.startIndex1 : scope.startIndex2)];
        return true;
    }

    int getNumReady() const noexcept { return fifo.getNumReady(); }
    static constexpr int capacity() noexcept { return Capacity - 1; } // AbstractFifo keeps one slot free

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, (size_t) Capacity> storage {};
};

} // namespace jaudios
