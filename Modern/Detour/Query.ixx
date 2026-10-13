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
export module recastnav.query;
import std;
export import recastnav.mesh;

export namespace recastnav {
enum class QueryError {
    InvalidLimits, InvalidGeometry, InvalidFilter, MissingSnapshotLease,
    SnapshotMismatch, WorkspaceBusy, InvalidResourceLease, CapacityExceeded,
    AllocationFailure, Cancelled, FilteredEndpoint, CostOverflow,
};
enum class PathStatus { Complete, Partial };
enum class PathTermination { ReachedGoal, Unreachable, NodeCapacity, ExpansionCapacity, CorridorCapacity, Cancelled };
enum class SearchAlgorithm { AStar, Dijkstra };
class QueryLimits final {
public:
    constexpr QueryLimits(std::size_t nodes = 65'536u, std::size_t expansions = 65'536u,
        std::size_t corridor = 65'536u) noexcept : m_nodes{nodes}, m_expansions{expansions}, m_corridor{corridor} {}
    [[nodiscard]] constexpr std::size_t GetMaximumNodes() const noexcept { return m_nodes; }
    [[nodiscard]] constexpr std::size_t GetMaximumExpansions() const noexcept { return m_expansions; }
    [[nodiscard]] constexpr std::size_t GetMaximumCorridor() const noexcept { return m_corridor; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return m_nodes > 0u && m_corridor > 0u; }
private:
    std::size_t m_nodes{}, m_expansions{}, m_corridor{};
};
class QueryFilter final {
public:
    QueryFilter() noexcept { m_costs.fill(1.0); m_enabled.fill(true); }
    [[nodiscard]] std::expected<void, QueryError> SetAreaCost(std::uint32_t area, double cost) noexcept;
    [[nodiscard]] std::expected<void, QueryError> SetAreaEnabled(std::uint32_t area, bool enabled) noexcept;
    // Empty required categories accept all. Nonempty lists require any match;
    // an excluded category always rejects. Categories remain full-width IDs.
    void SetRequiredCategories(std::vector<std::uint32_t> categories) noexcept { m_required = std::move(categories); }
    void SetExcludedCategories(std::vector<std::uint32_t> categories) noexcept { m_excluded = std::move(categories); }
    [[nodiscard]] bool Accepts(const Polygon& polygon) const noexcept;
    [[nodiscard]] bool Accepts(const OffMeshConnection& connection) const noexcept;
    [[nodiscard]] double GetMinimumCost() const noexcept;
private:
    friend class QueryWorkspace;
    [[nodiscard]] double GetAreaCost(std::uint32_t area) const noexcept { return m_costs[area]; }
    [[nodiscard]] bool Accepts(std::uint32_t area, std::span<const std::uint32_t> categories) const noexcept;
    std::array<double, 64> m_costs{};
    std::array<bool, 64> m_enabled{};
    std::vector<std::uint32_t> m_required{}, m_excluded{};
};
class NearestPolygon final {
public:
    NearestPolygon(PolygonLocation location, Vec3 position, double squaredDistance) noexcept
        : m_location{std::move(location)}, m_position{position}, m_squaredDistance{squaredDistance} {}
    [[nodiscard]] const PolygonLocation& GetLocation() const noexcept { return m_location; }
    [[nodiscard]] const Vec3& GetPosition() const noexcept { return m_position; }
    [[nodiscard]] double GetSquaredDistance() const noexcept { return m_squaredDistance; }
private:
    PolygonLocation m_location;
    Vec3 m_position{};
    double m_squaredDistance{};
};
class CorridorResult final {
public:
    CorridorResult(PathStatus status, PathTermination termination, std::vector<PolygonLocation> polygons,
        std::vector<MeshTransition> transitions, double cost, std::size_t expansions) noexcept
        : m_status{status}, m_termination{termination}, m_polygons{std::move(polygons)},
          m_transitions{std::move(transitions)}, m_cost{cost}, m_expansions{expansions} {}
    [[nodiscard]] PathStatus GetStatus() const noexcept { return m_status; }
    [[nodiscard]] PathTermination GetTermination() const noexcept { return m_termination; }
    [[nodiscard]] std::span<const PolygonLocation> GetPolygons() const noexcept { return m_polygons; }
    [[nodiscard]] std::span<const MeshTransition> GetTransitions() const noexcept { return m_transitions; }
    [[nodiscard]] double GetCost() const noexcept { return m_cost; }
    [[nodiscard]] std::size_t GetExpansionCount() const noexcept { return m_expansions; }
private:
    PathStatus m_status{};
    PathTermination m_termination{};
    std::vector<PolygonLocation> m_polygons{};
    std::vector<MeshTransition> m_transitions{};
    double m_cost{};
    std::size_t m_expansions{};
};
// One operation exclusively leases this workspace. PMR scratch never escapes;
// the resource is owned until after all containers have been destroyed.
class QueryWorkspace final {
    class ConstructionKey final {
        friend class QueryWorkspace;
        ConstructionKey() = default;
    };
public:
    [[nodiscard]] static std::expected<std::unique_ptr<QueryWorkspace>, QueryError> Create(QueryLimits limits = {}) noexcept;
    [[nodiscard]] static std::expected<std::unique_ptr<QueryWorkspace>, QueryError> CreateWithResource(
        std::shared_ptr<std::pmr::memory_resource> resource, QueryLimits limits = {}) noexcept;
    QueryWorkspace(const QueryWorkspace&) = delete;
    QueryWorkspace& operator=(const QueryWorkspace&) = delete;
    QueryWorkspace(std::shared_ptr<std::pmr::memory_resource> resource, QueryLimits limits, ConstructionKey)
        : m_resource{std::move(resource)}, m_nodes{m_resource.get()}, m_heap{m_resource.get()},
          m_lookup{m_resource.get()}, m_reverse{m_resource.get()}, m_limits{limits} {}
    [[nodiscard]] std::expected<std::optional<NearestPolygon>, QueryError> FindNearestPolygon(
        const SnapshotLease& snapshot, const Vec3& position, const Vec3& halfExtents,
        const QueryFilter& filter = {}, std::stop_token cancellation = {}) noexcept;
    // Costs are the exact centroid/portal graph metric: departure-leg cost uses
    // the source area, arrival-leg cost the destination area, and an off-mesh
    // segment its own area. A* uses Euclidean distance times the minimum area
    // cost; Dijkstra uses zero. Equal keys use canonical TileKey/polygon order.
    [[nodiscard]] std::expected<CorridorResult, QueryError> FindPath(
        const PolygonLocation& start, const PolygonLocation& goal, const QueryFilter& filter = {},
        SearchAlgorithm algorithm = SearchAlgorithm::AStar, std::stop_token cancellation = {}) noexcept;
private:
    struct SearchNode final {
        std::size_t polygon{};
        double cost{}, heuristic{};
        std::optional<std::size_t> parent{};
        std::optional<std::size_t> parentArc{};
        std::optional<std::size_t> heapPosition{};
    };
    [[nodiscard]] bool Before(std::size_t left, std::size_t right) const noexcept;
    void SwapHeap(std::size_t left, std::size_t right) noexcept;
    void UpHeap(std::size_t position) noexcept;
    void PushOrUpdate(std::size_t node);
    [[nodiscard]] std::size_t PopHeap() noexcept;
    [[nodiscard]] std::expected<CorridorResult, QueryError> Search(const PolygonLocation& start,
        const PolygonLocation& goal, const QueryFilter& filter, SearchAlgorithm algorithm, std::stop_token cancellation);
    std::shared_ptr<std::pmr::memory_resource> m_resource;
    std::pmr::vector<SearchNode> m_nodes;
    std::pmr::vector<std::size_t> m_heap;
    std::pmr::unordered_map<std::size_t, std::size_t> m_lookup;
    std::pmr::vector<std::size_t> m_reverse;
    QueryLimits m_limits;
    std::atomic_flag m_inUse{};
};
}
