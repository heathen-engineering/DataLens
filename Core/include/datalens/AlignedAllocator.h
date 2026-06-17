/******************************************************************************
 * AlignedAllocator.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * A minimal C++17 std-compatible allocator that returns over-aligned storage
 * (default: a 64-byte cache line). DataStore uses it for column buffers so that
 * each column starts on a cache-line boundary: two distinct cache-line-aligned,
 * non-overlapping allocations can never share a cache line, which eliminates
 * false sharing between Systems that write different columns concurrently
 * (Coding Law 4 / the commit partitioning story, DataLens-Spec.md §7.1.4).
 ******************************************************************************/

#pragma once

#include <cstddef>
#include <new>

namespace datalens
{
    template <typename T, std::size_t Alignment = 64>
    struct AlignedAllocator
    {
        using value_type = T;
        static constexpr std::size_t alignment = Alignment;

        AlignedAllocator() noexcept = default;

        template <typename U>
        AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

        template <typename U>
        struct rebind { using other = AlignedAllocator<U, Alignment>; };

        T* allocate(std::size_t n)
        {
            if (n == 0)
                return nullptr;
            // C++17 over-aligned operator new. Throws std::bad_alloc on failure (std contract).
            return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t(Alignment)));
        }

        void deallocate(T* p, std::size_t /*n*/) noexcept
        {
            ::operator delete(p, std::align_val_t(Alignment));
        }
    };

    // Stateless: all instances are interchangeable, so vectors can move/swap their storage freely.
    template <typename T, typename U, std::size_t A>
    bool operator==(const AlignedAllocator<T, A>&, const AlignedAllocator<U, A>&) noexcept { return true; }

    template <typename T, typename U, std::size_t A>
    bool operator!=(const AlignedAllocator<T, A>&, const AlignedAllocator<U, A>&) noexcept { return false; }
}
