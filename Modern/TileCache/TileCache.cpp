// Copyright (c) 2026 Samp-reloaded contributors.
// Altered implementation; distributed under the repository's zlib license.
module recastnav.tilecache;
import std;
import recastnav.codec.tile;
import recastnav.codec.layer;

namespace recastnav::tilecache {
namespace {
[[nodiscard]] bool LimitsValid(const CacheLimits& limits) noexcept {
    return limits.GetDocumentLimits().IsValid() && limits.GetMaximumTiles() > 0u
        && limits.GetMaximumObstacles() > 0u && limits.GetMaximumOutstandingBatches() > 0u
        && limits.GetMaximumAffectedTilesPerBatch() > 0u && limits.GetMaximumQueueBytes() >= sizeof(BatchRequest)
        && limits.GetMaximumSnapshotDocumentBytes() > 0u;
}
[[nodiscard]] bool BoundsValid(const Aabb& bounds) noexcept {
    return bounds.IsValid() && bounds.GetMinimum().GetX() < bounds.GetMaximum().GetX()
        && bounds.GetMinimum().GetZ() < bounds.GetMaximum().GetZ();
}
[[nodiscard]] std::optional<Aabb> AppliedBounds(const PublicationSnapshot& snapshot, ObstacleKey key) noexcept {
    const auto obstacles{snapshot.GetObstacles()};
    const auto found{std::lower_bound(obstacles.begin(), obstacles.end(), key,
        [](const Obstacle& obstacle, ObstacleKey value) { return obstacle.GetKey() < value; })};
    if (found == obstacles.end() || found->GetKey() != key) { return {}; }
    return found->GetBounds();
}
[[nodiscard]] bool Affected(const TileDocument& tile, const Aabb& obstacle) noexcept {
    const auto& bounds{tile.GetMetadata().GetBounds()};
    const double radius{tile.GetMetadata().GetProfile().GetAgentRadius()};
    // Conservatively dirty every vertical layer in the obstacle's expanded XZ footprint.
    return static_cast<double>(obstacle.GetMaximum().GetX()) >= static_cast<double>(bounds.GetMinimum().GetX()) - radius
        && static_cast<double>(obstacle.GetMinimum().GetX()) <= static_cast<double>(bounds.GetMaximum().GetX()) + radius
        && static_cast<double>(obstacle.GetMaximum().GetZ()) >= static_cast<double>(bounds.GetMinimum().GetZ()) - radius
        && static_cast<double>(obstacle.GetMinimum().GetZ()) <= static_cast<double>(bounds.GetMaximum().GetZ()) + radius;
}
[[nodiscard]] bool LayerMatches(const TileDocument& tile, const LayerDocument& layer) noexcept {
    const auto& first{tile.GetMetadata()}; const auto& second{layer.GetMetadata()};
    return first.GetKey() == second.GetKey() && first.GetBounds() == second.GetBounds() && first.GetProfile() == second.GetProfile();
}
[[nodiscard]] CacheFailure MeshFailure(MeshError error) noexcept {
    const auto kind{error == MeshError::AllocationFailure ? CacheError::AllocationFailure
        : error == MeshError::CapacityExceeded ? CacheError::CapacityExceeded : CacheError::InvalidDocument};
    return {kind, {}, {}, error};
}
[[nodiscard]] CacheFailure DocumentFailure(Failure failure, std::optional<TileKey> key = {}) noexcept {
    const auto kind{failure.GetError() == Error::AllocationFailure ? CacheError::AllocationFailure
        : failure.GetError() == Error::BudgetExceeded || failure.GetError() == Error::SizeOverflow
            ? CacheError::CapacityExceeded : CacheError::InvalidDocument};
    return {kind, key, failure};
}
}

std::expected<TileUpdate, CacheFailure> DecodeTileUpdate(std::span<const std::byte> tile,
    std::optional<std::span<const std::byte>> layer, DocumentLimits limits) noexcept {
    auto decoded{codec::DecodeTile(tile, limits)};
    if (!decoded) { return std::unexpected{DocumentFailure(decoded.error())}; }
    if (!layer) { return TileUpdate{std::move(*decoded)}; }
    auto decodedLayer{codec::DecodeLayer(*layer, limits)};
    if (!decodedLayer) { return std::unexpected{DocumentFailure(decodedLayer.error(), decoded->GetMetadata().GetKey())}; }
    if (!LayerMatches(*decoded, *decodedLayer)) { return std::unexpected{CacheFailure{CacheError::InvalidDocument, decoded->GetMetadata().GetKey()}}; }
    return TileUpdate{std::move(*decoded), std::move(*decodedLayer)};
}

std::expected<PublicationLease, CacheFailure> PublicationSnapshot::Create(std::span<const TileUpdate> tiles,
    std::span<const Obstacle> appliedObstacles, std::shared_ptr<std::pmr::memory_resource> resource,
    CacheLimits limits, std::stop_token cancellation) noexcept {
    if (!resource) { return std::unexpected{CacheFailure{CacheError::MissingResource}}; }
    if (!LimitsValid(limits)) { return std::unexpected{CacheFailure{CacheError::InvalidLimits}}; }
    if (tiles.size() > limits.GetMaximumTiles() || appliedObstacles.size() > limits.GetMaximumObstacles()) {
        return std::unexpected{CacheFailure{CacheError::CapacityExceeded}};
    }
    try {
        if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
        std::pmr::vector<std::size_t> order{resource.get()}; order.resize(tiles.size());
        std::iota(order.begin(), order.end(), std::size_t{});
        std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return tiles[a].GetTile().GetMetadata().GetKey() < tiles[b].GetTile().GetMetadata().GetKey(); });
        std::vector<TileLease> documents{}; documents.reserve(tiles.size());
        std::pmr::vector<LayerLease> layers{resource.get()}; layers.reserve(tiles.size());
        std::size_t documentBytes{};
        for (std::size_t i{}; i < order.size(); ++i) {
            if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
            const auto& update{tiles[order[i]]}; const auto& tile{update.GetTile()}; const auto key{tile.GetMetadata().GetKey()};
            if (i > 0u && tiles[order[i - 1u]].GetTile().GetMetadata().GetKey() == key) {
                return std::unexpected{CacheFailure{CacheError::DuplicateTile, key}};
            }
            const auto encoded{codec::EncodeTile(tile, limits.GetDocumentLimits())};
            if (!encoded) { return std::unexpected{DocumentFailure(encoded.error(), key)}; }
            if (encoded->size() > limits.GetMaximumSnapshotDocumentBytes() - documentBytes) {
                return std::unexpected{CacheFailure{CacheError::CapacityExceeded, key}};
            }
            documentBytes += encoded->size();
            if (const auto& layer{update.GetLayer()}; layer) {
                if (!LayerMatches(tile, *layer)) { return std::unexpected{CacheFailure{CacheError::InvalidDocument, key}}; }
                const auto encodedLayer{codec::EncodeLayer(*layer, limits.GetDocumentLimits())};
                if (!encodedLayer) { return std::unexpected{DocumentFailure(encodedLayer.error(), key)}; }
                if (encodedLayer->size() > limits.GetMaximumSnapshotDocumentBytes() - documentBytes) {
                    return std::unexpected{CacheFailure{CacheError::CapacityExceeded, key}};
                }
                documentBytes += encodedLayer->size();
                layers.push_back(std::make_shared<const LayerDocument>(*layer));
            } else { layers.emplace_back(); }
            documents.push_back(std::make_shared<const TileDocument>(tile));
        }
        std::pmr::vector<Obstacle> obstacles{appliedObstacles.begin(), appliedObstacles.end(), resource.get()};
        std::ranges::sort(obstacles, {}, &Obstacle::GetKey);
        for (std::size_t i{}; i < obstacles.size(); ++i) {
            if (!BoundsValid(obstacles[i].GetBounds()) || (i > 0u && obstacles[i - 1u].GetKey() == obstacles[i].GetKey())) {
                return std::unexpected{CacheFailure{CacheError::InvalidDocument}};
            }
        }
        if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
        auto mesh{MeshSnapshot::Create(std::move(documents), limits.GetMeshLimits())};
        if (!mesh) { return std::unexpected{MeshFailure(mesh.error())}; }
        if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
        return std::make_shared<PublicationSnapshot>(ConstructionKey{}, std::move(resource), std::move(*mesh), std::move(layers), std::move(obstacles));
    } catch (const std::bad_alloc&) { return std::unexpected{CacheFailure{CacheError::AllocationFailure}}; }
      catch (const std::length_error&) { return std::unexpected{CacheFailure{CacheError::CapacityExceeded}}; }
}

