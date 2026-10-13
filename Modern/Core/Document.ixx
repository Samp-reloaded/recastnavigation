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
export module recastnav.document;
import std;
export import recastnav.core;
export import recastnav.math;

export namespace recastnav {
class BuildProfile final {
public:
    constexpr BuildProfile(float cellSize = 0.3f, float cellHeight = 0.2f, float agentHeight = 2.0f,
        float agentRadius = 0.6f, float maximumClimb = 0.9f, float maximumSlopeDegrees = 45.0f) noexcept
        : m_cellSize{cellSize}, m_cellHeight{cellHeight}, m_height{agentHeight}, m_radius{agentRadius},
          m_climb{maximumClimb}, m_slope{maximumSlopeDegrees} {}
    [[nodiscard]] constexpr float GetCellSize() const noexcept { return m_cellSize; }
    [[nodiscard]] constexpr float GetCellHeight() const noexcept { return m_cellHeight; }
    [[nodiscard]] constexpr float GetAgentHeight() const noexcept { return m_height; }
    [[nodiscard]] constexpr float GetAgentRadius() const noexcept { return m_radius; }
    [[nodiscard]] constexpr float GetMaximumClimb() const noexcept { return m_climb; }
    [[nodiscard]] constexpr float GetMaximumSlopeDegrees() const noexcept { return m_slope; }
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] constexpr bool operator==(const BuildProfile&) const noexcept = default;
private:
    float m_cellSize{}, m_cellHeight{}, m_height{}, m_radius{}, m_climb{}, m_slope{};
};
class TileKey final {
public:
    constexpr TileKey(std::int64_t x = 0, std::int64_t z = 0, std::uint32_t layer = 0) noexcept : m_x{x}, m_z{z}, m_layer{layer} {}
    [[nodiscard]] constexpr std::int64_t GetX() const noexcept { return m_x; }
    [[nodiscard]] constexpr std::int64_t GetZ() const noexcept { return m_z; }
    [[nodiscard]] constexpr std::uint32_t GetLayer() const noexcept { return m_layer; }
    [[nodiscard]] constexpr auto operator<=>(const TileKey&) const noexcept = default;
private:
    std::int64_t m_x{}, m_z{};
    std::uint32_t m_layer{};
};
using CookFingerprint = std::array<std::byte, 32>;
class TileMetadata final {
public:
    constexpr TileMetadata(TileKey key, Aabb bounds, BuildProfile profile = {}, CookFingerprint fingerprint = {}) noexcept
        : m_key{key}, m_bounds{bounds}, m_profile{profile}, m_fingerprint{fingerprint} {}
    [[nodiscard]] constexpr const TileKey& GetKey() const noexcept { return m_key; }
    [[nodiscard]] constexpr const Aabb& GetBounds() const noexcept { return m_bounds; }
    [[nodiscard]] constexpr const BuildProfile& GetProfile() const noexcept { return m_profile; }
    [[nodiscard]] constexpr const CookFingerprint& GetFingerprint() const noexcept { return m_fingerprint; }
    [[nodiscard]] constexpr bool operator==(const TileMetadata&) const noexcept = default;
private:
    TileKey m_key;
    Aabb m_bounds;
    BuildProfile m_profile;
    CookFingerprint m_fingerprint;
};
enum class PortalSide : std::uint32_t { PositiveX, PositiveZ, NegativeX, NegativeZ };
// No neighbour, a local polygon, or an unresolved portal at a tile boundary. No packed sentinel.
using EdgeConnection = std::variant<std::monostate, PolygonIndex, PortalSide>;
class Polygon final {
public:
    Polygon(std::vector<VertexIndex> vertices, std::vector<EdgeConnection> edges, std::uint32_t area = 63u,
        std::vector<std::uint32_t> categories = {}, bool enabled = true) noexcept
        : m_vertices{std::move(vertices)}, m_edges{std::move(edges)}, m_area{area},
          m_categories{std::move(categories)}, m_enabled{enabled} {}
    [[nodiscard]] std::span<const VertexIndex> GetVertices() const noexcept { return m_vertices; }
    [[nodiscard]] std::span<const EdgeConnection> GetEdges() const noexcept { return m_edges; }
    [[nodiscard]] std::uint32_t GetArea() const noexcept { return m_area; }
    [[nodiscard]] std::span<const std::uint32_t> GetCategories() const noexcept { return m_categories; }
    [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
    [[nodiscard]] bool operator==(const Polygon&) const noexcept = default;
private:
    std::vector<VertexIndex> m_vertices{};
    std::vector<EdgeConnection> m_edges{};
    std::uint32_t m_area{};
    std::vector<std::uint32_t> m_categories{};
    bool m_enabled{};
};
class DetailTriangle final {
public:
    constexpr DetailTriangle(std::array<DetailVertexIndex, 3> vertices,
        std::array<bool, 3> boundaryEdges = {}) noexcept : m_vertices{vertices}, m_boundary{boundaryEdges} {}
    [[nodiscard]] constexpr const std::array<DetailVertexIndex, 3>& GetVertices() const noexcept { return m_vertices; }
    [[nodiscard]] constexpr const std::array<bool, 3>& GetBoundaryEdges() const noexcept { return m_boundary; }
    [[nodiscard]] constexpr bool operator==(const DetailTriangle&) const noexcept = default;
private:
    std::array<DetailVertexIndex, 3> m_vertices;
    std::array<bool, 3> m_boundary{};
};
class DetailMesh final {
public:
    // Triangle indices address polygon vertices first, followed by this mesh's additional vertices.
    DetailMesh(std::vector<Vec3> additionalVertices, std::vector<DetailTriangle> triangles) noexcept
        : m_vertices{std::move(additionalVertices)}, m_triangles{std::move(triangles)} {}
    [[nodiscard]] std::span<const Vec3> GetAdditionalVertices() const noexcept { return m_vertices; }
    [[nodiscard]] std::span<const DetailTriangle> GetTriangles() const noexcept { return m_triangles; }
    [[nodiscard]] bool operator==(const DetailMesh&) const noexcept = default;
private:
    std::vector<Vec3> m_vertices{};
    std::vector<DetailTriangle> m_triangles{};
};
class OffMeshConnection final {
public:
    OffMeshConnection(Vec3 start, Vec3 end, float radius, std::uint64_t userId = 0,
        std::uint32_t area = 63u, std::vector<std::uint32_t> categories = {}, bool bidirectional = false) noexcept
        : m_start{start}, m_end{end}, m_radius{radius}, m_userId{userId}, m_area{area},
          m_categories{std::move(categories)}, m_bidirectional{bidirectional} {}
    [[nodiscard]] const Vec3& GetStart() const noexcept { return m_start; }
    [[nodiscard]] const Vec3& GetEnd() const noexcept { return m_end; }
    [[nodiscard]] float GetRadius() const noexcept { return m_radius; }
    [[nodiscard]] std::uint64_t GetUserId() const noexcept { return m_userId; }
    [[nodiscard]] std::uint32_t GetArea() const noexcept { return m_area; }
    [[nodiscard]] std::span<const std::uint32_t> GetCategories() const noexcept { return m_categories; }
    [[nodiscard]] bool IsBidirectional() const noexcept { return m_bidirectional; }
    [[nodiscard]] bool operator==(const OffMeshConnection&) const noexcept = default;
private:
    Vec3 m_start{}, m_end{};
    float m_radius{};
    std::uint64_t m_userId{};
    std::uint32_t m_area{};
    std::vector<std::uint32_t> m_categories{};
    bool m_bidirectional{};
};
class TileDocument final {
public:
    [[nodiscard]] static std::expected<TileDocument, Failure> Create(TileMetadata metadata,
        std::vector<Vec3> vertices, std::vector<Polygon> polygons,
        std::vector<DetailMesh> details = {}, std::vector<OffMeshConnection> offMesh = {}, DocumentLimits limits = {}) noexcept;
    [[nodiscard]] const TileMetadata& GetMetadata() const noexcept { return m_metadata; }
    [[nodiscard]] std::span<const Vec3> GetVertices() const noexcept { return m_vertices; }
    [[nodiscard]] std::span<const Polygon> GetPolygons() const noexcept { return m_polygons; }
    [[nodiscard]] std::span<const DetailMesh> GetDetails() const noexcept { return m_details; }
    [[nodiscard]] std::span<const OffMeshConnection> GetOffMeshConnections() const noexcept { return m_offMesh; }
    [[nodiscard]] bool operator==(const TileDocument&) const noexcept = default;
private:
    TileDocument(TileMetadata metadata, std::vector<Vec3> vertices, std::vector<Polygon> polygons,
        std::vector<DetailMesh> details, std::vector<OffMeshConnection> offMesh) noexcept
        : m_metadata{metadata}, m_vertices{std::move(vertices)}, m_polygons{std::move(polygons)},
          m_details{std::move(details)}, m_offMesh{std::move(offMesh)} {}
    TileMetadata m_metadata;
    std::vector<Vec3> m_vertices{};
    std::vector<Polygon> m_polygons{};
    std::vector<DetailMesh> m_details{};
    std::vector<OffMeshConnection> m_offMesh{};
};
class LayerRegion final {
public:
    constexpr LayerRegion(std::size_t minimumX, std::size_t minimumZ, std::size_t maximumX,
        std::size_t maximumZ) noexcept : m_minX{minimumX}, m_minZ{minimumZ}, m_maxX{maximumX}, m_maxZ{maximumZ} {}
    [[nodiscard]] constexpr std::size_t GetMinimumX() const noexcept { return m_minX; }
    [[nodiscard]] constexpr std::size_t GetMinimumZ() const noexcept { return m_minZ; }
    [[nodiscard]] constexpr std::size_t GetMaximumX() const noexcept { return m_maxX; }
    [[nodiscard]] constexpr std::size_t GetMaximumZ() const noexcept { return m_maxZ; }
    [[nodiscard]] constexpr bool operator==(const LayerRegion&) const noexcept = default;
private:
    std::size_t m_minX{}, m_minZ{}, m_maxX{}, m_maxZ{};
};
class LayerConnection final {
public:
    constexpr LayerConnection(bool neighbour = false, bool portal = false) noexcept : m_neighbour{neighbour}, m_portal{portal} {}
    [[nodiscard]] constexpr bool HasNeighbour() const noexcept { return m_neighbour; }
    [[nodiscard]] constexpr bool IsPortal() const noexcept { return m_portal; }
    [[nodiscard]] constexpr bool operator==(const LayerConnection&) const noexcept = default;
private:
    bool m_neighbour{}, m_portal{};
};
class LayerCell final {
public:
    // Connections use upstream order: -X, +Z, +X, -Z. Height is relative to the layer's minimum height.
    constexpr LayerCell(std::optional<std::uint32_t> height = {}, std::uint32_t area = 0,
        std::array<LayerConnection, 4> connections = {}) noexcept : m_height{height}, m_area{area}, m_connections{connections} {}
    [[nodiscard]] constexpr const std::optional<std::uint32_t>& GetHeight() const noexcept { return m_height; }
    [[nodiscard]] constexpr std::uint32_t GetArea() const noexcept { return m_area; }
    [[nodiscard]] constexpr const std::array<LayerConnection, 4>& GetConnections() const noexcept { return m_connections; }
    [[nodiscard]] constexpr bool operator==(const LayerCell&) const noexcept = default;
private:
    std::optional<std::uint32_t> m_height{};
    std::uint32_t m_area{};
    std::array<LayerConnection, 4> m_connections{};
};
class LayerDocument final {
public:
    [[nodiscard]] static std::expected<LayerDocument, Failure> Create(TileMetadata metadata,
        std::size_t width, std::size_t height, LayerRegion usableRegion,
        std::uint32_t minimumHeight, std::uint32_t maximumHeight,
        std::vector<LayerCell> cells, DocumentLimits limits = {}) noexcept;
    [[nodiscard]] const TileMetadata& GetMetadata() const noexcept { return m_metadata; }
    [[nodiscard]] std::size_t GetWidth() const noexcept { return m_width; }
    [[nodiscard]] std::size_t GetHeight() const noexcept { return m_height; }
    [[nodiscard]] const LayerRegion& GetUsableRegion() const noexcept { return m_region; }
    [[nodiscard]] std::uint32_t GetMinimumHeight() const noexcept { return m_minimumHeight; }
    [[nodiscard]] std::uint32_t GetMaximumHeight() const noexcept { return m_maximumHeight; }
    [[nodiscard]] std::span<const LayerCell> GetCells() const noexcept { return m_cells; }
    [[nodiscard]] bool operator==(const LayerDocument&) const noexcept = default;
private:
    LayerDocument(TileMetadata metadata, std::size_t width, std::size_t height, LayerRegion region,
        std::uint32_t minimumHeight, std::uint32_t maximumHeight, std::vector<LayerCell> cells) noexcept
        : m_metadata{metadata}, m_width{width}, m_height{height}, m_region{region},
          m_minimumHeight{minimumHeight}, m_maximumHeight{maximumHeight}, m_cells{std::move(cells)} {}
    TileMetadata m_metadata;
    std::size_t m_width{}, m_height{};
    LayerRegion m_region;
    std::uint32_t m_minimumHeight{}, m_maximumHeight{};
    std::vector<LayerCell> m_cells{};
};
class Triangle final {
public:
    constexpr explicit Triangle(std::array<VertexIndex, 3> vertices, std::uint32_t area = 63u) noexcept
        : m_vertices{vertices}, m_area{area} {}
    [[nodiscard]] constexpr const std::array<VertexIndex, 3>& GetVertices() const noexcept { return m_vertices; }
    [[nodiscard]] constexpr std::uint32_t GetArea() const noexcept { return m_area; }
private:
    std::array<VertexIndex, 3> m_vertices;
    std::uint32_t m_area{};
};
// A synchronous geometry borrow. Validation never allocates or retains caller memory.
[[nodiscard]] std::expected<void, Failure> ValidateBuildInput(std::span<const Vec3> vertices,
    std::span<const Triangle> triangles, const BuildProfile& profile, DocumentLimits limits = {}) noexcept;
}
