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
module recastnav.codec.tile;
import std;
#include "CodecDetail.hpp"

namespace recastnav::codec {
std::expected<std::vector<std::byte>, Failure> EncodeTile(const TileDocument& document, DocumentLimits limits) noexcept try {
    if (!limits.IsValid()) { return std::unexpected{Failure{Error::InvalidLimits}}; }
    if (document.GetVertices().size() > limits.GetMaximumVertices() || document.GetPolygons().size() > limits.GetMaximumPolygons()
        || document.GetOffMeshConnections().size() > limits.GetMaximumOffMeshConnections()) { return std::unexpected{Failure{Error::BudgetExceeded}}; }
    Writer writer{limits.GetMaximumBytes(), kTileMagic}; writer.Metadata(document.GetMetadata());
    writer.Count(document.GetVertices().size()); writer.Count(document.GetPolygons().size());
    writer.Count(document.GetDetails().size()); writer.Count(document.GetOffMeshConnections().size());
    for (const auto& vertex : document.GetVertices()) { writer.Point(vertex); }
    for (const auto& polygon : document.GetPolygons()) {
        if (polygon.GetVertices().size() > limits.GetMaximumPolygonVertices()) { Refuse(Error::BudgetExceeded); }
        writer.Count(polygon.GetVertices().size());
        for (const auto vertex : polygon.GetVertices()) { writer.Count(vertex.GetValue()); }
        writer.U32(polygon.GetArea()); writer.Bool(polygon.IsEnabled()); writer.Categories(polygon.GetCategories(), limits);
        for (const auto& edge : polygon.GetEdges()) {
            if (const auto* neighbour{std::get_if<PolygonIndex>(&edge)}) { writer.U8(1u); writer.Count(neighbour->GetValue()); }
            else if (const auto* side{std::get_if<PortalSide>(&edge)}) { writer.U8(2u); writer.U64(static_cast<std::uint32_t>(*side)); }
            else { writer.U8(0u); writer.U64(0u); }
        }
    }
    std::size_t detailVertices{}, detailTriangles{};
    for (const auto& detail : document.GetDetails()) {
        if (detail.GetAdditionalVertices().size() > limits.GetMaximumDetailVertices() - detailVertices
            || detail.GetTriangles().size() > limits.GetMaximumDetailTriangles() - detailTriangles) { Refuse(Error::BudgetExceeded); }
        detailVertices += detail.GetAdditionalVertices().size(); detailTriangles += detail.GetTriangles().size();
        writer.Count(detail.GetAdditionalVertices().size()); writer.Count(detail.GetTriangles().size());
        for (const auto& point : detail.GetAdditionalVertices()) { writer.Point(point); }
        for (const auto& triangle : detail.GetTriangles()) {
            for (const auto vertex : triangle.GetVertices()) { writer.Count(vertex.GetValue()); }
            for (const auto boundary : triangle.GetBoundaryEdges()) { writer.Bool(boundary); }
        }
    }
    for (const auto& link : document.GetOffMeshConnections()) {
        writer.Point(link.GetStart()); writer.Point(link.GetEnd()); writer.F32(link.GetRadius());
        writer.U64(link.GetUserId()); writer.U32(link.GetArea()); writer.Bool(link.IsBidirectional()); writer.Categories(link.GetCategories(), limits);
    }
    return writer.Finish();
} catch (const CodecFailure& failure) { return std::unexpected{failure.GetFailure()}; }
catch (const std::bad_alloc&) { return std::unexpected{Failure{Error::AllocationFailure}}; }
catch (const std::length_error&) { return std::unexpected{Failure{Error::SizeOverflow}}; }

std::expected<TileDocument, Failure> DecodeTile(std::span<const std::byte> bytes, DocumentLimits limits) noexcept try {
    auto reader{OpenFrame(bytes, limits, kTileMagic)}; const auto metadata{reader.Metadata()};
    const auto vertexCount{reader.Count(limits.GetMaximumVertices(), 12u)};
    const auto polygonCount{reader.Count(limits.GetMaximumPolygons(), 64u)};
    const auto detailCount{reader.Count(limits.GetMaximumPolygons(), 16u)};
    const auto offMeshCount{reader.Count(limits.GetMaximumOffMeshConnections(), 49u)};
    if (detailCount != 0u && detailCount != polygonCount) { Refuse(Error::InvalidDetailMesh, reader.Position()); }
    std::vector<Vec3> vertices{}; vertices.reserve(vertexCount);
    for (std::size_t i{}; i < vertexCount; ++i) { vertices.push_back(reader.Point()); }
    std::vector<Polygon> polygons{}; polygons.reserve(polygonCount);
    for (std::size_t i{}; i < polygonCount; ++i) {
        const auto count{reader.Count(limits.GetMaximumPolygonVertices(), 17u)};
        if (count < 3u) { Refuse(Error::InvalidTopology, reader.Position()); }
        std::vector<VertexIndex> indices{}; indices.reserve(count);
        for (std::size_t j{}; j < count; ++j) { indices.emplace_back(reader.Index()); }
        const auto area{reader.U32()}; const auto enabled{reader.Bool()}; auto categories{reader.Categories(limits)};
        std::vector<EdgeConnection> edges{}; edges.reserve(count);
        for (std::size_t j{}; j < count; ++j) {
            const auto kind{reader.U8()}; const auto value{reader.U64()};
            if (kind == 0u && value == 0u) { edges.emplace_back(std::monostate{}); }
            else if (kind == 1u && value <= std::numeric_limits<std::size_t>::max()) { edges.emplace_back(PolygonIndex{static_cast<std::size_t>(value)}); }
            else if (kind == 2u && value <= static_cast<std::uint32_t>(PortalSide::NegativeZ)) { edges.emplace_back(static_cast<PortalSide>(value)); }
            else { Refuse(Error::InvalidEncoding, reader.Position() - 9u); }
        }
        polygons.emplace_back(std::move(indices), std::move(edges), area, std::move(categories), enabled);
    }
    std::vector<DetailMesh> details{}; details.reserve(detailCount); std::size_t detailVertices{}, detailTriangles{};
    for (std::size_t i{}; i < detailCount; ++i) {
        const auto pointCount{reader.Count(limits.GetMaximumDetailVertices() - detailVertices, 12u)};
        const auto triangleCount{reader.Count(limits.GetMaximumDetailTriangles() - detailTriangles, 27u)};
        detailVertices += pointCount; detailTriangles += triangleCount;
        std::vector<Vec3> points{}; points.reserve(pointCount);
        for (std::size_t j{}; j < pointCount; ++j) { points.push_back(reader.Point()); }
        std::vector<DetailTriangle> triangles{}; triangles.reserve(triangleCount);
        for (std::size_t j{}; j < triangleCount; ++j) {
            std::array<DetailVertexIndex, 3> indices{}; for (auto& index : indices) { index = DetailVertexIndex{reader.Index()}; }
            std::array<bool, 3> boundaries{}; for (auto& boundary : boundaries) { boundary = reader.Bool(); }
            triangles.emplace_back(indices, boundaries);
        }
        details.emplace_back(std::move(points), std::move(triangles));
    }
    std::vector<OffMeshConnection> links{}; links.reserve(offMeshCount);
    for (std::size_t i{}; i < offMeshCount; ++i) {
        const auto start{reader.Point()}; const auto end{reader.Point()}; const auto radius{reader.F32()};
        const auto userId{reader.U64()}; const auto area{reader.U32()}; const auto bidirectional{reader.Bool()};
        links.emplace_back(start, end, radius, userId, area, reader.Categories(limits), bidirectional);
    }
    reader.End();
    return TileDocument::Create(metadata, std::move(vertices), std::move(polygons), std::move(details), std::move(links), limits);
} catch (const CodecFailure& failure) { return std::unexpected{failure.GetFailure()}; }
catch (const std::bad_alloc&) { return std::unexpected{Failure{Error::AllocationFailure}}; }
catch (const std::length_error&) { return std::unexpected{Failure{Error::SizeOverflow}}; }
}