const TileKey& TileLocation::GetTileKey() const noexcept { return m_snapshot->m_mesh->GetTiles()[m_index]->GetMetadata().GetKey(); }
std::expected<std::optional<TileLocation>, CacheFailure> PublicationSnapshot::Locate(const TileKey& key) const noexcept {
    const auto tiles{m_mesh->GetTiles()};
    const auto found{std::lower_bound(tiles.begin(), tiles.end(), key,
        [](const TileLease& tile, const TileKey& value) { return tile->GetMetadata().GetKey() < value; })};
    if (found == tiles.end() || (*found)->GetMetadata().GetKey() != key) { return std::optional<TileLocation>{}; }
    return std::optional<TileLocation>{TileLocation{shared_from_this(), static_cast<std::size_t>(found - tiles.begin())}};
}
std::expected<std::reference_wrapper<const TileDocument>, CacheFailure> PublicationSnapshot::GetTile(const TileLocation& location) const noexcept {
    if (location.m_snapshot.get() != this) { return std::unexpected{CacheFailure{CacheError::StaleSnapshot}}; }
    if (location.m_index >= m_mesh->GetTiles().size()) { return std::unexpected{CacheFailure{CacheError::InvalidLocator}}; }
    return std::cref(*m_mesh->GetTiles()[location.m_index]);
}
std::expected<std::optional<std::reference_wrapper<const LayerDocument>>, CacheFailure> PublicationSnapshot::GetLayer(const TileLocation& location) const noexcept {
    if (location.m_snapshot.get() != this) { return std::unexpected{CacheFailure{CacheError::StaleSnapshot}}; }
    if (location.m_index >= m_layers.size()) { return std::unexpected{CacheFailure{CacheError::InvalidLocator}}; }
    if (!m_layers[location.m_index]) { return std::optional<std::reference_wrapper<const LayerDocument>>{}; }
    return std::optional{std::cref(*m_layers[location.m_index])};
}

