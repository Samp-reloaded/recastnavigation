// Copyright (c) 2026 Samp-reloaded contributors.
// Altered implementation; distributed under the repository's zlib license.
module;
#include "Recast.h"
#include "LegacyAllocation.hpp"
module recastnav.build;
import std;

namespace recastnav {
namespace {
class OracleContext final : public rcContext {
public:
    OracleContext() : rcContext{true} { enableTimer(false); }
    [[nodiscard]] bool HasError() const noexcept { return m_error; }
private:
    void doLog(rcLogCategory category, const char*, int) override { m_error = m_error || category == RC_LOG_ERROR; }
    bool m_error{};
};
[[nodiscard]] bool OptionsValid(const BuildOptions& options) noexcept {
    return options.GetMinimumRegionCells() <= static_cast<std::size_t>(std::numeric_limits<int>::max())
        && options.GetMergeRegionCells() <= static_cast<std::size_t>(std::numeric_limits<int>::max())
        && std::isfinite(options.GetContourMaximumErrorCells()) && options.GetContourMaximumErrorCells() >= 0
        && std::isfinite(options.GetMaximumEdgeLength()) && options.GetMaximumEdgeLength() >= 0
        && std::isfinite(options.GetDetailSampleDistanceCells())
        && (options.GetDetailSampleDistanceCells() == 0 || options.GetDetailSampleDistanceCells() >= 0.9f)
        && std::isfinite(options.GetDetailMaximumErrorCells()) && options.GetDetailMaximumErrorCells() >= 0;
}
void CheckCancellation(const BuildContext& context) {
    if (context.GetCancellation().stop_requested()) { throw cooking::CookingCancelled{}; }
}
[[nodiscard]] bool LegacyNormalRepresentable(const Vec3& a, const Vec3& b, const Vec3& c) noexcept {
    const auto ab{b - a}, ac{c - a};
    const float x{ab.GetY() * ac.GetZ() - ab.GetZ() * ac.GetY()};
    const float y{ab.GetZ() * ac.GetX() - ab.GetX() * ac.GetZ()};
    const float z{ab.GetX() * ac.GetY() - ab.GetY() * ac.GetX()};
    const float lengthSquared{x * x + y * y + z * z};
    return std::isfinite(lengthSquared) && lengthSquared > 0;
}
[[nodiscard]] std::expected<int, BuildFailure> VoxelCount(double value, double maximum) noexcept {
    if (!std::isfinite(value) || value < 0 || value > maximum) {
        return std::unexpected{BuildFailure{BuildError::LegacyRepresentationLimit, BuildStage::Input}};
    }
    return static_cast<int>(value);
}
[[nodiscard]] std::expected<rcConfig, BuildFailure> Configuration(const BuildInput& input, const BuildContext& context) noexcept {
    const auto& bounds{input.GetBounds()}; const auto& profile{input.GetProfile()}; const auto& options{input.GetOptions()};
    if (!bounds.IsValid() || bounds.GetMinimum().GetX() == bounds.GetMaximum().GetX()
        || bounds.GetMinimum().GetZ() == bounds.GetMaximum().GetZ()) {
        return std::unexpected{BuildFailure{BuildError::InvalidInput, BuildStage::Input, Failure{Error::InvalidBounds}}};
    }
    if (!OptionsValid(options)) {
        return std::unexpected{BuildFailure{BuildError::InvalidInput, BuildStage::Input, Failure{Error::InvalidProfile}}};
    }
    const auto cellsX{VoxelCount(std::ceil((static_cast<double>(bounds.GetMaximum().GetX()) - bounds.GetMinimum().GetX()) / profile.GetCellSize()), 65534)};
    const auto cellsZ{VoxelCount(std::ceil((static_cast<double>(bounds.GetMaximum().GetZ()) - bounds.GetMinimum().GetZ()) / profile.GetCellSize()), 65534)};
    const auto height{VoxelCount(std::ceil(static_cast<double>(profile.GetAgentHeight()) / profile.GetCellHeight()), 255)};
    const auto climb{VoxelCount(std::floor(static_cast<double>(profile.GetMaximumClimb()) / profile.GetCellHeight()), RC_SPAN_MAX_HEIGHT)};
    const auto radius{VoxelCount(std::ceil(static_cast<double>(profile.GetAgentRadius()) / profile.GetCellSize()), 127)};
    const auto edge{VoxelCount(std::floor(static_cast<double>(options.GetMaximumEdgeLength()) / profile.GetCellSize()), 65534)};
    for (const auto& value : {cellsX, cellsZ, height, climb, radius, edge}) {
        if (!value) { return std::unexpected{value.error()}; }
    }
    const auto gridCells{CheckedProduct(static_cast<std::size_t>(*cellsX), static_cast<std::size_t>(*cellsZ),
        std::min<std::size_t>(context.GetMaximumGridCells(), 16'777'215u))};
    if (!gridCells) { return std::unexpected{BuildFailure{BuildError::BudgetExceeded, BuildStage::Input}}; }
    rcConfig config{};
    config.width = *cellsX; config.height = *cellsZ;
    config.cs = profile.GetCellSize(); config.ch = profile.GetCellHeight();
    config.walkableSlopeAngle = profile.GetMaximumSlopeDegrees();
    config.walkableHeight = *height; config.walkableClimb = *climb; config.walkableRadius = *radius;
    config.maxEdgeLen = *edge; config.maxSimplificationError = options.GetContourMaximumErrorCells();
    config.minRegionArea = static_cast<int>(options.GetMinimumRegionCells());
    config.mergeRegionArea = static_cast<int>(options.GetMergeRegionCells());
    config.maxVertsPerPoly = static_cast<int>(context.GetDocumentLimits().GetMaximumPolygonVertices());
    config.detailSampleDist = options.GetDetailSampleDistanceCells() * config.cs;
    config.detailSampleMaxError = options.GetDetailMaximumErrorCells() * config.ch;
    config.bmin[0] = bounds.GetMinimum().GetX(); config.bmin[1] = bounds.GetMinimum().GetY(); config.bmin[2] = bounds.GetMinimum().GetZ();
    // Expand to complete raster cells, and two vertical quantization cells. Published metadata
    // uses these effective bounds so quantized vertices and detail heights remain contained.
    config.bmax[0] = config.bmin[0] + static_cast<float>(config.width) * config.cs;
    config.bmax[1] = bounds.GetMaximum().GetY() + 2.0f * config.ch;
    config.bmax[2] = config.bmin[2] + static_cast<float>(config.height) * config.cs;
    const auto vertical{(static_cast<double>(config.bmax[1]) - config.bmin[1]) / config.ch};
    if (!std::isfinite(config.bmax[0]) || !std::isfinite(config.bmax[1]) || !std::isfinite(config.bmax[2])
        || config.bmax[0] <= config.bmin[0] || config.bmax[1] <= config.bmin[1] || config.bmax[2] <= config.bmin[2]
        || vertical >= RC_SPAN_MAX_HEIGHT || !std::isfinite(config.detailSampleDist)
        || !std::isfinite(config.detailSampleMaxError)) {
        return std::unexpected{BuildFailure{BuildError::LegacyRepresentationLimit, BuildStage::Input}};
    }
    return config;
}
// Stable non-cryptographic provenance fingerprint, with canonical little-endian words.
[[nodiscard]] CookFingerprint Fingerprint(const BuildInput& input, const DocumentLimits& limits) noexcept {
    std::array<std::uint64_t, 4> hashes{14695981039346656037ull, 7809847782465536322ull, 9650029242287828579ull, 2870177450012600261ull};
    const auto word = [&](std::uint64_t value) {
        for (unsigned byte{}; byte < 8u; ++byte) {
            for (auto& hash : hashes) { hash ^= (value >> (byte * 8u)) & 255u; hash *= 1099511628211ull; }
        }
    };
    const auto scalar = [&](float value) { word(std::bit_cast<std::uint32_t>(value)); };
    const auto point = [&](const Vec3& value) { scalar(value.GetX()); scalar(value.GetY()); scalar(value.GetZ()); };
    word(1u); word(input.GetVertices().size()); word(input.GetTriangles().size());
    for (const auto& vertex : input.GetVertices()) { point(vertex); }
    for (const auto& triangle : input.GetTriangles()) {
        for (const auto index : triangle.GetVertices()) { word(index.GetValue()); }
        word(triangle.GetArea());
    }
    point(input.GetBounds().GetMinimum()); point(input.GetBounds().GetMaximum());
    const auto& profile{input.GetProfile()}; const auto& options{input.GetOptions()};
    scalar(profile.GetCellSize()); scalar(profile.GetCellHeight()); scalar(profile.GetAgentHeight());
    scalar(profile.GetAgentRadius()); scalar(profile.GetMaximumClimb()); scalar(profile.GetMaximumSlopeDegrees());
    word(options.GetMinimumRegionCells()); word(options.GetMergeRegionCells()); scalar(options.GetContourMaximumErrorCells());
    scalar(options.GetMaximumEdgeLength()); scalar(options.GetDetailSampleDistanceCells()); scalar(options.GetDetailMaximumErrorCells());
    word(limits.GetMaximumPolygonVertices());
    CookFingerprint result{};
    for (std::size_t i{}; i < hashes.size(); ++i) {
        for (unsigned byte{}; byte < 8u; ++byte) { result[i * 8u + byte] = static_cast<std::byte>((hashes[i] >> (byte * 8u)) & 255u); }
    }
    return result;
}
[[nodiscard]] std::expected<EdgeConnection, BuildFailure> DecodeLegacyEdge(unsigned short encoded, std::size_t polygons) noexcept {
    if (encoded == RC_MESH_NULL_IDX) { return EdgeConnection{std::monostate{}}; }
    if ((encoded & 0x8000u) == 0u) {
        if (encoded >= polygons) { return std::unexpected{BuildFailure{BuildError::AlgorithmFailure, BuildStage::Document}}; }
        return EdgeConnection{PolygonIndex{encoded}};
    }
    switch (encoded & 0x7fffu) {
    case 0u: return EdgeConnection{PortalSide::NegativeX};
    case 1u: return EdgeConnection{PortalSide::PositiveZ};
    case 2u: return EdgeConnection{PortalSide::PositiveX};
    case 3u: return EdgeConnection{PortalSide::NegativeZ};
    default: return std::unexpected{BuildFailure{BuildError::AlgorithmFailure, BuildStage::Document}};
    }
}
[[nodiscard]] std::array<bool, 3> DecodeLegacyDetailEdges(unsigned char encoded) noexcept {
    return {(encoded & 1u) != 0u, (encoded & 4u) != 0u, (encoded & 16u) != 0u};
}
[[nodiscard]] std::expected<TileDocument, BuildFailure> Publish(const TileMetadata& metadata, const rcPolyMesh& mesh,
    const rcPolyMeshDetail& detail, const BuildContext& context) {
    const auto& limits{context.GetDocumentLimits()};
    if (mesh.nverts < 0 || mesh.npolys < 0 || detail.nmeshes != mesh.npolys || detail.nverts < 0 || detail.ntris < 0) {
        return std::unexpected{BuildFailure{BuildError::AlgorithmFailure, BuildStage::Document}};
    }
    const auto vertexCount{static_cast<std::size_t>(mesh.nverts)}, polygonCount{static_cast<std::size_t>(mesh.npolys)};
    if (vertexCount > limits.GetMaximumVertices() || polygonCount > limits.GetMaximumPolygons()
        || static_cast<std::size_t>(detail.ntris) > limits.GetMaximumDetailTriangles()) {
        return std::unexpected{BuildFailure{BuildError::BudgetExceeded, BuildStage::Document}};
    }
    if (polygonCount >= 32768u) { return std::unexpected{BuildFailure{BuildError::LegacyRepresentationLimit, BuildStage::Document}}; }
    std::vector<Vec3> vertices{}; vertices.reserve(vertexCount);
    for (std::size_t i{}; i < vertexCount; ++i) {
        vertices.emplace_back(mesh.bmin[0] + static_cast<float>(mesh.verts[i * 3u]) * mesh.cs,
            mesh.bmin[1] + static_cast<float>(mesh.verts[i * 3u + 1u]) * mesh.ch,
            mesh.bmin[2] + static_cast<float>(mesh.verts[i * 3u + 2u]) * mesh.cs);
    }
    std::vector<Polygon> polygons{}; polygons.reserve(polygonCount);
    std::vector<DetailMesh> details{}; details.reserve(polygonCount);
    std::size_t additionalVertexCount{};
    for (std::size_t i{}; i < polygonCount; ++i) {
        CheckCancellation(context);
        const auto base{i * static_cast<std::size_t>(mesh.nvp) * 2u};
        std::vector<VertexIndex> polygonVertices{}; std::vector<EdgeConnection> edges{};
        for (std::size_t j{}; j < static_cast<std::size_t>(mesh.nvp); ++j) {
            const auto index{mesh.polys[base + j]}; if (index == RC_MESH_NULL_IDX) { break; }
            polygonVertices.emplace_back(index);
            const auto connection{DecodeLegacyEdge(mesh.polys[base + static_cast<std::size_t>(mesh.nvp) + j], polygonCount)};
            if (!connection) { return std::unexpected{connection.error()}; }
            edges.push_back(*connection);
        }
        const auto detailBase{static_cast<std::size_t>(detail.meshes[i * 4u])};
        const auto detailCount{static_cast<std::size_t>(detail.meshes[i * 4u + 1u])};
        const auto triangleBase{static_cast<std::size_t>(detail.meshes[i * 4u + 2u])};
        const auto triangleCount{static_cast<std::size_t>(detail.meshes[i * 4u + 3u])};
        if (detailBase > static_cast<std::size_t>(detail.nverts) || detailCount > static_cast<std::size_t>(detail.nverts) - detailBase
            || triangleBase > static_cast<std::size_t>(detail.ntris) || triangleCount > static_cast<std::size_t>(detail.ntris) - triangleBase
            || detailCount < polygonVertices.size()) {
            return std::unexpected{BuildFailure{BuildError::AlgorithmFailure, BuildStage::Document}};
        }
        const auto additionalCount{detailCount - polygonVertices.size()};
        if (additionalCount > limits.GetMaximumDetailVertices() - additionalVertexCount) {
            return std::unexpected{BuildFailure{BuildError::BudgetExceeded, BuildStage::Document}};
        }
        additionalVertexCount += additionalCount;
        std::vector<Vec3> additional{}; additional.reserve(additionalCount);
        for (std::size_t j{polygonVertices.size()}; j < detailCount; ++j) {
            const auto offset{(detailBase + j) * 3u};
            additional.emplace_back(detail.verts[offset], detail.verts[offset + 1u], detail.verts[offset + 2u]);
        }
        std::vector<DetailTriangle> triangles{}; triangles.reserve(triangleCount);
        for (std::size_t j{}; j < triangleCount; ++j) {
            const auto offset{(triangleBase + j) * 4u};
            triangles.emplace_back(std::array{DetailVertexIndex{detail.tris[offset]}, DetailVertexIndex{detail.tris[offset + 1u]},
                DetailVertexIndex{detail.tris[offset + 2u]}}, DecodeLegacyDetailEdges(detail.tris[offset + 3u]));
        }
        polygons.emplace_back(std::move(polygonVertices), std::move(edges), mesh.areas[i]);
        details.emplace_back(std::move(additional), std::move(triangles));
    }
    auto document{TileDocument::Create(metadata, std::move(vertices), std::move(polygons), std::move(details), {}, limits)};
    if (!document) { return std::unexpected{BuildFailure{BuildError::AlgorithmFailure, BuildStage::Document, document.error()}}; }
    return std::move(*document);
}
}

std::expected<TileDocument, BuildFailure> CookTile(const BuildInput& input, BuildContext& context) noexcept {
    BuildStage stage{BuildStage::Input};
    try {
        CheckCancellation(context);
        const auto valid{ValidateBuildInput(input.GetVertices(), input.GetTriangles(), input.GetProfile(), context.GetDocumentLimits())};
        if (!valid) { return std::unexpected{BuildFailure{BuildError::InvalidInput, stage, valid.error()}}; }
        if (input.GetVertices().size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 3)
            || input.GetTriangles().size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 3)) {
            return std::unexpected{BuildFailure{BuildError::LegacyRepresentationLimit, stage}};
        }
        const auto configResult{Configuration(input, context)};
        if (!configResult) { return std::unexpected{configResult.error()}; }
        const auto config{*configResult};
        for (std::size_t i{}; i < input.GetVertices().size(); ++i) {
            if (!input.GetBounds().Contains(input.GetVertices()[i])) {
                return std::unexpected{BuildFailure{BuildError::InvalidInput, stage, Failure{Error::InvalidBounds, 0, i}}};
            }
        }
        for (const auto& triangle : input.GetTriangles()) {
            const auto& indices{triangle.GetVertices()};
            if (!LegacyNormalRepresentable(input.GetVertices()[indices[0].GetValue()], input.GetVertices()[indices[1].GetValue()],
                input.GetVertices()[indices[2].GetValue()])) {
                return std::unexpected{BuildFailure{BuildError::LegacyRepresentationLimit, stage}};
            }
        }
        cooking::AllocationSession session{context.GetScratchResource(), context.GetMaximumScratchBytes(), context.GetCancellation()};
        std::pmr::vector<float> vertices{&session}; vertices.reserve(input.GetVertices().size() * 3u);
        for (const auto& vertex : input.GetVertices()) {
            vertices.push_back(vertex.GetX()); vertices.push_back(vertex.GetY()); vertices.push_back(vertex.GetZ());
        }
        std::pmr::vector<int> triangles{&session}; triangles.reserve(input.GetTriangles().size() * 3u);
        std::pmr::vector<unsigned char> areas{&session}; areas.reserve(input.GetTriangles().size());
        for (const auto& triangle : input.GetTriangles()) {
            for (const auto index : triangle.GetVertices()) { triangles.push_back(static_cast<int>(index.GetValue())); }
            areas.push_back(static_cast<unsigned char>(triangle.GetArea()));
        }
        const TileMetadata metadata{input.GetKey(), {{config.bmin[0], config.bmin[1], config.bmin[2]},
            {config.bmax[0], config.bmax[1], config.bmax[2]}}, input.GetProfile(), Fingerprint(input, context.GetDocumentLimits())};
        const auto emptyDocument = [&]() -> std::expected<TileDocument, BuildFailure> {
            CheckCancellation(context);
            auto empty{TileDocument::Create(metadata, {}, {}, {}, {}, context.GetDocumentLimits())};
            if (!empty) { return std::unexpected{BuildFailure{BuildError::AlgorithmFailure, BuildStage::Document, empty.error()}}; }
            return std::move(*empty);
        };
        if (input.GetTriangles().empty()) { return emptyDocument(); }
        OracleContext oracle{};
        const auto failed = [&]() { return std::unexpected{BuildFailure{BuildError::AlgorithmFailure, stage}}; };
        stage = BuildStage::Rasterization; CheckCancellation(context);
        std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> field{rcAllocHeightfield(), &rcFreeHeightField};
        if (!field || !rcCreateHeightfield(&oracle, *field, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch)) { return failed(); }
        rcClearUnwalkableTriangles(&oracle, config.walkableSlopeAngle, vertices.data(), static_cast<int>(input.GetVertices().size()),
            triangles.data(), static_cast<int>(input.GetTriangles().size()), areas.data());
        if (!rcRasterizeTriangles(&oracle, vertices.data(), static_cast<int>(input.GetVertices().size()), triangles.data(), areas.data(),
            static_cast<int>(input.GetTriangles().size()), *field, config.walkableClimb) || oracle.HasError()) { return failed(); }
        stage = BuildStage::Filtering; CheckCancellation(context);
        rcFilterLowHangingWalkableObstacles(&oracle, config.walkableClimb, *field);
        rcFilterLedgeSpans(&oracle, config.walkableHeight, config.walkableClimb, *field);
        rcFilterWalkableLowHeightSpans(&oracle, config.walkableHeight, *field);
        // Check the private compact-field encoding before it can truncate a cell count/index.
        std::size_t spanCount{};
        for (std::size_t i{}; i < static_cast<std::size_t>(config.width) * static_cast<std::size_t>(config.height); ++i) {
            std::size_t columnCount{};
            for (const rcSpan* span{field->spans[i]}; span; span = span->next) {
                if (span->area != RC_NULL_AREA) { ++columnCount; ++spanCount; }
            }
            if (columnCount > 63u || spanCount > 16'777'215u) {
                return std::unexpected{BuildFailure{BuildError::LegacyRepresentationLimit, stage}};
            }
        }
        if (spanCount == 0u) { return emptyDocument(); }
        stage = BuildStage::CompactField; CheckCancellation(context);
        std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> compact{rcAllocCompactHeightfield(), &rcFreeCompactHeightfield};
        if (!compact || !rcBuildCompactHeightfield(&oracle, config.walkableHeight, config.walkableClimb, *field, *compact) || oracle.HasError()) { return failed(); }
        field.reset();
        stage = BuildStage::Erosion; CheckCancellation(context);
        if (!rcErodeWalkableArea(&oracle, config.walkableRadius, *compact) || oracle.HasError()) { return failed(); }
        if (std::none_of(compact->areas, compact->areas + compact->spanCount, [](unsigned char area) { return area != RC_NULL_AREA; })) { return emptyDocument(); }
        stage = BuildStage::Regions; CheckCancellation(context);
        if (!rcBuildDistanceField(&oracle, *compact) || !rcBuildRegions(&oracle, *compact, 0, config.minRegionArea, config.mergeRegionArea)
            || oracle.HasError()) { return failed(); }
        stage = BuildStage::Contours; CheckCancellation(context);
        std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> contours{rcAllocContourSet(), &rcFreeContourSet};
        if (!contours || !rcBuildContours(&oracle, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours) || oracle.HasError()) { return failed(); }
        if (contours->nconts == 0) { return emptyDocument(); }
        stage = BuildStage::PolygonMesh; CheckCancellation(context);
        std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> mesh{rcAllocPolyMesh(), &rcFreePolyMesh};
        if (!mesh || !rcBuildPolyMesh(&oracle, *contours, config.maxVertsPerPoly, *mesh) || oracle.HasError()) { return failed(); }
        contours.reset();
        if (mesh->npolys == 0) { return emptyDocument(); }
        stage = BuildStage::DetailMesh; CheckCancellation(context);
        std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)> detail{rcAllocPolyMeshDetail(), &rcFreePolyMeshDetail};
        if (!detail || !rcBuildPolyMeshDetail(&oracle, *mesh, *compact, config.detailSampleDist, config.detailSampleMaxError, *detail)
            || oracle.HasError()) { return failed(); }
        stage = BuildStage::Document; CheckCancellation(context);
        return Publish(metadata, *mesh, *detail, context);
    } catch (const cooking::CookingCancelled&) { return std::unexpected{BuildFailure{BuildError::Cancelled, stage}}; }
      catch (const cooking::ScratchBudgetExceeded&) { return std::unexpected{BuildFailure{BuildError::BudgetExceeded, stage}}; }
      catch (const std::bad_alloc&) { return std::unexpected{BuildFailure{BuildError::AllocationFailure, stage}}; }
      catch (const std::length_error&) { return std::unexpected{BuildFailure{BuildError::BudgetExceeded, stage}}; }
}
}
