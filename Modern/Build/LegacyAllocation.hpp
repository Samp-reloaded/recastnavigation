// Copyright (c) 2026 Samp-reloaded contributors.
// Altered implementation; distributed under the repository's zlib license.
#pragma once
#include <cstddef>
#include <memory_resource>
#include <stop_token>

namespace recastnav::cooking {
struct ScratchBudgetExceeded final {};
struct CookingCancelled final {};
struct AllocationHeader;
// Private compatibility boundary. Tracks allocations even when legacy raw local storage
// would otherwise escape its ordinary error-return cleanup during exception unwinding.
class AllocationSession final : public std::pmr::memory_resource {
public:
    AllocationSession(std::pmr::memory_resource& resource, std::size_t maximumBytes, std::stop_token cancellation) noexcept;
    ~AllocationSession() override;
    AllocationSession(const AllocationSession&) = delete;
    AllocationSession& operator=(const AllocationSession&) = delete;
    [[nodiscard]] void* AllocateLegacy(std::size_t bytes);
    void ReleaseLegacy(void* allocation) noexcept;
    [[nodiscard]] static AllocationSession& Current();
private:
    [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void* allocation, std::size_t bytes, std::size_t alignment) override;
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;
    std::pmr::memory_resource& m_resource;
    std::size_t m_maximumBytes{}, m_bytes{};
    std::stop_token m_cancellation;
    AllocationHeader* m_first{};
    AllocationSession* m_previous{};
};
}