struct TileCache::Impl final {
    struct Job final { BatchRequestLease request; bool pending{}; std::size_t bytes{}; };
    Impl(std::shared_ptr<std::pmr::memory_resource> resource, CacheLimits limits, PublicationLease snapshot)
        : resource{std::move(resource)}, limits{limits}, current{std::move(snapshot)}, jobs{this->resource.get()} {}
    std::shared_ptr<std::pmr::memory_resource> resource;
    CacheLimits limits;
    mutable std::mutex mutex;
    std::atomic<PublicationLease> current;
    std::pmr::vector<Job> jobs;
    std::size_t queueBytes{};
};
TileCache::TileCache(ConstructionKey, std::unique_ptr<Impl> implementation) noexcept : m_impl{std::move(implementation)} {}
TileCache::~TileCache() = default;
std::expected<std::unique_ptr<TileCache>, CacheFailure> TileCache::Create(std::span<const TileUpdate> initialTiles,
    std::shared_ptr<std::pmr::memory_resource> resource, CacheLimits limits, std::stop_token cancellation) noexcept {
    auto snapshot{PublicationSnapshot::Create(initialTiles, {}, resource, limits, cancellation)};
    if (!snapshot) { return std::unexpected{snapshot.error()}; }
    try {
        auto implementation{std::make_unique<Impl>(std::move(resource), limits, std::move(*snapshot))};
        return std::make_unique<TileCache>(ConstructionKey{}, std::move(implementation));
    } catch (const std::bad_alloc&) { return std::unexpected{CacheFailure{CacheError::AllocationFailure}}; }
}
PublicationLease TileCache::AcquireSnapshot() const noexcept { return m_impl->current.load(std::memory_order_acquire); }

