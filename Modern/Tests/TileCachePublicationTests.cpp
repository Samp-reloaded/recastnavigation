// Copyright (c) 2026 Samp-reloaded contributors.
// Altered implementation; distributed under the repository's zlib license.
#include <doctest/doctest.h>
import std;
import recastnav.document;
import recastnav.mesh;
import recastnav.codec.tile;
import recastnav.codec.layer;
import recastnav.tilecache;

namespace {
using namespace recastnav;
using namespace recastnav::tilecache;
struct ResourceStats final { std::size_t outstanding{}; };
class TestResource final : public std::pmr::memory_resource {
public:
    explicit TestResource(std::shared_ptr<ResourceStats> stats = std::make_shared<ResourceStats>()) : m_stats{std::move(stats)} {}
    void FailNextAllocation() noexcept { m_failNext = true; }
    [[nodiscard]] std::size_t GetOutstandingBytes() const noexcept { return m_stats->outstanding; }
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (std::exchange(m_failNext, false)) { throw std::bad_alloc{}; }
        void* memory{std::pmr::new_delete_resource()->allocate(bytes, alignment)};
        m_stats->outstanding += bytes; return memory;
    }
    void do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) override {
        m_stats->outstanding -= bytes; std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    std::shared_ptr<ResourceStats> m_stats;
    bool m_failNext{};
};
[[nodiscard]] TileUpdate MakeTile(std::int64_t x, std::uint32_t area = 63u, bool layer = false, float height = 0.0f, float ceiling = 2.0f) {
    const float minimum{static_cast<float>(x) * 4.0f};
    const TileMetadata metadata{{x, 0, 0}, {{minimum, 0, 0}, {minimum + 4.0f, ceiling, 4.0f}}};
    std::vector<Vec3> vertices{{minimum, height, 0}, {minimum, height, 4}, {minimum + 4, height, 4}, {minimum + 4, height, 0}};
    std::vector<Polygon> polygons{};
    polygons.emplace_back(std::vector<VertexIndex>{VertexIndex{0}, VertexIndex{1}, VertexIndex{2}, VertexIndex{3}},
        std::vector<EdgeConnection>{PortalSide::NegativeX, PortalSide::PositiveZ, PortalSide::PositiveX, PortalSide::NegativeZ}, area);
    auto tile{TileDocument::Create(metadata, std::move(vertices), std::move(polygons))}; REQUIRE(tile.has_value());
    if (!layer) { return TileUpdate{std::move(*tile)}; }
    auto cells{LayerDocument::Create(metadata, 1u, 1u, {0u, 0u, 0u, 0u}, 0u, 4u, std::vector<LayerCell>{LayerCell{0u, 63u}})};
    REQUIRE(cells.has_value()); return TileUpdate{std::move(*tile), std::move(*cells)};
}
[[nodiscard]] std::unique_ptr<TileCache> MakeStore(const std::shared_ptr<TestResource>& resource, CacheLimits limits = {}) {
    const std::array initial{MakeTile(1, 63u, true), MakeTile(0, 63u, true)};
    auto cache{TileCache::Create(initial, resource, limits)}; REQUIRE(cache.has_value()); return std::move(*cache);
}
[[nodiscard]] ObstacleChange Crossing(ObstacleKey key = ObstacleKey{1}) {
    return {key, Aabb{{3.5f, 0, 1}, {4.5f, 1.5f, 3}}};
}
[[nodiscard]] BatchRequestLease Begin(TileCache& cache) {
    auto next{cache.BeginNextBatch()}; REQUIRE(next.has_value()); REQUIRE(next->has_value()); return **next;
}
[[nodiscard]] std::uint32_t Area(const PublicationLease& snapshot, TileKey key) {
    auto location{snapshot->Locate(key)}; REQUIRE(location.has_value()); REQUIRE(location->has_value());
    auto tile{snapshot->GetTile(**location)}; REQUIRE(tile.has_value()); return tile->get().GetPolygons()[0].GetArea();
}
}

