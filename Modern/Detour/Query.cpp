// Copyright (c) 2009 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
// claim that you wrote the original software. If you use this software
// in a product, an acknowledgment in the product documentation would be
// appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
// misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
// Copyright (c) 2026 Samp-reloaded contributors.
// This altered implementation is part of the Samp-reloaded fork.
module recastnav.query;
import std;
#include "Geometry.hpp"

namespace recastnav {
namespace {
class WorkspaceGuard final {
public:
    explicit WorkspaceGuard(std::atomic_flag& flag) noexcept : m_flag{flag} {}
    ~WorkspaceGuard() { m_flag.clear(std::memory_order_release); }
    WorkspaceGuard(const WorkspaceGuard&) = delete;
    WorkspaceGuard& operator=(const WorkspaceGuard&) = delete;
private:
    std::atomic_flag& m_flag;
};
}
std::expected<void, QueryError> QueryFilter::SetAreaCost(std::uint32_t area, double cost) noexcept {
    if (area >= m_costs.size() || !std::isfinite(cost) || cost <= 0.0) { return std::unexpected{QueryError::InvalidFilter}; }
    m_costs[area] = cost; return {};
}
std::expected<void, QueryError> QueryFilter::SetAreaEnabled(std::uint32_t area, bool enabled) noexcept {
    if (area >= m_enabled.size()) { return std::unexpected{QueryError::InvalidFilter}; }
    m_enabled[area] = enabled; return {};
}
bool QueryFilter::Accepts(std::uint32_t area, std::span<const std::uint32_t> categories) const noexcept {
    if (area >= m_enabled.size() || !m_enabled[area]) { return false; }
    for (const auto value : categories) {
        if (std::ranges::find(m_excluded, value) != m_excluded.end()) { return false; }
    }
    if (m_required.empty()) { return true; }
    for (const auto value : categories) {
        if (std::ranges::find(m_required, value) != m_required.end()) { return true; }
    }
    return false;
}
bool QueryFilter::Accepts(const Polygon& polygon) const noexcept {
    return polygon.IsEnabled() && Accepts(polygon.GetArea(), polygon.GetCategories());
}
bool QueryFilter::Accepts(const OffMeshConnection& connection) const noexcept {
    return Accepts(connection.GetArea(), connection.GetCategories());
}
double QueryFilter::GetMinimumCost() const noexcept {
    double minimum{std::numeric_limits<double>::infinity()};
    for (std::size_t i{}; i < m_costs.size(); ++i) { if (m_enabled[i]) { minimum = std::min(minimum, m_costs[i]); } }
    return std::isfinite(minimum) ? minimum : 0.0;
}
std::expected<std::unique_ptr<QueryWorkspace>, QueryError> QueryWorkspace::Create(QueryLimits limits) noexcept {
    if (!limits.IsValid()) { return std::unexpected{QueryError::InvalidLimits}; }
    try {
        auto resource{std::make_shared<std::pmr::unsynchronized_pool_resource>(std::pmr::new_delete_resource())};
        return CreateWithResource(std::move(resource), limits);
    } catch (const std::bad_alloc&) { return std::unexpected{QueryError::AllocationFailure}; }
}
std::expected<std::unique_ptr<QueryWorkspace>, QueryError> QueryWorkspace::CreateWithResource(
    std::shared_ptr<std::pmr::memory_resource> resource, QueryLimits limits) noexcept {
    if (!limits.IsValid()) { return std::unexpected{QueryError::InvalidLimits}; }
    if (!resource) { return std::unexpected{QueryError::InvalidResourceLease}; }
    try {
        return std::make_unique<QueryWorkspace>(std::move(resource), limits, ConstructionKey{});
    } catch (const std::bad_alloc&) { return std::unexpected{QueryError::AllocationFailure}; }
      catch (const std::length_error&) { return std::unexpected{QueryError::CapacityExceeded}; }
}
std::expected<std::optional<NearestPolygon>, QueryError> QueryWorkspace::FindNearestPolygon(
    const SnapshotLease& snapshot, const Vec3& position, const Vec3& halfExtents,
    const QueryFilter& filter, std::stop_token cancellation) noexcept {
    if (m_inUse.test_and_set(std::memory_order_acquire)) { return std::unexpected{QueryError::WorkspaceBusy}; }
    const WorkspaceGuard guard{m_inUse};
    if (!snapshot) { return std::unexpected{QueryError::MissingSnapshotLease}; }
    if (!position.IsFinite() || !halfExtents.IsFinite()
        || halfExtents.GetX() < 0 || halfExtents.GetY() < 0 || halfExtents.GetZ() < 0) {
        return std::unexpected{QueryError::InvalidGeometry};
    }
    std::optional<NearestPolygon> nearest{};
    double distance{std::numeric_limits<double>::infinity()};
    const auto graph{snapshot->ReadGraph()};
    if (cancellation.stop_requested()) { return std::unexpected{QueryError::Cancelled}; }
    for (std::size_t i{}; i < graph.GetNodeCount(); ++i) {
        if (cancellation.stop_requested()) { return std::unexpected{QueryError::Cancelled}; }
        const auto node{*graph.GetNode(MeshNodeIndex{i})}; const auto& tile{node.GetTile()};
        if (!filter.Accepts(tile.GetPolygons()[node.GetPolygonIndex().GetValue()]) || !IntersectsSearch(node.GetBounds(), position, halfExtents)) { continue; }
        const auto point{ClosestSurface(tile, node.GetPolygonIndex(), position)};
        const double candidate{DistanceSquared(point, position)};
        if (candidate < distance) { nearest = NearestPolygon{*graph.LocateNode(MeshNodeIndex{i}), point, candidate}; distance = candidate; }
    }
    return nearest;
}
bool QueryWorkspace::Before(std::size_t left, std::size_t right) const noexcept {
    const auto& a{m_nodes[left]}; const auto& b{m_nodes[right]};
    const double aTotal{a.cost + a.heuristic}, bTotal{b.cost + b.heuristic};
    if (aTotal != bTotal) { return aTotal < bTotal; }
    if (a.heuristic != b.heuristic) { return a.heuristic < b.heuristic; }
    return a.polygon < b.polygon;
}
void QueryWorkspace::SwapHeap(std::size_t left, std::size_t right) noexcept {
    std::swap(m_heap[left], m_heap[right]);
    m_nodes[m_heap[left]].heapPosition = left; m_nodes[m_heap[right]].heapPosition = right;
}
void QueryWorkspace::UpHeap(std::size_t position) noexcept {
    while (position > 0u) {
        const std::size_t parent{(position - 1u) / 2u};
        if (!Before(m_heap[position], m_heap[parent])) { break; }
        SwapHeap(position, parent); position = parent;
    }
}
void QueryWorkspace::PushOrUpdate(std::size_t node) {
    if (m_nodes[node].heapPosition) { UpHeap(*m_nodes[node].heapPosition); return; }
    const std::size_t position{m_heap.size()}; m_heap.push_back(node);
    m_nodes[node].heapPosition = position; UpHeap(position);
}
std::size_t QueryWorkspace::PopHeap() noexcept {
    const std::size_t node{m_heap.front()};
    m_nodes[node].heapPosition.reset();
    if (m_heap.size() == 1u) { m_heap.pop_back(); return node; }
    m_heap[0] = m_heap.back(); m_heap.pop_back(); m_nodes[m_heap[0]].heapPosition = 0u;
    std::size_t position{};
    while (position < m_heap.size() / 2u) {
        const std::size_t left{position * 2u + 1u};
        const std::size_t right{left + 1u};
        std::size_t next{left};
        if (right < m_heap.size() && Before(m_heap[right], m_heap[left])) { next = right; }
        if (!Before(m_heap[next], m_heap[position])) { break; }
        SwapHeap(position, next); position = next;
    }
    return node;
}
std::expected<CorridorResult, QueryError> QueryWorkspace::FindPath(const PolygonLocation& start,
    const PolygonLocation& goal, const QueryFilter& filter, SearchAlgorithm algorithm, std::stop_token cancellation) noexcept {
    if (m_inUse.test_and_set(std::memory_order_acquire)) { return std::unexpected{QueryError::WorkspaceBusy}; }
    const WorkspaceGuard guard{m_inUse};
    if (!start.GetSnapshot() || !goal.GetSnapshot()) { return std::unexpected{QueryError::MissingSnapshotLease}; }
    if (start.GetSnapshot() != goal.GetSnapshot()) { return std::unexpected{QueryError::SnapshotMismatch}; }
    if (algorithm != SearchAlgorithm::AStar && algorithm != SearchAlgorithm::Dijkstra) { return std::unexpected{QueryError::InvalidFilter}; }
    try {
        return Search(start, goal, filter, algorithm, cancellation);
    } catch (const std::bad_alloc&) { return std::unexpected{QueryError::AllocationFailure}; }
      catch (const std::length_error&) { return std::unexpected{QueryError::CapacityExceeded}; }
}
std::expected<CorridorResult, QueryError> QueryWorkspace::Search(const PolygonLocation& start,
    const PolygonLocation& goal, const QueryFilter& filter, SearchAlgorithm algorithm, std::stop_token cancellation) {
    const auto graph{start.GetSnapshot()->ReadGraph()};
    const auto sourceIndex{graph.GetNodeIndex(start)}, destinationIndex{graph.GetNodeIndex(goal)};
    if (!sourceIndex || !destinationIndex) { return std::unexpected{QueryError::SnapshotMismatch}; }
    const std::size_t source{sourceIndex->GetValue()}, destination{destinationIndex->GetValue()};
    const auto polygon = [&](std::size_t node) -> const Polygon& {
        const auto entry{*graph.GetNode(MeshNodeIndex{node})}; return entry.GetTile().GetPolygons()[entry.GetPolygonIndex().GetValue()];
    };
    if (!filter.Accepts(polygon(source)) || !filter.Accepts(polygon(destination))) { return std::unexpected{QueryError::FilteredEndpoint}; }
    m_nodes.clear(); m_heap.clear(); m_lookup.clear(); m_reverse.clear();
    const auto goalNode{*graph.GetNode(MeshNodeIndex{destination})};
    const auto remainingDistance = [&](std::size_t node) { return Distance(graph.GetNode(MeshNodeIndex{node})->GetCentre(), goalNode.GetCentre()); };
    const double minimumCost{filter.GetMinimumCost()};
    const auto heuristic = [&](std::size_t node) { return algorithm == SearchAlgorithm::AStar ? remainingDistance(node) * minimumCost : 0.0; };
    const double sourceHeuristic{heuristic(source)};
    if (!std::isfinite(sourceHeuristic)) { return std::unexpected{QueryError::CostOverflow}; }
    m_nodes.push_back(SearchNode{source, 0.0, sourceHeuristic, {}, {}, {}});
    m_lookup.emplace(source, 0u); PushOrUpdate(0u);
    std::size_t best{}, expansions{};
    double bestDistance{remainingDistance(source)};
    PathTermination termination{PathTermination::Unreachable};
    bool capacityReached{};
    while (!m_heap.empty()) {
        if (cancellation.stop_requested()) { termination = PathTermination::Cancelled; break; }
        if (expansions == m_limits.GetMaximumExpansions()) { termination = PathTermination::ExpansionCapacity; break; }
        const std::size_t currentIndex{PopHeap()};
        ++expansions;
        const std::size_t current{m_nodes[currentIndex].polygon};
        if (current == destination) { best = currentIndex; termination = PathTermination::ReachedGoal; break; }
        const auto& currentPolygon{polygon(current)};
        const auto graphNode{*graph.GetNode(MeshNodeIndex{current})};
        for (std::size_t arcIndex{}; arcIndex < graphNode.GetArcCount(); ++arcIndex) {
            if (cancellation.stop_requested()) { termination = PathTermination::Cancelled; break; }
            const auto arc{*graphNode.GetArc(MeshArcIndex{arcIndex})};
            const std::size_t nextPolygonIndex{arc.GetDestination().GetValue()};
            const auto& nextPolygon{polygon(nextPolygonIndex)};
            if (!filter.Accepts(nextPolygon)) { continue; }
            double segmentCost{filter.GetAreaCost(currentPolygon.GetArea())};
            if (const auto offMesh{arc.GetOffMeshConnection()}) {
                const auto& connection{offMesh->get()};
                if (!filter.Accepts(connection)) { continue; }
                segmentCost = filter.GetAreaCost(connection.GetArea());
            }
            const auto& departure{arc.GetTransition().GetDeparture()}; const auto& arrival{arc.GetTransition().GetArrival()};
            const double step{Distance(graphNode.GetCentre(), departure) * filter.GetAreaCost(currentPolygon.GetArea())
                + Distance(departure, arrival) * segmentCost
                + Distance(arrival, graph.GetNode(MeshNodeIndex{nextPolygonIndex})->GetCentre()) * filter.GetAreaCost(nextPolygon.GetArea())};
            const double cost{m_nodes[currentIndex].cost + step}, estimate{heuristic(nextPolygonIndex)};
            if (!std::isfinite(cost) || !std::isfinite(estimate) || !std::isfinite(cost + estimate)) {
                return std::unexpected{QueryError::CostOverflow};
            }
            const auto found{m_lookup.find(nextPolygonIndex)};
            std::size_t nextIndex{};
            if (found == m_lookup.end()) {
                if (m_nodes.size() == m_limits.GetMaximumNodes()) { capacityReached = true; continue; }
                nextIndex = m_nodes.size();
                m_nodes.push_back(SearchNode{nextPolygonIndex, cost, estimate, currentIndex, arcIndex, {}});
                m_lookup.emplace(nextPolygonIndex, nextIndex);
            } else {
                nextIndex = found->second;
                if (cost >= m_nodes[nextIndex].cost) { continue; }
                m_nodes[nextIndex].cost = cost; m_nodes[nextIndex].parent = currentIndex;
                m_nodes[nextIndex].parentArc = arcIndex;
            }
            PushOrUpdate(nextIndex);
            const double distance{remainingDistance(nextPolygonIndex)};
            if (distance < bestDistance || (distance == bestDistance
                && (cost < m_nodes[best].cost || (cost == m_nodes[best].cost && nextPolygonIndex < m_nodes[best].polygon)))) {
                best = nextIndex; bestDistance = distance;
            }
        }
        if (termination == PathTermination::Cancelled) { break; }
    }
    if (capacityReached && (termination == PathTermination::Unreachable || termination == PathTermination::ReachedGoal)) {
        termination = PathTermination::NodeCapacity;
    }
    for (std::optional<std::size_t> node{best}; node; node = m_nodes[*node].parent) { m_reverse.push_back(*node); }
    std::reverse(m_reverse.begin(), m_reverse.end());
    if (m_reverse.size() > m_limits.GetMaximumCorridor()) {
        m_reverse.resize(m_limits.GetMaximumCorridor()); termination = PathTermination::CorridorCapacity;
    }
    std::vector<PolygonLocation> polygons{}; std::vector<MeshTransition> transitions{};
    polygons.reserve(m_reverse.size()); transitions.reserve(m_reverse.size() - 1u);
    for (std::size_t i{}; i < m_reverse.size(); ++i) {
        const auto& node{m_nodes[m_reverse[i]]}; polygons.push_back(*graph.LocateNode(MeshNodeIndex{node.polygon}));
        if (i > 0u) {
            const auto& parent{m_nodes[m_reverse[i - 1u]]};
            const auto parentNode{*graph.GetNode(MeshNodeIndex{parent.polygon})};
            transitions.push_back(parentNode.GetArc(MeshArcIndex{*node.parentArc})->GetTransition());
        }
    }
    const PathStatus status{termination == PathTermination::ReachedGoal ? PathStatus::Complete : PathStatus::Partial};
    return CorridorResult{status, termination, std::move(polygons), std::move(transitions),
        m_nodes[m_reverse.back()].cost, expansions};
}
}