std::expected<BatchRequestLease, CacheFailure> TileCache::MakeRequest(const PublicationLease& base,
    ObstacleChange change, std::stop_token cancellation) const {
    if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
    if (change.GetRequestedBounds() && !BoundsValid(*change.GetRequestedBounds())) {
        return std::unexpected{CacheFailure{CacheError::InvalidDocument}};
    }
    const auto applied{AppliedBounds(*base, change.GetKey())};
    if (!applied && !change.GetRequestedBounds()) { return std::unexpected{CacheFailure{CacheError::UnknownObstacle}}; }
    std::size_t count{};
    for (const auto& tile : base->GetMesh()->GetTiles()) {
        if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
        if ((applied && Affected(*tile, *applied)) || (change.GetRequestedBounds() && Affected(*tile, *change.GetRequestedBounds()))) {
            if (count >= m_impl->limits.GetMaximumAffectedTilesPerBatch()) {
                return std::unexpected{CacheFailure{CacheError::CapacityExceeded}};
            }
            ++count;
        }
    }
    const auto keyBytes{CheckedProduct(count, sizeof(TileKey), m_impl->limits.GetMaximumQueueBytes())};
    if (!keyBytes || *keyBytes > m_impl->limits.GetMaximumQueueBytes() - sizeof(BatchRequest)) {
        return std::unexpected{CacheFailure{CacheError::CapacityExceeded}};
    }
    std::pmr::vector<TileKey> affected{m_impl->resource.get()}; affected.reserve(count);
    for (const auto& tile : base->GetMesh()->GetTiles()) {
        if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
        if ((applied && Affected(*tile, *applied)) || (change.GetRequestedBounds() && Affected(*tile, *change.GetRequestedBounds()))) {
            affected.push_back(tile->GetMetadata().GetKey());
        }
    }
    return std::make_shared<BatchRequest>(BatchRequest::ConstructionKey{}, base, change, m_impl->limits, std::move(affected));
}