TEST_CASE("Affected tile candidates own every replacement and publish geometry and obstacles together") {
    auto resource{std::make_shared<TestResource>()}; auto cache{MakeStore(resource)}; const auto old{cache->AcquireSnapshot()};
    const auto location{old->Locate({0, 0, 0})}; REQUIRE(location.has_value()); REQUIRE(location->has_value());
    const auto polygon{old->GetMesh()->Locate({0, 0, 0}, PolygonIndex{0})}; REQUIRE(polygon.has_value()); REQUIRE(polygon->has_value());
    auto queued{cache->QueueObstacleChange(Crossing())}; REQUIRE(queued.has_value()); CHECK((*queued)->GetAffectedTiles().size() == 2u);
    auto dirty{cache->GetObstacleStatus(ObstacleKey{1})}; REQUIRE(dirty.has_value()); CHECK(dirty->IsGeometryDirty()); CHECK_FALSE(dirty->IsBuildPending());
    auto request{Begin(*cache)}; CHECK(request == *queued);
    auto pending{cache->GetObstacleStatus(ObstacleKey{1})}; REQUIRE(pending.has_value()); CHECK(pending->IsBuildPending());
    const std::array incomplete{MakeTile(0, 7u)}; auto missing{TileCache::PrepareBatch(request, incomplete)};
    REQUIRE_FALSE(missing.has_value()); CHECK(missing.error().GetError() == CacheError::MissingReplacement); CHECK(cache->AcquireSnapshot() == old);
    std::array replacements{MakeTile(1, 7u), MakeTile(0, 7u)};
    auto candidate{TileCache::PrepareBatch(request, replacements)}; REQUIRE(candidate.has_value());
    replacements[0] = MakeTile(1, 19u); CHECK(Area(candidate->GetSnapshot(), {1, 0, 0}) == 7u);
    CHECK(cache->AcquireSnapshot() == old); CHECK(old->GetObstacles().empty());
    auto published{cache->Publish(*candidate)}; REQUIRE(published.has_value()); CHECK(cache->AcquireSnapshot() == *published);
    CHECK(Area(*published, {0, 0, 0}) == 7u); CHECK(Area(*published, {1, 0, 0}) == 7u); CHECK((*published)->GetObstacles().size() == 1u);
    CHECK(Area(old, {0, 0, 0}) == 63u); CHECK(old->GetTile(**location).has_value());
    auto stale{(*published)->GetTile(**location)}; REQUIRE_FALSE(stale.has_value()); CHECK(stale.error().GetError() == CacheError::StaleSnapshot);
    auto stalePolygon{(*published)->GetMesh()->GetPolygon(**polygon)}; REQUIRE_FALSE(stalePolygon.has_value()); CHECK(stalePolygon.error() == MeshError::SnapshotMismatch);
    auto clean{cache->GetObstacleStatus(ObstacleKey{1})}; REQUIRE(clean.has_value()); CHECK_FALSE(clean->IsGeometryDirty());
    auto repeated{cache->Publish(*candidate)}; REQUIRE_FALSE(repeated.has_value()); CHECK(repeated.error().GetError() == CacheError::MissingBatch);
}
TEST_CASE("A stale batch publishes nothing and explicit retry rebases all affected geometry") {
    auto resource{std::make_shared<TestResource>()}; auto cache{MakeStore(resource)};
    auto first{cache->QueueObstacleChange(Crossing(ObstacleKey{1}))}; auto second{cache->QueueObstacleChange(Crossing(ObstacleKey{2}))};
    REQUIRE(first.has_value()); REQUIRE(second.has_value()); auto a{Begin(*cache)}, b{Begin(*cache)};
    const std::array replacements{MakeTile(0, 7u), MakeTile(1, 7u)};
    auto ca{TileCache::PrepareBatch(a, replacements)}, cb{TileCache::PrepareBatch(b, replacements)}; REQUIRE(ca.has_value()); REQUIRE(cb.has_value());
    auto published{cache->Publish(*ca)}; REQUIRE(published.has_value());
    auto stale{cache->Publish(*cb)}; REQUIRE_FALSE(stale.has_value()); CHECK(stale.error().GetError() == CacheError::StaleSnapshot);
    CHECK(cache->AcquireSnapshot() == *published); CHECK((*published)->GetObstacles().size() == 1u);
    auto pending{cache->GetObstacleStatus(ObstacleKey{2})}; REQUIRE(pending.has_value()); CHECK(pending->IsBuildPending()); CHECK(pending->IsGeometryDirty());
    auto retry{cache->RetryBatch(b)}; REQUIRE(retry.has_value()); CHECK((*retry)->GetBaseSnapshot() == *published); CHECK(*retry != b);
    auto begun{Begin(*cache)}; CHECK(begun == *retry);
    auto next{TileCache::PrepareBatch(begun, replacements)}; REQUIRE(next.has_value()); auto result{cache->Publish(*next)}; REQUIRE(result.has_value());
    CHECK((*result)->GetObstacles().size() == 2u);
}
TEST_CASE("Cancelled preparation and publication retain the complete old snapshot and pending status") {
    auto resource{std::make_shared<TestResource>()}; auto cache{MakeStore(resource)}; const auto old{cache->AcquireSnapshot()};
    auto queued{cache->QueueObstacleChange(Crossing())}; REQUIRE(queued.has_value()); auto request{Begin(*cache)};
    const std::array replacements{MakeTile(0, 7u), MakeTile(1, 7u)}; std::stop_source stopped{}; stopped.request_stop();
    auto cancelled{TileCache::PrepareBatch(request, replacements, stopped.get_token())}; REQUIRE_FALSE(cancelled.has_value());
    CHECK(cancelled.error().GetError() == CacheError::Cancelled); CHECK(cache->AcquireSnapshot() == old);
    auto candidate{TileCache::PrepareBatch(request, replacements)}; REQUIRE(candidate.has_value());
    auto denied{cache->Publish(*candidate, stopped.get_token())}; REQUIRE_FALSE(denied.has_value()); CHECK(denied.error().GetError() == CacheError::Cancelled);
    CHECK(cache->AcquireSnapshot() == old); auto status{cache->GetObstacleStatus(ObstacleKey{1})}; REQUIRE(status.has_value()); CHECK(status->IsBuildPending());
    CHECK(cache->CancelBatch(request).has_value()); auto absent{cache->GetObstacleStatus(ObstacleKey{1})}; REQUIRE_FALSE(absent.has_value());
    CHECK(absent.error().GetError() == CacheError::UnknownObstacle); CHECK(cache->AcquireSnapshot() == old);
}
TEST_CASE("Resource failure rolls back candidate storage and leaves the batch retryable") {
    auto resource{std::make_shared<TestResource>()}; auto cache{MakeStore(resource)};
    auto queued{cache->QueueObstacleChange(Crossing())}; REQUIRE(queued.has_value()); auto request{Begin(*cache)};
    const auto old{cache->AcquireSnapshot()}; const auto bytes{resource->GetOutstandingBytes()};
    const std::array replacements{MakeTile(0, 7u), MakeTile(1, 7u)}; resource->FailNextAllocation();
    auto failed{TileCache::PrepareBatch(request, replacements)}; REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().GetError() == CacheError::AllocationFailure); CHECK(cache->AcquireSnapshot() == old); CHECK(resource->GetOutstandingBytes() == bytes);
    auto candidate{TileCache::PrepareBatch(request, replacements)}; REQUIRE(candidate.has_value()); CHECK(cache->Publish(*candidate).has_value());
}
TEST_CASE("Bounded admission, duplicate keys and replacement metadata have explicit outcomes") {
    auto resource{std::make_shared<TestResource>()}; auto cache{MakeStore(resource, CacheLimits{65'536u, 65'536u, 1u})};
    auto queued{cache->QueueObstacleChange(Crossing())}; REQUIRE(queued.has_value());
    auto duplicate{cache->QueueObstacleChange(Crossing())}; REQUIRE_FALSE(duplicate.has_value()); CHECK(duplicate.error().GetError() == CacheError::ObstacleBusy);
    auto full{cache->QueueObstacleChange(Crossing(ObstacleKey{2}))}; REQUIRE_FALSE(full.has_value()); CHECK(full.error().GetError() == CacheError::CapacityExceeded);
    auto request{Begin(*cache)}; const std::array repeated{MakeTile(0), MakeTile(0)};
    auto bad{TileCache::PrepareBatch(request, repeated)}; REQUIRE_FALSE(bad.has_value()); CHECK(bad.error().GetError() == CacheError::DuplicateTile);
    const std::array wrong{MakeTile(0), MakeTile(9)}; auto unmatched{TileCache::PrepareBatch(request, wrong)};
    REQUIRE_FALSE(unmatched.has_value()); CHECK(unmatched.error().GetError() == CacheError::MissingReplacement);
    const std::array metadata{MakeTile(0, 7u, false, 0, 3.0f), MakeTile(1)};
    auto changedBounds{TileCache::PrepareBatch(request, metadata)}; REQUIRE_FALSE(changedBounds.has_value());
    CHECK(changedBounds.error().GetError() == CacheError::InvalidDocument);
    CHECK(cache->CancelBatch(request).has_value()); CHECK(cache->QueueObstacleChange(Crossing(ObstacleKey{2})).has_value());
}
TEST_CASE("Removing an obstacle dirties its former footprint and publishes removal with replacement geometry") {
    auto resource{std::make_shared<TestResource>()}; auto cache{MakeStore(resource)};
    auto queued{cache->QueueObstacleChange(Crossing())}; REQUIRE(queued.has_value()); auto request{Begin(*cache)};
    const std::array blocked{MakeTile(0, 7u), MakeTile(1, 7u)}; auto candidate{TileCache::PrepareBatch(request, blocked)};
    REQUIRE(candidate.has_value()); REQUIRE(cache->Publish(*candidate).has_value());
    auto removal{cache->QueueObstacleChange({ObstacleKey{1}, {}})}; REQUIRE(removal.has_value()); CHECK((*removal)->GetAffectedTiles().size() == 2u);
    auto removeRequest{Begin(*cache)}; const std::array restored{MakeTile(0), MakeTile(1)};
    auto removeCandidate{TileCache::PrepareBatch(removeRequest, restored)}; REQUIRE(removeCandidate.has_value());
    auto result{cache->Publish(*removeCandidate)}; REQUIRE(result.has_value()); CHECK((*result)->GetObstacles().empty()); CHECK(Area(*result, {0, 0, 0}) == 63u);
}
TEST_CASE("Snapshot and local locator ownership keep the PMR resource alive beyond the store") {
    const auto stats{std::make_shared<ResourceStats>()}; auto resource{std::make_shared<TestResource>(stats)};
    std::weak_ptr<TestResource> lifetime{resource}; auto cache{MakeStore(resource)}; auto snapshot{cache->AcquireSnapshot()};
    auto found{snapshot->Locate({0, 0, 0})}; REQUIRE(found.has_value()); REQUIRE(found->has_value());
    std::optional<TileLocation> location{**found}; found->reset(); resource.reset(); cache.reset(); snapshot.reset();
    CHECK_FALSE(lifetime.expired()); CHECK(location->GetSnapshot()->GetTile(*location).has_value());
    location.reset(); CHECK(lifetime.expired()); CHECK(stats->outstanding == 0u);
}
TEST_CASE("Cache document ingestion uses the existing versioned tile and layer codecs") {
    const auto source{MakeTile(0, 7u, true)};
    auto tile{codec::EncodeTile(source.GetTile())}; REQUIRE(tile.has_value());
    auto layer{codec::EncodeLayer(*source.GetLayer())}; REQUIRE(layer.has_value());
    auto decoded{DecodeTileUpdate(*tile, std::span<const std::byte>{*layer})}; REQUIRE(decoded.has_value());
    CHECK(decoded->GetTile() == source.GetTile()); REQUIRE(decoded->GetLayer().has_value()); CHECK(*decoded->GetLayer() == *source.GetLayer());
    (*tile)[0] = std::byte{0}; auto invalid{DecodeTileUpdate(*tile)}; REQUIRE_FALSE(invalid.has_value()); CHECK(invalid.error().GetError() == CacheError::InvalidDocument);
}
