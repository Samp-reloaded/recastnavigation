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
export module recastnav.mesh;
import std;
export import recastnav.document;

export namespace recastnav {
enum class MeshError {
    InvalidLimits, MissingTileLease, DuplicateTile, IncompatibleProfile,
    CapacityExceeded, AllocationFailure, InvalidPolygon, SnapshotMismatch, InvalidNode, InvalidArc,
};
class MeshLimits final {
public:
    constexpr MeshLimits(std::size_t tiles = 65'536u, std::size_t polygons = 1'048'576u,
        std::size_t arcs = 8'388'608u, std::size_t connectionWork = 33'554'432u,
        std::size_t documentBytes = 512u * 1024u * 1024u) noexcept
        : m_tiles{tiles}, m_polygons{polygons}, m_arcs{arcs}, m_work{connectionWork}, m_documentBytes{documentBytes} {}
    [[nodiscard]] constexpr std::size_t GetMaximumTiles() const noexcept { return m_tiles; }
    [[nodiscard]] constexpr std::size_t GetMaximumPolygons() const noexcept { return m_polygons; }
    [[nodiscard]] constexpr std::size_t GetMaximumArcs() const noexcept { return m_arcs; }
    [[nodiscard]] constexpr std::size_t GetMaximumConnectionWork() const noexcept { return m_work; }
    // Aggregate copied document payload, including nested vector elements.
    [[nodiscard]] constexpr std::size_t GetMaximumDocumentBytes() const noexcept { return m_documentBytes; }
private:
    std::size_t m_tiles{}, m_polygons{}, m_arcs{}, m_work{}, m_documentBytes{};
};
class MeshSnapshot;
using SnapshotLease = std::shared_ptr<const MeshSnapshot>;
using TileLease = std::shared_ptr<const TileDocument>;
class PolygonLocation final {
public:
    PolygonLocation(const PolygonLocation&) noexcept = default;
    PolygonLocation& operator=(const PolygonLocation&) noexcept = default;
    // Moving a local reference preserves both handles' snapshot authority.
    PolygonLocation(PolygonLocation&& other) noexcept
        : m_snapshot{other.m_snapshot}, m_tile{other.m_tile}, m_polygon{other.m_polygon} {}
    PolygonLocation& operator=(PolygonLocation&& other) noexcept {
        m_snapshot = other.m_snapshot; m_tile = other.m_tile; m_polygon = other.m_polygon; return *this;
    }
    [[nodiscard]] const SnapshotLease& GetSnapshot() const noexcept { return m_snapshot; }
    [[nodiscard]] const TileKey& GetTileKey() const noexcept;
    [[nodiscard]] PolygonIndex GetPolygonIndex() const noexcept { return m_polygon; }
    [[nodiscard]] bool operator==(const PolygonLocation&) const noexcept = default;
private:
    friend class MeshSnapshot;
    PolygonLocation(SnapshotLease snapshot, std::size_t tile, PolygonIndex polygon) noexcept
        : m_snapshot{std::move(snapshot)}, m_tile{tile}, m_polygon{polygon} {}
    SnapshotLease m_snapshot;
    std::size_t m_tile{};
    PolygonIndex m_polygon{};
};
// A graph transition retains its actual portal or directed off-mesh endpoints.
class MeshTransition final {
public:
    MeshTransition(Vec3 departure, Vec3 arrival, std::optional<std::uint64_t> offMeshId = {}) noexcept
        : m_departure{departure}, m_arrival{arrival}, m_offMeshId{offMeshId} {}
    [[nodiscard]] const Vec3& GetDeparture() const noexcept { return m_departure; }
    [[nodiscard]] const Vec3& GetArrival() const noexcept { return m_arrival; }
    [[nodiscard]] const std::optional<std::uint64_t>& GetOffMeshUserId() const noexcept { return m_offMeshId; }
private:
    Vec3 m_departure{}, m_arrival{};
    std::optional<std::uint64_t> m_offMeshId{};
};
struct MeshNodeIndexTag;
struct MeshArcIndexTag;
using MeshNodeIndex = Index<MeshNodeIndexTag>;
using MeshArcIndex = Index<MeshArcIndexTag>;
class MeshGraphReadView;
class MeshGraphNode;
// Read proxies retain their exact snapshot lease. Indices are local diagnostics;
// obtaining a node, arc or polygon location always goes through a checked factory.
class MeshGraphArc final {
public:
    MeshGraphArc(const MeshGraphArc&) noexcept = default;
    MeshGraphArc& operator=(const MeshGraphArc&) noexcept = default;
    [[nodiscard]] MeshNodeIndex GetDestination() const noexcept;
    [[nodiscard]] const MeshTransition& GetTransition() const noexcept;
    [[nodiscard]] std::optional<std::reference_wrapper<const OffMeshConnection>> GetOffMeshConnection() const noexcept;
private:
    friend class MeshGraphNode;
    MeshGraphArc(SnapshotLease snapshot, std::size_t node, std::size_t arc) noexcept
        : m_snapshot{std::move(snapshot)}, m_node{node}, m_arc{arc} {}
    SnapshotLease m_snapshot;
    std::size_t m_node{}, m_arc{};
};
class MeshGraphNode final {
public:
    MeshGraphNode(const MeshGraphNode&) noexcept = default;
    MeshGraphNode& operator=(const MeshGraphNode&) noexcept = default;
    [[nodiscard]] const TileDocument& GetTile() const noexcept;
    [[nodiscard]] PolygonIndex GetPolygonIndex() const noexcept;
    [[nodiscard]] const Vec3& GetCentre() const noexcept;
    [[nodiscard]] const Aabb& GetBounds() const noexcept;
    [[nodiscard]] std::size_t GetArcCount() const noexcept;
    [[nodiscard]] std::expected<MeshGraphArc, MeshError> GetArc(MeshArcIndex arc) const noexcept;
private:
    friend class MeshGraphReadView;
    MeshGraphNode(SnapshotLease snapshot, std::size_t node) noexcept : m_snapshot{std::move(snapshot)}, m_node{node} {}
    SnapshotLease m_snapshot;
    std::size_t m_node{};
};
class MeshGraphReadView final {
public:
    MeshGraphReadView(const MeshGraphReadView&) noexcept = default;
    MeshGraphReadView& operator=(const MeshGraphReadView&) noexcept = default;
    [[nodiscard]] const SnapshotLease& GetSnapshot() const noexcept { return m_snapshot; }
    [[nodiscard]] std::size_t GetNodeCount() const noexcept;
    [[nodiscard]] std::expected<MeshGraphNode, MeshError> GetNode(MeshNodeIndex node) const noexcept;
    [[nodiscard]] std::expected<MeshNodeIndex, MeshError> GetNodeIndex(const PolygonLocation& location) const noexcept;
    [[nodiscard]] std::expected<PolygonLocation, MeshError> LocateNode(MeshNodeIndex node) const noexcept;
private:
    friend class MeshSnapshot;
    explicit MeshGraphReadView(SnapshotLease snapshot) noexcept : m_snapshot{std::move(snapshot)} {}
    SnapshotLease m_snapshot;
};
class MeshSnapshot final : public std::enable_shared_from_this<MeshSnapshot> {
    class ConstructionKey final {
        friend class MeshSnapshot;
        ConstructionKey() = default;
    };
public:
    // Documents are copied into const-owned storage to freeze mutable aliases;
    // tile order is canonicalized by full TileKey. Missing portal/off-mesh endpoints
    // remain unconnected so a snapshot can represent a streamed subset of a world.
    [[nodiscard]] static std::expected<SnapshotLease, MeshError> Create(
        std::vector<TileLease> tiles, MeshLimits limits = {}) noexcept;
    [[nodiscard]] std::span<const TileLease> GetTiles() const noexcept { return m_tiles; }
    [[nodiscard]] std::size_t GetPolygonCount() const noexcept { return m_nodes.size(); }
    [[nodiscard]] std::size_t GetArcCount() const noexcept { return m_arcCount; }
    [[nodiscard]] MeshGraphReadView ReadGraph() const noexcept { return MeshGraphReadView{shared_from_this()}; }
    [[nodiscard]] std::expected<std::optional<PolygonLocation>, MeshError> Locate(
        const TileKey& tile, PolygonIndex polygon) const noexcept;
    [[nodiscard]] std::expected<std::reference_wrapper<const Polygon>, MeshError> GetPolygon(
        const PolygonLocation& location) const noexcept;
    MeshSnapshot(const MeshSnapshot&) = delete;
    MeshSnapshot& operator=(const MeshSnapshot&) = delete;
    explicit MeshSnapshot(ConstructionKey) noexcept {}
private:
    friend class PolygonLocation;
    friend class MeshGraphReadView;
    friend class MeshGraphNode;
    friend class MeshGraphArc;
    struct Arc final {
        std::size_t destination{};
        MeshTransition transition{{}, {}};
        std::optional<std::pair<std::size_t, std::size_t>> offMesh{};
    };
    struct Node final {
        std::size_t tile{};
        PolygonIndex polygon{};
        Vec3 centre{};
        Aabb bounds{};
        std::vector<Arc> arcs{};
    };
    [[nodiscard]] std::expected<void, MeshError> Build(MeshLimits limits);
    [[nodiscard]] std::size_t NodeIndex(const PolygonLocation& location) const noexcept {
        return m_tileOffsets[location.m_tile] + location.m_polygon.GetValue();
    }
    [[nodiscard]] PolygonLocation Location(std::size_t node) const noexcept {
        return {shared_from_this(), m_nodes[node].tile, m_nodes[node].polygon};
    }
    std::vector<TileLease> m_tiles{};
    std::vector<std::size_t> m_tileOffsets{};
    std::vector<Node> m_nodes{};
    std::size_t m_arcCount{};
};
}