std::expected<BatchRequestLease, CacheFailure> TileCache::QueueObstacleChange(ObstacleChange change, std::stop_token cancellation) noexcept {
    try {
        const std::scoped_lock lock{m_impl->mutex};
        if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
        if (std::ranges::any_of(m_impl->jobs, [&](const auto& job) { return job.request->GetObstacleChange().GetKey() == change.GetKey(); })) {
            return std::unexpected{CacheFailure{CacheError::ObstacleBusy}};
        }
        if (m_impl->jobs.size() >= m_impl->limits.GetMaximumOutstandingBatches()) { return std::unexpected{CacheFailure{CacheError::CapacityExceeded}}; }
        const auto base{AcquireSnapshot()};
        if (change.GetRequestedBounds() && !AppliedBounds(*base, change.GetKey())) {
            std::size_t admitted{base->GetObstacles().size()};
            for (const auto& job : m_impl->jobs) {
                const auto& queued{job.request->GetObstacleChange()};
                if (queued.GetRequestedBounds() && !AppliedBounds(*base, queued.GetKey())) { ++admitted; }
            }
            if (admitted >= m_impl->limits.GetMaximumObstacles()) { return std::unexpected{CacheFailure{CacheError::CapacityExceeded}}; }
        }
        auto request{MakeRequest(base, change, cancellation)};
        if (!request) { return std::unexpected{request.error()}; }
        const auto bytes{CheckedProduct((*request)->GetAffectedTiles().size(), sizeof(TileKey), m_impl->limits.GetMaximumQueueBytes())};
        if (!bytes || *bytes > std::numeric_limits<std::size_t>::max() - sizeof(BatchRequest) - sizeof(Impl::Job)) {
            return std::unexpected{CacheFailure{CacheError::CapacityExceeded}};
        }
        const auto charge{*bytes + sizeof(BatchRequest) + sizeof(Impl::Job)};
        if (charge > m_impl->limits.GetMaximumQueueBytes() - m_impl->queueBytes) { return std::unexpected{CacheFailure{CacheError::CapacityExceeded}}; }
        if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
        m_impl->jobs.push_back(Impl::Job{*request, false, charge});
        m_impl->queueBytes += charge;
        return std::move(*request);
    } catch (const std::bad_alloc&) { return std::unexpected{CacheFailure{CacheError::AllocationFailure}}; }
      catch (const std::length_error&) { return std::unexpected{CacheFailure{CacheError::CapacityExceeded}}; }
}
std::expected<std::optional<BatchRequestLease>, CacheFailure> TileCache::BeginNextBatch(std::stop_token cancellation) noexcept {
    const std::scoped_lock lock{m_impl->mutex};
    if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
    const auto next{std::ranges::find_if(m_impl->jobs, [](const auto& job) { return !job.pending; })};
    if (next == m_impl->jobs.end()) { return std::optional<BatchRequestLease>{}; }
    next->pending = true;
    return std::optional{next->request};
}
std::expected<ObstacleStatus, CacheFailure> TileCache::GetObstacleStatus(ObstacleKey obstacle) const noexcept {
    const std::scoped_lock lock{m_impl->mutex};
    const auto applied{AppliedBounds(*AcquireSnapshot(), obstacle)};
    const auto job{std::ranges::find_if(m_impl->jobs, [&](const auto& value) { return value.request->GetObstacleChange().GetKey() == obstacle; })};
    if (job != m_impl->jobs.end()) {
        return ObstacleStatus{job->pending ? ObstaclePhase::Pending : ObstaclePhase::Dirty, applied, job->request->GetObstacleChange().GetRequestedBounds()};
    }
    if (!applied) { return std::unexpected{CacheFailure{CacheError::UnknownObstacle}}; }
    return ObstacleStatus{ObstaclePhase::Clean, applied, applied};
}
std::expected<BatchRequestLease, CacheFailure> TileCache::RetryBatch(const BatchRequestLease& request, std::stop_token cancellation) noexcept {
    try {
        const std::scoped_lock lock{m_impl->mutex};
        const auto job{std::ranges::find_if(m_impl->jobs, [&](const auto& value) { return value.request == request; })};
        if (job == m_impl->jobs.end()) { return std::unexpected{CacheFailure{CacheError::MissingBatch}}; }
        auto retry{MakeRequest(AcquireSnapshot(), request->GetObstacleChange(), cancellation)};
        if (!retry) { return std::unexpected{retry.error()}; }
        const auto bytes{CheckedProduct((*retry)->GetAffectedTiles().size(), sizeof(TileKey), m_impl->limits.GetMaximumQueueBytes())};
        if (!bytes || *bytes > std::numeric_limits<std::size_t>::max() - sizeof(BatchRequest) - sizeof(Impl::Job)) {
            return std::unexpected{CacheFailure{CacheError::CapacityExceeded}};
        }
        const auto charge{*bytes + sizeof(BatchRequest) + sizeof(Impl::Job)};
        if (charge > m_impl->limits.GetMaximumQueueBytes() - (m_impl->queueBytes - job->bytes)) {
            return std::unexpected{CacheFailure{CacheError::CapacityExceeded}};
        }
        if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
        m_impl->queueBytes = m_impl->queueBytes - job->bytes + charge;
        *job = Impl::Job{*retry, false, charge};
        return std::move(*retry);
    } catch (const std::bad_alloc&) { return std::unexpected{CacheFailure{CacheError::AllocationFailure}}; }
      catch (const std::length_error&) { return std::unexpected{CacheFailure{CacheError::CapacityExceeded}}; }
}
std::expected<void, CacheFailure> TileCache::CancelBatch(const BatchRequestLease& request) noexcept {
    const std::scoped_lock lock{m_impl->mutex};
    const auto job{std::ranges::find_if(m_impl->jobs, [&](const auto& value) { return value.request == request; })};
    if (job == m_impl->jobs.end()) { return std::unexpected{CacheFailure{CacheError::MissingBatch}}; }
    m_impl->queueBytes -= job->bytes; m_impl->jobs.erase(job);
    return {};
}

