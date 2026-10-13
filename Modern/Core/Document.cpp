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
module recastnav.document;
import std;

namespace recastnav {
namespace {
[[nodiscard]] bool CategoriesValid(std::span<const std::uint32_t> categories, const DocumentLimits& limits) noexcept {
    if (categories.size() > limits.GetMaximumCategories()) { return false; }
    for (std::size_t i{}; i < categories.size(); ++i) {
        if (std::find(categories.begin(), categories.begin() + static_cast<std::ptrdiff_t>(i), categories[i])
            != categories.begin() + static_cast<std::ptrdiff_t>(i)) { return false; }
    }
    return true;
}
[[nodiscard]] std::expected<void, Failure> ValidateMetadata(const TileMetadata& metadata, const DocumentLimits& limits) noexcept {
    if (!limits.IsValid()) { return std::unexpected{Failure{Error::InvalidLimits}}; }
    if (!metadata.GetProfile().IsValid()) { return std::unexpected{Failure{Error::InvalidProfile}}; }
    const auto& bounds{metadata.GetBounds()};
    if (!bounds.IsValid() || bounds.GetMinimum().GetX() == bounds.GetMaximum().GetX()
        || bounds.GetMinimum().GetZ() == bounds.GetMaximum().GetZ()) {
        return std::unexpected{Failure{Error::InvalidBounds}};
    }
    return {};
}
[[nodiscard]] bool TriangleHasArea(const Vec3& a, const Vec3& b, const Vec3& c) noexcept {
    const double ax{static_cast<double>(b.GetX()) - a.GetX()}, ay{static_cast<double>(b.GetY()) - a.GetY()}, az{static_cast<double>(b.GetZ()) - a.GetZ()};
    const double bx{static_cast<double>(c.GetX()) - a.GetX()}, by{static_cast<double>(c.GetY()) - a.GetY()}, bz{static_cast<double>(c.GetZ()) - a.GetZ()};
    return ay * bz - az * by != 0.0 || az * bx - ax * bz != 0.0 || ax * by - ay * bx != 0.0;
}
class DetailEdge final {
public:
    DetailEdge(std::size_t from, std::size_t to, bool boundary) noexcept
        : m_lower{std::min(from, to)}, m_upper{std::max(from, to)}, m_from{from}, m_to{to}, m_boundary{boundary} {}
    [[nodiscard]] std::size_t GetLower() const noexcept { return m_lower; }
    [[nodiscard]] std::size_t GetUpper() const noexcept { return m_upper; }
    [[nodiscard]] std::size_t GetFrom() const noexcept { return m_from; }
    [[nodiscard]] std::size_t GetTo() const noexcept { return m_to; }
    [[nodiscard]] bool IsBoundary() const noexcept { return m_boundary; }
    void SetBoundaryInterval(std::size_t edge, float low, float high) noexcept {
        m_polygonEdge = edge; m_intervalLow = low; m_intervalHigh = high;
    }
    [[nodiscard]] std::size_t GetPolygonEdge() const noexcept { return m_polygonEdge; }
    [[nodiscard]] float GetIntervalLow() const noexcept { return m_intervalLow; }
    [[nodiscard]] float GetIntervalHigh() const noexcept { return m_intervalHigh; }
private:
    std::size_t m_lower{}, m_upper{}, m_from{}, m_to{}, m_polygonEdge{};
    float m_intervalLow{}, m_intervalHigh{};
    bool m_boundary{};
};
[[nodiscard]] std::expected<void, Failure> ValidateDetailTopology(std::span<const Vec3> vertices,
    std::span<const VertexIndex> polygon, const DetailMesh& detail, const DocumentLimits& limits,
    std::size_t polygonOrdinal) {
    const auto refuse = [polygonOrdinal]() -> std::expected<void, Failure> {
        return std::unexpected{Failure{Error::InvalidDetailMesh, 0, polygonOrdinal}};
    };
    const auto additional{detail.GetAdditionalVertices()};
    const auto vertexCount{polygon.size() + additional.size()};
    const auto vertexBytes{CheckedProduct(vertexCount, sizeof(std::size_t), limits.GetMaximumBytes())};
    if (!vertexBytes) { return std::unexpected{Failure{Error::BudgetExceeded, 0, polygonOrdinal}}; }
    const auto edgeCount{CheckedProduct(detail.GetTriangles().size(), 3u,
        (limits.GetMaximumBytes() - *vertexBytes) / sizeof(DetailEdge))};
    if (!edgeCount) { return std::unexpected{Failure{Error::BudgetExceeded, 0, polygonOrdinal}}; }
    const auto point = [&](std::size_t index) -> const Vec3& {
        return index < polygon.size() ? vertices[polygon[index].GetValue()] : additional[index - polygon.size()];
    };
    // A detail surface is a single-valued height function. Geometrically duplicate
    // projected vertices cannot conceal disconnected edges under different indices.
    std::vector<std::size_t> order(vertexCount);
    std::iota(order.begin(), order.end(), std::size_t{});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return std::pair{point(a).GetX(), point(a).GetZ()} < std::pair{point(b).GetX(), point(b).GetZ()};
    });
    for (std::size_t i{1u}; i < order.size(); ++i) {
        if (point(order[i - 1u]).GetX() == point(order[i]).GetX()
            && point(order[i - 1u]).GetZ() == point(order[i]).GetZ()) { return refuse(); }
    }
    // Collinear polygon vertices do not require extra detail triangles. Collapse
    // only perimeter chains for coverage; polygon and triangle records stay intact.
    std::array<std::size_t, 6u> corners{};
    std::size_t cornerCount{};
    for (std::size_t i{}; i < polygon.size(); ++i) {
        if (UpwardTurn(point((i + polygon.size() - 1u) % polygon.size()), point(i), point((i + 1u) % polygon.size())) > 0.0) {
            corners[cornerCount++] = i;
        }
    }
    if (cornerCount < 3u) { return refuse(); }
    std::vector<DetailEdge> edges{}; edges.reserve(*edgeCount);
    for (const auto& triangle : detail.GetTriangles()) {
        const auto& indices{triangle.GetVertices()}; const auto& boundary{triangle.GetBoundaryEdges()};
        for (std::size_t edge{}; edge < 3u; ++edge) {
            edges.emplace_back(indices[edge].GetValue(), indices[(edge + 1u) % 3u].GetValue(), boundary[edge]);
        }
    }
    std::sort(edges.begin(), edges.end(), [](const DetailEdge& a, const DetailEdge& b) {
        return std::pair{a.GetLower(), a.GetUpper()} < std::pair{b.GetLower(), b.GetUpper()};
    });
    std::size_t boundaryCount{};
    for (std::size_t first{}; first < edges.size();) {
        auto last{first + 1u};
        while (last < edges.size() && edges[last].GetLower() == edges[first].GetLower()
            && edges[last].GetUpper() == edges[first].GetUpper()) { ++last; }
        if (last - first == 2u) {
            if (edges[first].IsBoundary() || edges[first + 1u].IsBoundary()
                || edges[first].GetFrom() != edges[first + 1u].GetTo()
                || edges[first].GetTo() != edges[first + 1u].GetFrom()) { return refuse(); }
        } else if (last - first == 1u) {
            if (!edges[first].IsBoundary()) { return refuse(); }
            const auto& from{point(edges[first].GetFrom())}; const auto& to{point(edges[first].GetTo())};
            bool assigned{};
            for (std::size_t edge{}; edge < cornerCount; ++edge) {
                const auto& a{point(corners[edge])}; const auto& b{point(corners[(edge + 1u) % cornerCount])};
                if (UpwardTurn(a, b, from) != 0.0 || UpwardTurn(a, b, to) != 0.0) { continue; }
                const bool useX{std::abs(static_cast<double>(b.GetX()) - a.GetX())
                    >= std::abs(static_cast<double>(b.GetZ()) - a.GetZ())};
                const auto aValue{useX ? a.GetX() : a.GetZ()}, bValue{useX ? b.GetX() : b.GetZ()};
                const auto fromValue{useX ? from.GetX() : from.GetZ()}, toValue{useX ? to.GetX() : to.GetZ()};
                const auto low{std::min(fromValue, toValue)}, high{std::max(fromValue, toValue)};
                if (low >= high || low < std::min(aValue, bValue) || high > std::max(aValue, bValue)
                    || (fromValue < toValue) != (aValue < bValue)) { continue; }
                auto retained{edges[first]}; retained.SetBoundaryInterval(edge, low, high);
                edges[boundaryCount++] = retained; assigned = true; break;
            }
            if (!assigned) { return refuse(); }
        } else { return refuse(); }
        first = last;
    }
    edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(boundaryCount), edges.end());
    std::sort(edges.begin(), edges.end(), [](const DetailEdge& a, const DetailEdge& b) {
        return std::tuple{a.GetPolygonEdge(), a.GetIntervalLow(), a.GetIntervalHigh()}
            < std::tuple{b.GetPolygonEdge(), b.GetIntervalLow(), b.GetIntervalHigh()};
    });
    std::size_t cursor{};
    for (std::size_t edge{}; edge < cornerCount; ++edge) {
        const auto& a{point(corners[edge])}; const auto& b{point(corners[(edge + 1u) % cornerCount])};
        const bool useX{std::abs(static_cast<double>(b.GetX()) - a.GetX()) >= std::abs(static_cast<double>(b.GetZ()) - a.GetZ())};
        const auto aValue{useX ? a.GetX() : a.GetZ()}, bValue{useX ? b.GetX() : b.GetZ()};
        auto reached{std::min(aValue, bValue)};
        while (cursor < edges.size() && edges[cursor].GetPolygonEdge() == edge) {
            if (edges[cursor].GetIntervalLow() != reached) { return refuse(); }
            reached = edges[cursor++].GetIntervalHigh();
        }
        if (reached != std::max(aValue, bValue)) { return refuse(); }
    }
    if (cursor != edges.size()) { return refuse(); }
    // Positive projected triangles, reversed interior pairs, and exactly one
    // oriented polygon perimeter have winding degree one: no gaps or overlaps.
    return {};
}
}
bool BuildProfile::IsValid() const noexcept {
    return std::isfinite(m_cellSize) && m_cellSize > 0 && std::isfinite(m_cellHeight) && m_cellHeight > 0
        && std::isfinite(m_height) && m_height > 0 && std::isfinite(m_radius) && m_radius >= 0
        && std::isfinite(m_climb) && m_climb >= 0 && std::isfinite(m_slope) && m_slope >= 0 && m_slope < 90;
}
std::expected<TileDocument, Failure> TileDocument::Create(TileMetadata metadata,
    std::vector<Vec3> vertices, std::vector<Polygon> polygons, std::vector<DetailMesh> details,
    std::vector<OffMeshConnection> offMesh, DocumentLimits limits) noexcept try {
    if (const auto valid{ValidateMetadata(metadata, limits)}; !valid) { return std::unexpected{valid.error()}; }
    if (vertices.size() > limits.GetMaximumVertices() || polygons.size() > limits.GetMaximumPolygons()
        || offMesh.size() > limits.GetMaximumOffMeshConnections()) { return std::unexpected{Failure{Error::BudgetExceeded}}; }
    if ((!polygons.empty() && vertices.size() < 3u) || (!details.empty() && details.size() != polygons.size())) {
        return std::unexpected{Failure{Error::InvalidDetailMesh}};
    }
    for (std::size_t i{}; i < vertices.size(); ++i) {
        if (!vertices[i].IsFinite()) { return std::unexpected{Failure{Error::NonFiniteGeometry, 0, i}}; }
        if (!metadata.GetBounds().Contains(vertices[i])) { return std::unexpected{Failure{Error::InvalidBounds, 0, i}}; }
    }
    for (std::size_t i{}; i < polygons.size(); ++i) {
        const auto& polygon{polygons[i]}; const auto indices{polygon.GetVertices()}; const auto edges{polygon.GetEdges()};
        if (indices.size() < 3u || indices.size() > limits.GetMaximumPolygonVertices() || edges.size() != indices.size()) {
            return std::unexpected{Failure{Error::InvalidTopology, 0, i}};
        }
        if (polygon.GetArea() >= 64u || !CategoriesValid(polygon.GetCategories(), limits)) {
            return std::unexpected{Failure{Error::InvalidArea, 0, i}};
        }
        for (std::size_t j{}; j < indices.size(); ++j) {
            if (indices[j].GetValue() >= vertices.size()) { return std::unexpected{Failure{Error::InvalidIndex, 0, i}}; }
            for (std::size_t k{}; k < j; ++k) {
                const auto& current{vertices[indices[j].GetValue()]}; const auto& previous{vertices[indices[k].GetValue()]};
                if (indices[j] == indices[k] || (current.GetX() == previous.GetX() && current.GetZ() == previous.GetZ())) {
                    return std::unexpected{Failure{Error::DegenerateGeometry, 0, i}};
                }
            }
        }
        bool hasTurn{};
        for (std::size_t j{}; j < indices.size(); ++j) {
            const auto& a{vertices[indices[j].GetValue()]}; const auto& b{vertices[indices[(j + 1u) % indices.size()].GetValue()]};
            const auto& c{vertices[indices[(j + 2u) % indices.size()].GetValue()]}; const auto turn{UpwardTurn(a, b, c)};
            if (turn < 0.0) { return std::unexpected{Failure{Error::NonConvexPolygon, 0, i}}; }
            hasTurn = hasTurn || turn > 0.0;
            // Every vertex lies on the interior side of every directed edge; this also rejects stars.
            for (const auto candidate : indices) {
                if (UpwardTurn(a, b, vertices[candidate.GetValue()]) < 0.0) {
                    return std::unexpected{Failure{Error::NonConvexPolygon, 0, i}};
                }
            }
            if (const auto* neighbour{std::get_if<PolygonIndex>(&edges[j])}) {
                if (neighbour->GetValue() >= polygons.size() || neighbour->GetValue() == i) {
                    return std::unexpected{Failure{Error::InvalidTopology, 0, i}};
                }
                const auto& other{polygons[neighbour->GetValue()]}; const auto otherVertices{other.GetVertices()}; const auto otherEdges{other.GetEdges()};
                if (otherVertices.size() < 3u || otherVertices.size() > limits.GetMaximumPolygonVertices()
                    || otherVertices.size() != otherEdges.size()) {
                    return std::unexpected{Failure{Error::InvalidTopology, 0, i}};
                }
                bool reciprocal{};
                for (std::size_t k{}; k < otherVertices.size(); ++k) {
                    const auto next{(k + 1u) % otherVertices.size()};
                    if (otherVertices[k].GetValue() >= vertices.size() || otherVertices[next].GetValue() >= vertices.size()) {
                        return std::unexpected{Failure{Error::InvalidIndex, 0, i}};
                    }
                    if (vertices[otherVertices[k].GetValue()] == b && vertices[otherVertices[next].GetValue()] == a) {
                        const auto* back{std::get_if<PolygonIndex>(&otherEdges[k])}; reciprocal = back && back->GetValue() == i;
                        break;
                    }
                }
                if (!reciprocal) { return std::unexpected{Failure{Error::InvalidTopology, 0, i}}; }
            } else if (const auto* side{std::get_if<PortalSide>(&edges[j])}) {
                const auto& low{metadata.GetBounds().GetMinimum()}; const auto& high{metadata.GetBounds().GetMaximum()};
                bool onBoundary{};
                switch (*side) {
                case PortalSide::PositiveX: onBoundary = a.GetX() == high.GetX() && b.GetX() == high.GetX(); break;
                case PortalSide::PositiveZ: onBoundary = a.GetZ() == high.GetZ() && b.GetZ() == high.GetZ(); break;
                case PortalSide::NegativeX: onBoundary = a.GetX() == low.GetX() && b.GetX() == low.GetX(); break;
                case PortalSide::NegativeZ: onBoundary = a.GetZ() == low.GetZ() && b.GetZ() == low.GetZ(); break;
                default: return std::unexpected{Failure{Error::InvalidTopology, 0, i}};
                }
                if (!onBoundary) { return std::unexpected{Failure{Error::InvalidTopology, 0, i}}; }
            }
        }
        if (!hasTurn) { return std::unexpected{Failure{Error::DegenerateGeometry, 0, i}}; }
    }
    std::size_t detailVertexCount{}, detailTriangleCount{};
    for (std::size_t i{}; i < details.size(); ++i) {
        const auto& detail{details[i]}; const auto additional{detail.GetAdditionalVertices()}; const auto triangles{detail.GetTriangles()};
        if (additional.size() > limits.GetMaximumDetailVertices() - detailVertexCount
            || triangles.size() > limits.GetMaximumDetailTriangles() - detailTriangleCount) {
            return std::unexpected{Failure{Error::BudgetExceeded, 0, i}};
        }
        detailVertexCount += additional.size(); detailTriangleCount += triangles.size();
        const auto polygon{polygons[i].GetVertices()};
        if (additional.size() > std::numeric_limits<std::size_t>::max() - polygon.size() || triangles.empty()) {
            return std::unexpected{Failure{Error::InvalidDetailMesh, 0, i}};
        }
        for (const auto& vertex : additional) {
            if (!metadata.GetBounds().Contains(vertex)) { return std::unexpected{Failure{Error::InvalidDetailMesh, 0, i}}; }
            for (std::size_t edge{}; edge < polygon.size(); ++edge) {
                if (UpwardTurn(vertices[polygon[edge].GetValue()],
                    vertices[polygon[(edge + 1u) % polygon.size()].GetValue()], vertex) < 0.0) {
                    return std::unexpected{Failure{Error::InvalidDetailMesh, 0, i}};
                }
            }
        }
        const auto point = [&](DetailVertexIndex index) -> const Vec3& {
            return index.GetValue() < polygon.size() ? vertices[polygon[index.GetValue()].GetValue()] : additional[index.GetValue() - polygon.size()];
        };
        for (const auto& triangle : triangles) {
            const auto& indices{triangle.GetVertices()};
            for (const auto index : indices) { if (index.GetValue() >= polygon.size() + additional.size()) { return std::unexpected{Failure{Error::InvalidDetailMesh, 0, i}}; } }
            if (!TriangleHasArea(point(indices[0]), point(indices[1]), point(indices[2]))
                || UpwardTurn(point(indices[0]), point(indices[1]), point(indices[2])) <= 0.0) {
                return std::unexpected{Failure{Error::InvalidDetailMesh, 0, i}};
            }
        }
        if (const auto topology{ValidateDetailTopology(vertices, polygon, detail, limits, i)}; !topology) {
            return std::unexpected{topology.error()};
        }
    }
    for (std::size_t i{}; i < offMesh.size(); ++i) {
        const auto& link{offMesh[i]};
        if (!link.GetStart().IsFinite() || !link.GetEnd().IsFinite() || !std::isfinite(link.GetRadius())
            || link.GetRadius() < 0 || link.GetStart() == link.GetEnd() || link.GetArea() >= 64u
            || !CategoriesValid(link.GetCategories(), limits)) {
            return std::unexpected{Failure{Error::InvalidOffMeshConnection, 0, i}};
        }
    }
    return TileDocument{metadata, std::move(vertices), std::move(polygons), std::move(details), std::move(offMesh)};
} catch (const std::bad_alloc&) { return std::unexpected{Failure{Error::AllocationFailure}}; }
catch (const std::length_error&) { return std::unexpected{Failure{Error::SizeOverflow}}; }
std::expected<LayerDocument, Failure> LayerDocument::Create(TileMetadata metadata, std::size_t width,
    std::size_t height, LayerRegion region, std::uint32_t minimumHeight, std::uint32_t maximumHeight,
    std::vector<LayerCell> cells, DocumentLimits limits) noexcept {
    if (const auto valid{ValidateMetadata(metadata, limits)}; !valid) { return std::unexpected{valid.error()}; }
    const auto count{CheckedProduct(width, height, limits.GetMaximumLayerCells())};
    if (!count) { return std::unexpected{Failure{Error::BudgetExceeded}}; }
    if (width == 0u || height == 0u || cells.size() != *count || minimumHeight > maximumHeight
        || region.GetMinimumX() > region.GetMaximumX() || region.GetMinimumZ() > region.GetMaximumZ()
        || region.GetMaximumX() >= width || region.GetMaximumZ() >= height) {
        return std::unexpected{Failure{Error::InvalidLayer}};
    }
    for (std::size_t i{}; i < cells.size(); ++i) {
        const auto& cell{cells[i]}; const auto x{i % width}; const auto z{i / width};
        if (cell.GetArea() >= 64u || (cell.GetHeight() && *cell.GetHeight() > maximumHeight - minimumHeight)
            || (!cell.GetHeight() && cell.GetArea() != 0u)) { return std::unexpected{Failure{Error::InvalidLayer, 0, i}}; }
        for (std::size_t direction{}; direction < 4u; ++direction) {
            const auto& connection{cell.GetConnections()[direction]};
            if (connection.HasNeighbour() && connection.IsPortal()) {
                return std::unexpected{Failure{Error::InvalidLayer, 0, i}};
            }
            if (!connection.HasNeighbour() && !connection.IsPortal()) { continue; }
            if (!cell.GetHeight() || cell.GetArea() == 0u || x < region.GetMinimumX() || x > region.GetMaximumX()
                || z < region.GetMinimumZ() || z > region.GetMaximumZ()) {
                return std::unexpected{Failure{Error::InvalidLayer, 0, i}};
            }
            const bool inGrid{direction == 0u ? x != 0u : direction == 1u ? z + 1u < height : direction == 2u ? x + 1u < width : z != 0u};
            if (connection.HasNeighbour()) {
                if (!inGrid) { return std::unexpected{Failure{Error::InvalidLayer, 0, i}}; }
                const auto neighbour{direction == 0u ? i - 1u : direction == 1u ? i + width : direction == 2u ? i + 1u : i - width};
                const auto& other{cells[neighbour]};
                if (!other.GetHeight() || other.GetArea() == 0u || !other.GetConnections()[(direction + 2u) % 4u].HasNeighbour()) {
                    return std::unexpected{Failure{Error::InvalidLayer, 0, i}};
                }
            }
        }
    }
    return LayerDocument{metadata, width, height, region, minimumHeight, maximumHeight, std::move(cells)};
}
std::expected<void, Failure> ValidateBuildInput(std::span<const Vec3> vertices, std::span<const Triangle> triangles,
    const BuildProfile& profile, DocumentLimits limits) noexcept {
    if (!limits.IsValid()) { return std::unexpected{Failure{Error::InvalidLimits}}; }
    if (!profile.IsValid()) { return std::unexpected{Failure{Error::InvalidProfile}}; }
    if (vertices.size() > limits.GetMaximumVertices() || triangles.size() > limits.GetMaximumDetailTriangles()) {
        return std::unexpected{Failure{Error::BudgetExceeded}};
    }
    for (std::size_t i{}; i < vertices.size(); ++i) {
        if (!vertices[i].IsFinite()) { return std::unexpected{Failure{Error::NonFiniteGeometry, 0, i}}; }
    }
    for (std::size_t i{}; i < triangles.size(); ++i) {
        const auto& triangle{triangles[i]}; const auto& indices{triangle.GetVertices()};
        if (triangle.GetArea() >= 64u) { return std::unexpected{Failure{Error::InvalidArea, 0, i}}; }
        for (const auto index : indices) { if (index.GetValue() >= vertices.size()) { return std::unexpected{Failure{Error::InvalidIndex, 0, i}}; } }
        if (!TriangleHasArea(vertices[indices[0].GetValue()], vertices[indices[1].GetValue()], vertices[indices[2].GetValue()])) {
            return std::unexpected{Failure{Error::DegenerateGeometry, 0, i}};
        }
    }
    return {};
}
}
