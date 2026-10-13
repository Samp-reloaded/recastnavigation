// Copyright (c) 2026 Samp-reloaded contributors.
// Altered implementation; distributed under the repository's zlib license.
export module recastnav.build;
import std;
import recastnav.document;

export namespace recastnav {
enum class BuildStage { Input, Rasterization, Filtering, CompactField, Erosion, Regions, Contours, PolygonMesh, DetailMesh, Document };
enum class BuildError { InvalidInput, Cancelled, BudgetExceeded, AllocationFailure, LegacyRepresentationLimit, AlgorithmFailure };
class BuildFailure final {
public:
    constexpr BuildFailure(BuildError error, BuildStage stage, std::optional<Failure> source = {}) noexcept
        : m_error{error}, m_stage{stage}, m_source{source} {}
    [[nodiscard]] constexpr BuildError GetError() const noexcept { return m_error; }
    [[nodiscard]] constexpr BuildStage GetStage() const noexcept { return m_stage; }
    [[nodiscard]] constexpr const std::optional<Failure>& GetSourceFailure() const noexcept { return m_source; }
private:
    BuildError m_error;
    BuildStage m_stage;
    std::optional<Failure> m_source;
};
class BuildOptions final {
public:
    constexpr BuildOptions(std::size_t minimumRegionCells = 0u, std::size_t mergeRegionCells = 400u,
        float contourMaximumErrorCells = 1.3f, float maximumEdgeLength = 12.0f,
        float detailSampleDistanceCells = 6.0f, float detailMaximumErrorCells = 1.0f) noexcept
        : m_minimumRegion{minimumRegionCells}, m_mergeRegion{mergeRegionCells}, m_contourError{contourMaximumErrorCells},
          m_edgeLength{maximumEdgeLength}, m_detailDistance{detailSampleDistanceCells}, m_detailError{detailMaximumErrorCells} {}
    [[nodiscard]] constexpr std::size_t GetMinimumRegionCells() const noexcept { return m_minimumRegion; }
    [[nodiscard]] constexpr std::size_t GetMergeRegionCells() const noexcept { return m_mergeRegion; }
    [[nodiscard]] constexpr float GetContourMaximumErrorCells() const noexcept { return m_contourError; }
    [[nodiscard]] constexpr float GetMaximumEdgeLength() const noexcept { return m_edgeLength; }
    [[nodiscard]] constexpr float GetDetailSampleDistanceCells() const noexcept { return m_detailDistance; }
    [[nodiscard]] constexpr float GetDetailMaximumErrorCells() const noexcept { return m_detailError; }
private:
    std::size_t m_minimumRegion{}, m_mergeRegion{};
    float m_contourError{}, m_edgeLength{}, m_detailDistance{}, m_detailError{};
};
class BuildInput final {
public:
    // Synchronous borrows; triangle winding is upward in the Y-up, XZ-ground convention.
    constexpr BuildInput(std::span<const Vec3> vertices, std::span<const Triangle> triangles, Aabb bounds,
        BuildProfile profile = {}, TileKey key = {}, BuildOptions options = {}) noexcept
        : m_vertices{vertices}, m_triangles{triangles}, m_bounds{bounds}, m_profile{profile}, m_key{key}, m_options{options} {}
    [[nodiscard]] constexpr std::span<const Vec3> GetVertices() const noexcept { return m_vertices; }
    [[nodiscard]] constexpr std::span<const Triangle> GetTriangles() const noexcept { return m_triangles; }
    [[nodiscard]] constexpr const Aabb& GetBounds() const noexcept { return m_bounds; }
    [[nodiscard]] constexpr const BuildProfile& GetProfile() const noexcept { return m_profile; }
    [[nodiscard]] constexpr const TileKey& GetKey() const noexcept { return m_key; }
    [[nodiscard]] constexpr const BuildOptions& GetOptions() const noexcept { return m_options; }
private:
    std::span<const Vec3> m_vertices;
    std::span<const Triangle> m_triangles;
    Aabb m_bounds;
    BuildProfile m_profile;
    TileKey m_key;
    BuildOptions m_options;
};
class BuildContext final {
public:
    // Resource is borrowed through this synchronous call and owns only cooking scratch.
    // The returned document owns independent standard-library storage.
    explicit BuildContext(std::pmr::memory_resource& scratch, std::stop_token cancellation = {},
        std::size_t maximumScratchBytes = 256u * 1024u * 1024u,
        std::size_t maximumGridCells = 4'194'304u, DocumentLimits documentLimits = {}) noexcept
        : m_scratch{scratch}, m_cancellation{cancellation}, m_bytes{maximumScratchBytes},
          m_gridCells{maximumGridCells}, m_limits{documentLimits} {}
    [[nodiscard]] std::pmr::memory_resource& GetScratchResource() const noexcept { return m_scratch.get(); }
    [[nodiscard]] const std::stop_token& GetCancellation() const noexcept { return m_cancellation; }
    [[nodiscard]] std::size_t GetMaximumScratchBytes() const noexcept { return m_bytes; }
    [[nodiscard]] std::size_t GetMaximumGridCells() const noexcept { return m_gridCells; }
    [[nodiscard]] const DocumentLimits& GetDocumentLimits() const noexcept { return m_limits; }
private:
    std::reference_wrapper<std::pmr::memory_resource> m_scratch;
    std::stop_token m_cancellation;
    std::size_t m_bytes{}, m_gridCells{};
    DocumentLimits m_limits;
};
// Deterministic for identical input/options on the same floating-point implementation.
// Cancellation is checked between pipeline stages and at scratch allocations.
[[nodiscard]] std::expected<TileDocument, BuildFailure> CookTile(const BuildInput& input, BuildContext& context) noexcept;
}