std::expected<BatchCandidate, CacheFailure> TileCache::PrepareBatch(const BatchRequestLease& request,
    std::span<const TileUpdate> replacements, std::stop_token cancellation) noexcept {
    if (!request) { return std::unexpected{CacheFailure{CacheError::MissingBatch}}; }
    if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
    if (replacements.size() < request->GetAffectedTiles().size()) { return std::unexpected{CacheFailure{CacheError::MissingReplacement}}; }
    if (replacements.size() > request->GetAffectedTiles().size()) { return std::unexpected{CacheFailure{CacheError::InvalidBatch}}; }
    try {
        const auto& base{request->GetBaseSnapshot()};
        std::pmr::vector<std::size_t> order{base->m_resource.get()}; order.resize(replacements.size());
        std::iota(order.begin(), order.end(), std::size_t{});
        std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return replacements[a].GetTile().GetMetadata().GetKey() < replacements[b].GetTile().GetMetadata().GetKey(); });
        for (std::size_t i{}; i < order.size(); ++i) {
            const auto& tile{replacements[order[i]].GetTile()}; const auto key{tile.GetMetadata().GetKey()};
            if (i > 0u && replacements[order[i - 1u]].GetTile().GetMetadata().GetKey() == key) {
                return std::unexpected{CacheFailure{CacheError::DuplicateTile, key}};
            }
            if (key != request->GetAffectedTiles()[i]) { return std::unexpected{CacheFailure{CacheError::MissingReplacement, request->GetAffectedTiles()[i]}}; }
            const auto location{base->Locate(key)};
            if (!location || !*location) { return std::unexpected{CacheFailure{CacheError::InvalidBatch, key}}; }
            const auto original{base->GetTile(**location)};
            if (!original || tile.GetMetadata().GetBounds() != original->get().GetMetadata().GetBounds()
                || tile.GetMetadata().GetProfile() != original->get().GetMetadata().GetProfile()) {
                return std::unexpected{CacheFailure{CacheError::InvalidDocument, key}};
            }
        }
        std::pmr::vector<TileUpdate> documents{base->m_resource.get()}; documents.reserve(base->GetMesh()->GetTiles().size());
        std::size_t replacement{};
        for (std::size_t i{}; i < base->GetMesh()->GetTiles().size(); ++i) {
            if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
            const auto& tile{base->GetMesh()->GetTiles()[i]};
            if (replacement < order.size() && replacements[order[replacement]].GetTile().GetMetadata().GetKey() == tile->GetMetadata().GetKey()) {
                documents.push_back(replacements[order[replacement]]); ++replacement;
            } else {
                std::optional<LayerDocument> layer{};
                if (base->m_layers[i]) { layer = *base->m_layers[i]; }
                documents.emplace_back(*tile, std::move(layer));
            }
        }
        std::pmr::vector<Obstacle> obstacles{base->GetObstacles().begin(), base->GetObstacles().end(), base->m_resource.get()};
        const auto& change{request->GetObstacleChange()};
        const auto existing{std::ranges::find(obstacles, change.GetKey(), &Obstacle::GetKey)};
        if (existing != obstacles.end()) { obstacles.erase(existing); }
        if (change.GetRequestedBounds()) { obstacles.emplace_back(change.GetKey(), *change.GetRequestedBounds()); }
        auto snapshot{PublicationSnapshot::Create(documents, obstacles, base->m_resource, request->GetLimits(), cancellation)};
        if (!snapshot) { return std::unexpected{snapshot.error()}; }
        return BatchCandidate{request, std::move(*snapshot)};
    } catch (const std::bad_alloc&) { return std::unexpected{CacheFailure{CacheError::AllocationFailure}}; }
      catch (const std::length_error&) { return std::unexpected{CacheFailure{CacheError::CapacityExceeded}}; }
}
std::expected<PublicationLease, CacheFailure> TileCache::Publish(const BatchCandidate& candidate, std::stop_token cancellation) noexcept {
    const std::scoped_lock lock{m_impl->mutex};
    if (cancellation.stop_requested()) { return std::unexpected{CacheFailure{CacheError::Cancelled}}; }
    const auto job{std::ranges::find_if(m_impl->jobs, [&](const auto& value) { return value.request == candidate.GetRequest(); })};
    if (job == m_impl->jobs.end()) { return std::unexpected{CacheFailure{CacheError::MissingBatch}}; }
    if (!job->pending) { return std::unexpected{CacheFailure{CacheError::BatchNotBegun}}; }
    if (!candidate.GetSnapshot()) { return std::unexpected{CacheFailure{CacheError::InvalidBatch}}; }
    auto expected{job->request->GetBaseSnapshot()};
    if (!m_impl->current.compare_exchange_strong(expected, candidate.GetSnapshot(), std::memory_order_acq_rel, std::memory_order_acquire)) {
        return std::unexpected{CacheFailure{CacheError::StaleSnapshot}};
    }
    m_impl->queueBytes -= job->bytes; m_impl->jobs.erase(job);
    return candidate.GetSnapshot();
}
}
