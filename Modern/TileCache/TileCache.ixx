// Copyright (c) 2026 Samp-reloaded contributors.
// Altered implementation; distributed under the repository's zlib license.
export module recastnav.tilecache;
import std;
import recastnav.document;
import recastnav.mesh;

export namespace recastnav::tilecache {
enum class CacheError {
    InvalidLimits, MissingResource, InvalidDocument, InvalidBatch, MissingReplacement,
    DuplicateTile, MissingBatch, BatchNotBegun, ObstacleBusy, UnknownObstacle,
    StaleSnapshot, InvalidLocator, CapacityExceeded, AllocationFailure, Cancelled,
};
class CacheFailure final {
public:
    constexpr CacheFailure(CacheError error, std::optional<TileKey> tile = {},
        std::optional<Failure> document = {}, std::optional<MeshError> mesh = {}) noexcept
        : m_error{error}, m_tile{tile}, m_document{document}, m_mesh{mesh} {}
    [[nodiscard]] constexpr CacheError GetError() const noexcept { return m_error; }
    [[nodiscard]] constexpr const std::optional<TileKey>& GetTileKey() const noexcept { return m_tile; }
    [[nodiscard]] constexpr const std::optional<Failure>& GetDocumentFailure() const noexcept { return m_document; }
    [[nodiscard]] constexpr const std::optional<MeshError>& GetMeshFailure() const noexcept { return m_mesh; }
private:
    CacheError m_error;
    std::optional<TileKey> m_tile;
    std::optional<Failure> m_document;
    std::optional<MeshError> m_mesh;
};
class CacheLimits final {
public:
    constexpr CacheLimits(std::size_t tiles = 65'536u, std::size_t obstacles = 65'536u,
        std::size_t outstandingBatches = 64u, std::size_t affectedTilesPerBatch = 4096u,
        std::size_t queueBytes = 4u * 1024u * 1024u,
        std::size_t snapshotDocumentBytes = 512u * 1024u * 1024u,
        DocumentLimits documents = {}, MeshLimits mesh = {}) noexcept
        : m_tiles{tiles}, m_obstacles{obstacles}, m_batches{outstandingBatches}, m_affected{affectedTilesPerBatch},
          m_queueBytes{queueBytes}, m_documentBytes{snapshotDocumentBytes}, m_documents{documents}, m_mesh{mesh} {}
    [[nodiscard]] constexpr std::size_t GetMaximumTiles() const noexcept { return m_tiles; }
    [[nodiscard]] constexpr std::size_t GetMaximumObstacles() const noexcept { return m_obstacles; }
    [[nodiscard]] constexpr std::size_t GetMaximumOutstandingBatches() const noexcept { return m_batches; }
    [[nodiscard]] constexpr std::size_t GetMaximumAffectedTilesPerBatch() const noexcept { return m_affected; }
    [[nodiscard]] constexpr std::size_t GetMaximumQueueBytes() const noexcept { return m_queueBytes; }
    [[nodiscard]] constexpr std::size_t GetMaximumSnapshotDocumentBytes() const noexcept { return m_documentBytes; }
    [[nodiscard]] constexpr const DocumentLimits& GetDocumentLimits() const noexcept { return m_documents; }
    [[nodiscard]] constexpr const MeshLimits& GetMeshLimits() const noexcept { return m_mesh; }
private:
    std::size_t m_tiles{}, m_obstacles{}, m_batches{}, m_affected{}, m_queueBytes{}, m_documentBytes{};
    DocumentLimits m_documents;
    MeshLimits m_mesh;
};
class ObstacleKey final {
public:
    constexpr explicit ObstacleKey(std::uint64_t value) noexcept : m_value{value} {}
    [[nodiscard]] constexpr std::uint64_t GetValue() const noexcept { return m_value; }
    [[nodiscard]] constexpr auto operator<=>(const ObstacleKey&) const noexcept = default;
private:
    std::uint64_t m_value{};
};
class Obstacle final {
public:
    constexpr Obstacle(ObstacleKey key, Aabb bounds) noexcept : m_key{key}, m_bounds{bounds} {}
    [[nodiscard]] constexpr ObstacleKey GetKey() const noexcept { return m_key; }
    [[nodiscard]] constexpr const Aabb& GetBounds() const noexcept { return m_bounds; }
private:
    ObstacleKey m_key;
    Aabb m_bounds;
};
class ObstacleChange final {
public:
    // A disengaged requested bound explicitly removes the obstacle.
    constexpr ObstacleChange(ObstacleKey key, std::optional<Aabb> requestedBounds) noexcept
        : m_key{key}, m_bounds{requestedBounds} {}
    [[nodiscard]] constexpr ObstacleKey GetKey() const noexcept { return m_key; }
    [[nodiscard]] constexpr const std::optional<Aabb>& GetRequestedBounds() const noexcept { return m_bounds; }
private:
    ObstacleKey m_key;
    std::optional<Aabb> m_bounds;
};
enum class ObstaclePhase { Clean, Dirty, Pending };
class ObstacleStatus final {
public:
    constexpr ObstacleStatus(ObstaclePhase phase, std::optional<Aabb> applied, std::optional<Aabb> requested) noexcept
        : m_phase{phase}, m_applied{applied}, m_requested{requested} {}
    [[nodiscard]] constexpr ObstaclePhase GetPhase() const noexcept { return m_phase; }
    [[nodiscard]] constexpr bool IsGeometryDirty() const noexcept { return m_phase != ObstaclePhase::Clean; }
    [[nodiscard]] constexpr bool IsBuildPending() const noexcept { return m_phase == ObstaclePhase::Pending; }
    [[nodiscard]] constexpr const std::optional<Aabb>& GetAppliedBounds() const noexcept { return m_applied; }
    [[nodiscard]] constexpr const std::optional<Aabb>& GetRequestedBounds() const noexcept { return m_requested; }
private:
    ObstaclePhase m_phase;
    std::optional<Aabb> m_applied, m_requested;
};
class TileUpdate final {
public:
    TileUpdate(TileDocument tile, std::optional<LayerDocument> layer = {}) noexcept
        : m_tile{std::move(tile)}, m_layer{std::move(layer)} {}
    [[nodiscard]] const TileDocument& GetTile() const noexcept { return m_tile; }
    [[nodiscard]] const std::optional<LayerDocument>& GetLayer() const noexcept { return m_layer; }
private:
    TileDocument m_tile;
    std::optional<LayerDocument> m_layer;
};
// Decodes existing SRNT/SRNL documents; no cache-specific serialization dialect.
[[nodiscard]] std::expected<TileUpdate, CacheFailure> DecodeTileUpdate(std::span<const std::byte> tile,
    std::optional<std::span<const std::byte>> layer = {}, DocumentLimits limits = {}) noexcept;

class PublicationSnapshot;
using PublicationLease = std::shared_ptr<const PublicationSnapshot>;
using LayerLease = std::shared_ptr<const LayerDocument>;
class TileLocation final {
public:
    TileLocation(const TileLocation&) noexcept = default;
    TileLocation& operator=(const TileLocation&) noexcept = default;
    TileLocation(TileLocation&& other) noexcept : m_snapshot{other.m_snapshot}, m_index{other.m_index} {}
    TileLocation& operator=(TileLocation&& other) noexcept { m_snapshot = other.m_snapshot; m_index = other.m_index; return *this; }
    [[nodiscard]] const PublicationLease& GetSnapshot() const noexcept { return m_snapshot; }
    [[nodiscard]] const TileKey& GetTileKey() const noexcept;
    [[nodiscard]] bool operator==(const TileLocation&) const noexcept = default;
private:
    friend class PublicationSnapshot;
    TileLocation(PublicationLease snapshot, std::size_t index) noexcept : m_snapshot{std::move(snapshot)}, m_index{index} {}
    PublicationLease m_snapshot;
    std::size_t m_index{};
};
class PublicationSnapshot final : public std::enable_shared_from_this<PublicationSnapshot> {
    class ConstructionKey final { friend class PublicationSnapshot; ConstructionKey() = default; };
public:
    [[nodiscard]] static std::expected<PublicationLease, CacheFailure> Create(std::span<const TileUpdate> tiles,
        std::span<const Obstacle> appliedObstacles, std::shared_ptr<std::pmr::memory_resource> resource,
        CacheLimits limits = {}, std::stop_token cancellation = {}) noexcept;
    [[nodiscard]] const SnapshotLease& GetMesh() const noexcept { return m_mesh; }
    [[nodiscard]] std::span<const Obstacle> GetObstacles() const noexcept { return m_obstacles; }
    [[nodiscard]] std::expected<std::optional<TileLocation>, CacheFailure> Locate(const TileKey& key) const noexcept;
    [[nodiscard]] std::expected<std::reference_wrapper<const TileDocument>, CacheFailure> GetTile(const TileLocation& location) const noexcept;
    [[nodiscard]] std::expected<std::optional<std::reference_wrapper<const LayerDocument>>, CacheFailure> GetLayer(const TileLocation& location) const noexcept;
    PublicationSnapshot(const PublicationSnapshot&) = delete;
    PublicationSnapshot& operator=(const PublicationSnapshot&) = delete;
    PublicationSnapshot(ConstructionKey, std::shared_ptr<std::pmr::memory_resource> resource, SnapshotLease mesh,
        std::pmr::vector<LayerLease> layers, std::pmr::vector<Obstacle> obstacles) noexcept
        : m_resource{std::move(resource)}, m_mesh{std::move(mesh)}, m_layers{std::move(layers)}, m_obstacles{std::move(obstacles)} {}
private:
    friend class TileLocation;
    friend class TileCache;
    // Retain allocator authority until after all PMR containers have been destroyed.
    std::shared_ptr<std::pmr::memory_resource> m_resource;
    SnapshotLease m_mesh;
    std::pmr::vector<LayerLease> m_layers;
    std::pmr::vector<Obstacle> m_obstacles;
};
class TileCache;
class BatchRequest final {
    friend class TileCache;
    class ConstructionKey final { friend class TileCache; ConstructionKey() = default; };
public:
    BatchRequest(const BatchRequest&) = delete;
    BatchRequest& operator=(const BatchRequest&) = delete;
    BatchRequest(BatchRequest&&) = delete;
    BatchRequest& operator=(BatchRequest&&) = delete;
    [[nodiscard]] const PublicationLease& GetBaseSnapshot() const noexcept { return m_base; }
    [[nodiscard]] const ObstacleChange& GetObstacleChange() const noexcept { return m_change; }
    [[nodiscard]] std::span<const TileKey> GetAffectedTiles() const noexcept { return m_affected; }
    [[nodiscard]] const CacheLimits& GetLimits() const noexcept { return m_limits; }
    BatchRequest(ConstructionKey, PublicationLease base, ObstacleChange change, CacheLimits limits,
        std::pmr::vector<TileKey> affected) noexcept : m_base{std::move(base)}, m_change{change}, m_limits{limits}, m_affected{std::move(affected)} {}
private:
    PublicationLease m_base;
    ObstacleChange m_change;
    CacheLimits m_limits;
    std::pmr::vector<TileKey> m_affected;
};
using BatchRequestLease = std::shared_ptr<const BatchRequest>;
class BatchCandidate final {
public:
    [[nodiscard]] const BatchRequestLease& GetRequest() const noexcept { return m_request; }
    [[nodiscard]] const PublicationLease& GetSnapshot() const noexcept { return m_snapshot; }
private:
    friend class TileCache;
    BatchCandidate(BatchRequestLease request, PublicationLease snapshot) noexcept
        : m_request{std::move(request)}, m_snapshot{std::move(snapshot)} {}
    BatchRequestLease m_request;
    PublicationLease m_snapshot;
};
class TileCache final {
    struct Impl;
    class ConstructionKey final { friend class TileCache; ConstructionKey() = default; };
public:
    [[nodiscard]] static std::expected<std::unique_ptr<TileCache>, CacheFailure> Create(std::span<const TileUpdate> initialTiles,
        std::shared_ptr<std::pmr::memory_resource> resource, CacheLimits limits = {}, std::stop_token cancellation = {}) noexcept;
    [[nodiscard]] PublicationLease AcquireSnapshot() const noexcept;
    [[nodiscard]] std::expected<BatchRequestLease, CacheFailure> QueueObstacleChange(ObstacleChange change, std::stop_token cancellation = {}) noexcept;
    [[nodiscard]] std::expected<std::optional<BatchRequestLease>, CacheFailure> BeginNextBatch(std::stop_token cancellation = {}) noexcept;
    [[nodiscard]] std::expected<ObstacleStatus, CacheFailure> GetObstacleStatus(ObstacleKey obstacle) const noexcept;
    [[nodiscard]] std::expected<BatchRequestLease, CacheFailure> RetryBatch(const BatchRequestLease& request, std::stop_token cancellation = {}) noexcept;
    [[nodiscard]] std::expected<void, CacheFailure> CancelBatch(const BatchRequestLease& request) noexcept;
    // Explicit completed build documents, one for each affected tile; no executor or worker threads.
    [[nodiscard]] static std::expected<BatchCandidate, CacheFailure> PrepareBatch(const BatchRequestLease& request,
        std::span<const TileUpdate> replacements, std::stop_token cancellation = {}) noexcept;
    [[nodiscard]] std::expected<PublicationLease, CacheFailure> Publish(const BatchCandidate& candidate, std::stop_token cancellation = {}) noexcept;
    TileCache(ConstructionKey, std::unique_ptr<Impl> implementation) noexcept;
    ~TileCache();
    TileCache(const TileCache&) = delete;
    TileCache& operator=(const TileCache&) = delete;
private:
    [[nodiscard]] std::expected<BatchRequestLease, CacheFailure> MakeRequest(const PublicationLease& base,
        ObstacleChange change, std::stop_token cancellation) const;
    std::unique_ptr<Impl> m_impl;
};
}
