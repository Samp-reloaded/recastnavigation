// Copyright (c) 2026 Samp-reloaded contributors.
// Altered implementation; distributed under the repository's zlib license.
#include "LegacyAllocation.hpp"
#include "RecastAlloc.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <new>

namespace recastnav::cooking {
struct alignas(std::max_align_t) AllocationHeader final {
    std::size_t bytes{};
    AllocationHeader* previous{};
    AllocationHeader* next{};
};
namespace { thread_local AllocationSession* activeSession{}; }
AllocationSession::AllocationSession(std::pmr::memory_resource& resource, std::size_t maximumBytes,
    std::stop_token cancellation) noexcept : m_resource{resource}, m_maximumBytes{maximumBytes},
    m_cancellation{cancellation}, m_previous{activeSession} { activeSession = this; }
AllocationSession::~AllocationSession() {
    while (m_first) { ReleaseLegacy(m_first + 1); }
    assert(activeSession == this);
    activeSession = m_previous;
}
AllocationSession& AllocationSession::Current() { assert(activeSession); return *activeSession; }
void* AllocationSession::do_allocate(std::size_t bytes, std::size_t alignment) {
    if (m_cancellation.stop_requested()) { throw CookingCancelled{}; }
    if (bytes > m_maximumBytes - m_bytes) { throw ScratchBudgetExceeded{}; }
    void* allocation{m_resource.allocate(bytes, alignment)};
    m_bytes += bytes;
    return allocation;
}
void AllocationSession::do_deallocate(void* allocation, std::size_t bytes, std::size_t alignment) {
    assert(bytes <= m_bytes);
    m_resource.deallocate(allocation, bytes, alignment);
    m_bytes -= bytes;
}
bool AllocationSession::do_is_equal(const std::pmr::memory_resource& other) const noexcept { return this == &other; }
void* AllocationSession::AllocateLegacy(std::size_t bytes) {
    const auto payload{std::max<std::size_t>(bytes, 1u)};
    if (payload > std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader)) { throw ScratchBudgetExceeded{}; }
    const auto total{payload + sizeof(AllocationHeader)};
    auto* header{std::construct_at(static_cast<AllocationHeader*>(do_allocate(total, alignof(AllocationHeader))),
        AllocationHeader{total, nullptr, m_first})};
    if (m_first) { m_first->previous = header; }
    m_first = header;
    return header + 1;
}
void AllocationSession::ReleaseLegacy(void* allocation) noexcept {
    if (!allocation) { return; }
    auto* header{static_cast<AllocationHeader*>(allocation) - 1};
    if (header->previous) { header->previous->next = header->next; } else { m_first = header->next; }
    if (header->next) { header->next->previous = header->previous; }
    const auto bytes{header->bytes};
    std::destroy_at(header);
    do_deallocate(header, bytes, alignof(AllocationHeader));
}
}

// Only the private cooker oracle references these names. No process-global allocator mutation.
void* recastnavCookAlloc(std::size_t size, rcAllocHint) {
    return recastnav::cooking::AllocationSession::Current().AllocateLegacy(size);
}
void recastnavCookFree(void* allocation) {
    recastnav::cooking::AllocationSession::Current().ReleaseLegacy(allocation);
}
