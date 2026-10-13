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
module recastnav.mesh;
import std;
#include "Geometry.hpp"

namespace recastnav {
namespace {
struct Portal final {
    std::size_t node{};
    PortalSide side{};
    Vec3 a{}, b{};
};
[[nodiscard]] bool IsXSide(PortalSide side) noexcept {
    return side == PortalSide::PositiveX || side == PortalSide::NegativeX;
}
[[nodiscard]] bool IsPositive(PortalSide side) noexcept {
    return side == PortalSide::PositiveX || side == PortalSide::PositiveZ;
}
[[nodiscard]] bool AdjacentKeys(const TileKey& a, const TileKey& b, PortalSide side) noexcept {
    const auto x{a.GetX()}, z{a.GetZ()};
    switch (side) {
    case PortalSide::PositiveX: return x != std::numeric_limits<std::int64_t>::max() && x + 1 == b.GetX() && z == b.GetZ();
    case PortalSide::NegativeX: return x != std::numeric_limits<std::int64_t>::min() && x - 1 == b.GetX() && z == b.GetZ();
    case PortalSide::PositiveZ: return z != std::numeric_limits<std::int64_t>::max() && z + 1 == b.GetZ() && x == b.GetX();
    case PortalSide::NegativeZ: return z != std::numeric_limits<std::int64_t>::min() && z - 1 == b.GetZ() && x == b.GetX();
    }
    std::unreachable();
}
[[nodiscard]] Vec3 PortalPoint(const Portal& portal, double along) noexcept {
    const double a{IsXSide(portal.side) ? portal.a.GetZ() : portal.a.GetX()};
    const double b{IsXSide(portal.side) ? portal.b.GetZ() : portal.b.GetX()};
    return Interpolate(portal.a, portal.b, (along - a) / (b - a));
}
}
const TileKey& PolygonLocation::GetTileKey() const noexcept { return m_snapshot->m_tiles[m_tile]->GetMetadata().GetKey(); }
std::expected<SnapshotLease, MeshError> MeshSnapshot::Create(std::vector<TileLease> tiles, MeshLimits limits) noexcept {
    if (limits.GetMaximumTiles() == 0u || limits.GetMaximumPolygons() == 0u || limits.GetMaximumDocumentBytes() == 0u) {
        return std::unexpected{MeshError::InvalidLimits};
    }
    if (tiles.size() > limits.GetMaximumTiles()) { return std::unexpected{MeshError::CapacityExceeded}; }
    for (const auto& tile : tiles) { if (!tile) { return std::unexpected{MeshError::MissingTileLease}; } }
    std::size_t polygonCount{}, documentBytes{};
    const auto charge = [&](std::size_t count, std::size_t width) {
        const std::size_t remaining{limits.GetMaximumDocumentBytes() - documentBytes};
        if (count > remaining / width) { return false; }
        documentBytes += count * width; return true;
    };
    for (const auto& tile : tiles) {
        if (tile->GetPolygons().size() > limits.GetMaximumPolygons() - polygonCount) { return std::unexpected{MeshError::CapacityExceeded}; }
        polygonCount += tile->GetPolygons().size();
        if (!charge(1u, sizeof(TileDocument)) || !charge(tile->GetVertices().size(), sizeof(Vec3))
            || !charge(tile->GetPolygons().size(), sizeof(Polygon)) || !charge(tile->GetDetails().size(), sizeof(DetailMesh))
            || !charge(tile->GetOffMeshConnections().size(), sizeof(OffMeshConnection))) { return std::unexpected{MeshError::CapacityExceeded}; }
        for (const auto& polygon : tile->GetPolygons()) {
            if (!charge(polygon.GetVertices().size(), sizeof(VertexIndex)) || !charge(polygon.GetEdges().size(), sizeof(EdgeConnection))
                || !charge(polygon.GetCategories().size(), sizeof(std::uint32_t))) { return std::unexpected{MeshError::CapacityExceeded}; }
        }
        for (const auto& detail : tile->GetDetails()) {
            if (!charge(detail.GetAdditionalVertices().size(), sizeof(Vec3)) || !charge(detail.GetTriangles().size(), sizeof(DetailTriangle))) {
                return std::unexpected{MeshError::CapacityExceeded};
            }
        }
        for (const auto& link : tile->GetOffMeshConnections()) {
            if (!charge(link.GetCategories().size(), sizeof(std::uint32_t))) { return std::unexpected{MeshError::CapacityExceeded}; }
        }
    }
    try {
        std::sort(tiles.begin(), tiles.end(), [](const TileLease& a, const TileLease& b) {
            return a->GetMetadata().GetKey() < b->GetMetadata().GetKey();
        });
        for (std::size_t i{1}; i < tiles.size(); ++i) {
            if (tiles[i - 1u]->GetMetadata().GetKey() == tiles[i]->GetMetadata().GetKey()) {
                return std::unexpected{MeshError::DuplicateTile};
            }
            if (tiles[i - 1u]->GetMetadata().GetProfile() != tiles[i]->GetMetadata().GetProfile()) {
                return std::unexpected{MeshError::IncompatibleProfile};
            }
        }
        auto snapshot{std::make_shared<MeshSnapshot>(ConstructionKey{})};
        snapshot->m_tiles.reserve(tiles.size());
        for (const auto& tile : tiles) { snapshot->m_tiles.push_back(std::make_shared<const TileDocument>(*tile)); }
        if (const auto built{snapshot->Build(limits)}; !built) { return std::unexpected{built.error()}; }
        return SnapshotLease{std::move(snapshot)};
    } catch (const std::bad_alloc&) { return std::unexpected{MeshError::AllocationFailure}; }
      catch (const std::length_error&) { return std::unexpected{MeshError::CapacityExceeded}; }
}
std::expected<std::optional<PolygonLocation>, MeshError> MeshSnapshot::Locate(const TileKey& tile, PolygonIndex polygon) const noexcept {
    const auto found{std::lower_bound(m_tiles.begin(), m_tiles.end(), tile, [](const TileLease& candidate, const TileKey& key) {
        return candidate->GetMetadata().GetKey() < key;
    })};
    if (found == m_tiles.end() || (*found)->GetMetadata().GetKey() != tile) { return std::optional<PolygonLocation>{}; }
    if (polygon.GetValue() >= (*found)->GetPolygons().size()) { return std::unexpected{MeshError::InvalidPolygon}; }
    return std::optional<PolygonLocation>{PolygonLocation{shared_from_this(), static_cast<std::size_t>(found - m_tiles.begin()), polygon}};
}
std::expected<std::reference_wrapper<const Polygon>, MeshError> MeshSnapshot::GetPolygon(const PolygonLocation& location) const noexcept {
    if (location.m_snapshot.get() != this) { return std::unexpected{MeshError::SnapshotMismatch}; }
    return std::cref(m_tiles[location.m_tile]->GetPolygons()[location.m_polygon.GetValue()]);
}
std::expected<void, MeshError> MeshSnapshot::Build(MeshLimits limits) {
    std::size_t count{};
    m_tileOffsets.reserve(m_tiles.size());
    for (const auto& tile : m_tiles) {
        m_tileOffsets.push_back(count);
        if (tile->GetPolygons().size() > limits.GetMaximumPolygons() - count) { return std::unexpected{MeshError::CapacityExceeded}; }
        count += tile->GetPolygons().size();
    }
    m_nodes.reserve(count);
    for (std::size_t tile{}; tile < m_tiles.size(); ++tile) {
        for (std::size_t polygon{}; polygon < m_tiles[tile]->GetPolygons().size(); ++polygon) {
            const PolygonIndex index{polygon};
            m_nodes.push_back(Node{tile, index, PolygonCentre(*m_tiles[tile], index), PolygonBounds(*m_tiles[tile], index), {}});
        }
    }
    const auto addArc = [&](std::size_t source, std::size_t destination, MeshTransition transition,
        std::optional<std::pair<std::size_t, std::size_t>> offMesh = {}) -> bool {
        if (m_arcCount == limits.GetMaximumArcs()) { return false; }
        m_nodes[source].arcs.push_back(Arc{destination, transition, offMesh}); ++m_arcCount; return true;
    };
    // Bucketing by plane avoids comparisons against unrelated tile boundaries.
    std::map<std::pair<bool, float>, std::vector<Portal>> portals{};
    for (std::size_t nodeIndex{}; nodeIndex < m_nodes.size(); ++nodeIndex) {
        const auto& node{m_nodes[nodeIndex]}; const auto& tile{*m_tiles[node.tile]};
        const auto& polygon{tile.GetPolygons()[node.polygon.GetValue()]};
        for (std::size_t edge{}; edge < polygon.GetEdges().size(); ++edge) {
            const auto& a{tile.GetVertices()[polygon.GetVertices()[edge].GetValue()]};
            const auto& b{tile.GetVertices()[polygon.GetVertices()[(edge + 1u) % polygon.GetVertices().size()].GetValue()]};
            if (const auto* neighbour{std::get_if<PolygonIndex>(&polygon.GetEdges()[edge])}) {
                const auto point{Interpolate(a, b, 0.5)};
                if (!addArc(nodeIndex, m_tileOffsets[node.tile] + neighbour->GetValue(), MeshTransition{point, point})) {
                    return std::unexpected{MeshError::CapacityExceeded};
                }
            } else if (const auto* side{std::get_if<PortalSide>(&polygon.GetEdges()[edge])}) {
                const bool xSide{IsXSide(*side)};
                const float plane{xSide ? a.GetX() : a.GetZ()};
                portals[{xSide, plane}].push_back(Portal{nodeIndex, *side, a, b});
            }
        }
    }
    std::size_t work{};
    for (const auto& [plane, bucket] : portals) {
        for (std::size_t i{}; i < bucket.size(); ++i) {
            const auto& a{bucket[i]};
            for (std::size_t j{i + 1u}; j < bucket.size(); ++j) {
                if (work == limits.GetMaximumConnectionWork()) { return std::unexpected{MeshError::CapacityExceeded}; }
                ++work;
                const auto& b{bucket[j]};
                if (IsPositive(a.side) == IsPositive(b.side)) { continue; }
                const auto& aTile{*m_tiles[m_nodes[a.node].tile]}; const auto& bTile{*m_tiles[m_nodes[b.node].tile]};
                if (!AdjacentKeys(aTile.GetMetadata().GetKey(), bTile.GetMetadata().GetKey(), a.side)) { continue; }
                const auto coordinate = [&](const Vec3& p) -> double { return plane.first ? p.GetZ() : p.GetX(); };
                const double low{std::max(std::min(coordinate(a.a), coordinate(a.b)), std::min(coordinate(b.a), coordinate(b.b)))};
                const double high{std::min(std::max(coordinate(a.a), coordinate(a.b)), std::max(coordinate(b.a), coordinate(b.b)))};
                if (low >= high) { continue; }
                const double climb{aTile.GetMetadata().GetProfile().GetMaximumClimb()};
                const auto height = [&](const Portal& portal, double along) {
                    const double t{(along - coordinate(portal.a)) / (coordinate(portal.b) - coordinate(portal.a))};
                    return portal.a.GetY() + (static_cast<double>(portal.b.GetY()) - portal.a.GetY()) * t;
                };
                const double lowDifference{height(a, low) - height(b, low)}, highDifference{height(a, high) - height(b, high)};
                double first{}, last{1.0};
                if (lowDifference == highDifference) {
                    if (std::abs(lowDifference) > climb) { continue; }
                } else {
                    const double lowerRoot{(-climb - lowDifference) / (highDifference - lowDifference)};
                    const double upperRoot{(climb - lowDifference) / (highDifference - lowDifference)};
                    first = std::max(0.0, std::min(lowerRoot, upperRoot));
                    last = std::min(1.0, std::max(lowerRoot, upperRoot));
                    if (first >= last) { continue; }
                }
                const double midpoint{low + (high - low) * ((first + last) * 0.5)};
                const auto departure{PortalPoint(a, midpoint)}, arrival{PortalPoint(b, midpoint)};
                if (!addArc(a.node, b.node, MeshTransition{departure, arrival})
                    || !addArc(b.node, a.node, MeshTransition{arrival, departure})) {
                    return std::unexpected{MeshError::CapacityExceeded};
                }
            }
        }
    }
    const auto landing = [&](const Vec3& position, float radius, std::optional<std::size_t> owner)
        -> std::expected<std::optional<std::pair<std::size_t, Vec3>>, MeshError> {
        std::optional<std::pair<std::size_t, Vec3>> best{};
        double bestDistance{std::numeric_limits<double>::infinity()};
        const std::size_t begin{owner ? m_tileOffsets[*owner] : 0u};
        const std::size_t end{owner ? begin + m_tiles[*owner]->GetPolygons().size() : m_nodes.size()};
        for (std::size_t i{begin}; i < end; ++i) {
            const auto& node{m_nodes[i]};
            if (work == limits.GetMaximumConnectionWork()) { return std::unexpected{MeshError::CapacityExceeded}; }
            ++work;
            const auto& tile{*m_tiles[node.tile]};
            const double climb{tile.GetMetadata().GetProfile().GetMaximumClimb()};
            if (!IntersectsSearch(node.bounds, position, Vec3{radius, static_cast<float>(climb), radius})) { continue; }
            const auto point{ClosestSurfaceXZ(tile, node.polygon, position)};
            const double dx{static_cast<double>(position.GetX()) - point.GetX()}, dz{static_cast<double>(position.GetZ()) - point.GetZ()};
            if (dx * dx + dz * dz > static_cast<double>(radius) * radius
                || std::abs(static_cast<double>(position.GetY()) - point.GetY()) > climb) { continue; }
            const double distance{DistanceSquared(position, point)};
            if (distance < bestDistance) { bestDistance = distance; best = std::pair{i, point}; }
        }
        return best;
    };
    for (std::size_t tile{}; tile < m_tiles.size(); ++tile) {
        const auto connections{m_tiles[tile]->GetOffMeshConnections()};
        for (std::size_t i{}; i < connections.size(); ++i) {
            const auto& link{connections[i]};
            const auto start{landing(link.GetStart(), link.GetRadius(), tile)};
            if (!start) { return std::unexpected{start.error()}; }
            if (!*start) { continue; }
            const auto end{landing(link.GetEnd(), link.GetRadius(), {})};
            if (!end) { return std::unexpected{end.error()}; }
            if (!*start || !*end) { continue; }
            const auto& [from, departure]{**start}; const auto& [to, arrival]{**end};
            if (!addArc(from, to, MeshTransition{departure, arrival, link.GetUserId()}, std::pair{tile, i})
                || (link.IsBidirectional() && !addArc(to, from, MeshTransition{arrival, departure, link.GetUserId()}, std::pair{tile, i}))) {
                return std::unexpected{MeshError::CapacityExceeded};
            }
        }
    }
    // Stable sorting preserves authored edge/link order when destinations tie.
    for (auto& node : m_nodes) {
        std::stable_sort(node.arcs.begin(), node.arcs.end(), [](const Arc& a, const Arc& b) { return a.destination < b.destination; });
    }
    return {};
}
}
