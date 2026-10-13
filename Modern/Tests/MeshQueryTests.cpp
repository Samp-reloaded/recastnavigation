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
#include <doctest/doctest.h>
import std;
import recastnav.mesh;
import recastnav.query;

namespace {
using namespace recastnav;
[[nodiscard]] TileLease Square(TileKey key, float x, float z, std::vector<EdgeConnection> edges = std::vector<EdgeConnection>(4u),
    std::vector<OffMeshConnection> links = {}, std::uint32_t area = 63u,
    std::vector<std::uint32_t> categories = {}, bool enabled = true) {
    std::vector<Vec3> vertices{{x, 0, z + 1}, {x + 1, 0, z + 1}, {x + 1, 0, z}, {x, 0, z}};
    std::vector<Polygon> polygons{};
    polygons.emplace_back(std::vector<VertexIndex>{VertexIndex{0}, VertexIndex{1}, VertexIndex{2}, VertexIndex{3}},
        std::move(edges), area, std::move(categories), enabled);
    auto tile{TileDocument::Create(TileMetadata{key, Aabb{{x, -1, z}, {x + 1, 2, z + 1}}},
        std::move(vertices), std::move(polygons), {}, std::move(links))};
    REQUIRE(tile.has_value()); return std::make_shared<const TileDocument>(std::move(*tile));
}
[[nodiscard]] std::vector<EdgeConnection> AllPortals() {
    return {PortalSide::PositiveZ, PortalSide::PositiveX, PortalSide::NegativeZ, PortalSide::NegativeX};
}
[[nodiscard]] SnapshotLease Row() {
    auto snapshot{MeshSnapshot::Create({Square({2, 0}, 2, 0, AllPortals()), Square({0, 0}, 0, 0, AllPortals()),
        Square({1, 0}, 1, 0, AllPortals())})};
    REQUIRE(snapshot.has_value()); return *snapshot;
}
[[nodiscard]] PolygonLocation Location(const SnapshotLease& snapshot, TileKey key, std::size_t polygon = 0u) {
    auto location{snapshot->Locate(key, PolygonIndex{polygon})};
    REQUIRE(location.has_value()); REQUIRE(location->has_value()); return **location;
}
[[nodiscard]] std::unique_ptr<QueryWorkspace> Workspace(QueryLimits limits = {}) {
    auto workspace{QueryWorkspace::Create(limits)}; REQUIRE(workspace.has_value()); return std::move(*workspace);
}
class ControlledResource final : public std::pmr::memory_resource {
public:
    void SetFailing(bool failing) noexcept { m_failing = failing; }
    void SetCallback(std::function<void()> callback) { m_callback = std::move(callback); }
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (m_failing) { throw std::bad_alloc{}; }
        if (m_callback) { auto callback{std::move(m_callback)}; m_callback = {}; callback(); }
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    bool m_failing{};
    std::function<void()> m_callback{};
};
}

TEST_CASE("Snapshot admission canonicalizes tile keys, retains leases and checks local polygon authority") {
    const auto snapshot{Row()}; CHECK(snapshot->GetPolygonCount() == 3u); CHECK(snapshot->GetArcCount() == 4u);
    CHECK(snapshot->GetTiles()[0]->GetMetadata().GetKey() == TileKey{0, 0});
    const auto first{Location(snapshot, {0, 0})};
    auto moved{std::move(first)}; CHECK(first.GetTileKey() == moved.GetTileKey());
    auto missing{snapshot->Locate({17, 3}, PolygonIndex{0})}; REQUIRE(missing.has_value()); CHECK_FALSE(missing->has_value());
    auto invalid{snapshot->Locate({0, 0}, PolygonIndex{1})}; REQUIRE_FALSE(invalid.has_value()); CHECK(invalid.error() == MeshError::InvalidPolygon);
    const auto other{Row()}; auto stale{other->GetPolygon(first)};
    REQUIRE_FALSE(stale.has_value()); CHECK(stale.error() == MeshError::SnapshotMismatch);
    auto duplicate{MeshSnapshot::Create({snapshot->GetTiles()[0], snapshot->GetTiles()[0]})};
    REQUIRE_FALSE(duplicate.has_value()); CHECK(duplicate.error() == MeshError::DuplicateTile);
    auto nullTile{MeshSnapshot::Create({TileLease{}})};
    REQUIRE_FALSE(nullTile.has_value()); CHECK(nullTile.error() == MeshError::MissingTileLease);
    auto limited{MeshSnapshot::Create({snapshot->GetTiles()[0]}, MeshLimits{1, 0})}; CHECK_FALSE(limited.has_value());
    const auto empty{MeshSnapshot::Create({})}; REQUIRE(empty.has_value()); CHECK((*empty)->GetPolygonCount() == 0u);
}
TEST_CASE("Nearest polygon projects to actual surface, rejects invalid geometry and represents misses with optional") {
    const auto snapshot{Row()}; auto workspace{Workspace()};
    auto nearest{workspace->FindNearestPolygon(snapshot, {0.25f, 0.75f, 0.25f}, {1, 1, 1})};
    REQUIRE(nearest.has_value()); REQUIRE(nearest->has_value());
    CHECK((*nearest)->GetPosition() == Vec3{0.25f, 0, 0.25f}); CHECK((*nearest)->GetSquaredDistance() == doctest::Approx(0.5625));
    CHECK((*nearest)->GetLocation().GetTileKey() == TileKey{0, 0});
    auto edge{workspace->FindNearestPolygon(snapshot, {-0.25f, 0, 0.25f}, {0.5f, 1, 1})};
    REQUIRE(edge.has_value()); REQUIRE(edge->has_value()); CHECK((*edge)->GetPosition() == Vec3{0, 0, 0.25f});
    auto miss{workspace->FindNearestPolygon(snapshot, {100, 0, 100}, {1, 1, 1})};
    REQUIRE(miss.has_value()); CHECK_FALSE(miss->has_value());
    auto invalid{workspace->FindNearestPolygon(snapshot, {0, 0, 0}, {-1, 1, 1})};
    REQUIRE_FALSE(invalid.has_value()); CHECK(invalid.error() == QueryError::InvalidGeometry);
    auto nonfinite{workspace->FindNearestPolygon(snapshot, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, {1, 1, 1})};
    CHECK_FALSE(nonfinite.has_value());
    auto noSnapshot{workspace->FindNearestPolygon({}, {}, {1, 1, 1})};
    REQUIRE_FALSE(noSnapshot.has_value()); CHECK(noSnapshot.error() == QueryError::MissingSnapshotLease);
    const auto empty{MeshSnapshot::Create({})}; REQUIRE(empty.has_value());
    auto emptyHit{workspace->FindNearestPolygon(*empty, {}, {1, 1, 1})}; REQUIRE(emptyHit.has_value()); CHECK_FALSE(emptyHit->has_value());
}
TEST_CASE("Nearest polygon uses detail height rather than the coarse surface") {
    std::vector<Vec3> vertices{{0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}};
    std::vector<Polygon> polygons{Polygon{{VertexIndex{0}, VertexIndex{1}, VertexIndex{2}, VertexIndex{3}}, std::vector<EdgeConnection>(4u)}};
    std::vector<DetailTriangle> triangles{};
    for (std::size_t edge{}; edge < 4u; ++edge) {
        triangles.emplace_back(std::array{DetailVertexIndex{edge}, DetailVertexIndex{(edge + 1u) % 4u}, DetailVertexIndex{4}},
            std::array{true, false, false});
    }
    std::vector<DetailMesh> details{}; details.emplace_back(std::vector<Vec3>{{0.5f, 0.5f, 0.5f}}, std::move(triangles));
    auto tile{TileDocument::Create(TileMetadata{{0, 0}, {{0, -1, 0}, {1, 1, 1}}}, std::move(vertices), std::move(polygons), std::move(details))};
    REQUIRE(tile.has_value()); auto snapshot{MeshSnapshot::Create({std::make_shared<const TileDocument>(std::move(*tile))})};
    REQUIRE(snapshot.has_value()); auto workspace{Workspace()};
    auto nearest{workspace->FindNearestPolygon(*snapshot, {0.5f, 0.6f, 0.5f}, {1, 1, 1})};
    REQUIRE(nearest.has_value()); REQUIRE(nearest->has_value()); CHECK((*nearest)->GetPosition() == Vec3{0.5f, 0.5f, 0.5f});
}
TEST_CASE("Portal corridor is deterministic, includes graph transitions and agrees between A star and Dijkstra") {
    const auto snapshot{Row()}; auto workspace{Workspace()}; const auto start{Location(snapshot, {0, 0})}, goal{Location(snapshot, {2, 0})};
    auto path{workspace->FindPath(start, goal)}; REQUIRE(path.has_value()); CHECK(path->GetStatus() == PathStatus::Complete);
    CHECK(path->GetTermination() == PathTermination::ReachedGoal); REQUIRE(path->GetPolygons().size() == 3u);
    CHECK(path->GetPolygons()[1].GetTileKey() == TileKey{1, 0}); REQUIRE(path->GetTransitions().size() == 2u);
    CHECK(path->GetTransitions()[0].GetDeparture() == Vec3{1, 0, 0.5f}); CHECK(path->GetCost() == doctest::Approx(2.0));
    auto dijkstra{workspace->FindPath(start, goal, {}, SearchAlgorithm::Dijkstra)}; REQUIRE(dijkstra.has_value());
    CHECK(dijkstra->GetCost() == path->GetCost()); CHECK(std::ranges::equal(dijkstra->GetPolygons(), path->GetPolygons()));
    auto same{workspace->FindPath(start, start)}; REQUIRE(same.has_value()); CHECK(same->GetStatus() == PathStatus::Complete);
    CHECK(same->GetPolygons().size() == 1u); CHECK(same->GetTransitions().empty()); CHECK(same->GetCost() == 0.0);
    const auto other{Row()}; auto mismatch{workspace->FindPath(start, Location(other, {2, 0}))};
    REQUIRE_FALSE(mismatch.has_value()); CHECK(mismatch.error() == QueryError::SnapshotMismatch);
}
TEST_CASE("Area costs select the cheaper real route and equal costs break ties by canonical tile order") {
    auto snapshot{MeshSnapshot::Create({Square({1, 1}, 1, 1, AllPortals()), Square({1, 0}, 1, 0, AllPortals()),
        Square({0, 1}, 0, 1, AllPortals(), {}, 1u), Square({0, 0}, 0, 0, AllPortals())})};
    REQUIRE(snapshot.has_value()); auto workspace{Workspace()}; const auto start{Location(*snapshot, {0, 0})}, goal{Location(*snapshot, {1, 1})};
    auto equal{workspace->FindPath(start, goal)}; REQUIRE(equal.has_value()); REQUIRE(equal->GetPolygons().size() == 3u);
    CHECK(equal->GetPolygons()[1].GetTileKey() == TileKey{0, 1});
    QueryFilter filter{}; REQUIRE(filter.SetAreaCost(1, 9.0).has_value());
    auto weighted{workspace->FindPath(start, goal, filter)}; auto dijkstra{workspace->FindPath(start, goal, filter, SearchAlgorithm::Dijkstra)};
    REQUIRE(weighted.has_value()); REQUIRE(dijkstra.has_value()); REQUIRE(weighted->GetPolygons().size() == 3u);
    CHECK(weighted->GetPolygons()[1].GetTileKey() == TileKey{1, 0}); CHECK(weighted->GetCost() == doctest::Approx(2.0));
    CHECK(weighted->GetCost() == dijkstra->GetCost());
    CHECK_FALSE(filter.SetAreaCost(64u, 1.0).has_value()); CHECK_FALSE(filter.SetAreaCost(0u, 0.0).has_value());
    CHECK_FALSE(filter.SetAreaCost(0u, std::numeric_limits<double>::infinity()).has_value());
}
TEST_CASE("Off-mesh arcs preserve direction, user IDs and decrease an already queued indexed heap entry") {
    std::vector<OffMeshConnection> startLinks{}, middleLinks{}, goalLinks{};
    startLinks.emplace_back(Vec3{0.5f, 0, 0.5f}, Vec3{10.5f, 0, 0.5f}, 0.0f, 99u, 1u);
    startLinks.emplace_back(Vec3{0.5f, 0, 0.5f}, Vec3{0.5f, 0, 10.5f}, 0.0f, 100u);
    middleLinks.emplace_back(Vec3{0.5f, 0, 10.5f}, Vec3{10.5f, 0, 0.5f}, 0.0f, 101u);
    goalLinks.emplace_back(Vec3{10.5f, 0, 0.5f}, Vec3{20.5f, 0, 0.5f}, 0.0f, std::numeric_limits<std::uint64_t>::max());
    auto snapshot{MeshSnapshot::Create({Square({0, 0}, 0, 0, std::vector<EdgeConnection>(4u), std::move(startLinks)),
        Square({1, 0}, 10, 0, std::vector<EdgeConnection>(4u), std::move(goalLinks)),
        Square({2, 0}, 0, 10, std::vector<EdgeConnection>(4u), std::move(middleLinks)), Square({3, 0}, 20, 0)})};
    REQUIRE(snapshot.has_value()); auto workspace{Workspace()}; QueryFilter filter{}; REQUIRE(filter.SetAreaCost(1u, 10.0).has_value());
    const auto start{Location(*snapshot, {0, 0})}, goal{Location(*snapshot, {3, 0})};
    auto path{workspace->FindPath(start, goal, filter, SearchAlgorithm::Dijkstra)}; REQUIRE(path.has_value());
    CHECK(path->GetStatus() == PathStatus::Complete); REQUIRE(path->GetPolygons().size() == 4u);
    CHECK(path->GetPolygons()[1].GetTileKey() == TileKey{2, 0}); CHECK(path->GetCost() == doctest::Approx(20.0 + std::sqrt(200.0)));
    REQUIRE(path->GetTransitions().size() == 3u); CHECK(path->GetTransitions()[0].GetOffMeshUserId() == std::optional<std::uint64_t>{100u});
    CHECK(path->GetTransitions()[2].GetOffMeshUserId() == std::optional<std::uint64_t>{std::numeric_limits<std::uint64_t>::max()});
    auto reverse{workspace->FindPath(goal, start)}; REQUIRE(reverse.has_value()); CHECK(reverse->GetTermination() == PathTermination::Unreachable);
}
TEST_CASE("Off-mesh connection filters and bidirectional traversal are independent of surface categories") {
    std::vector<OffMeshConnection> links{};
    links.emplace_back(Vec3{0.5f, 0, 0.5f}, Vec3{3.5f, 0, 0.5f}, 0.0f, 3u, 4u, std::vector<std::uint32_t>{1'000'000u}, true);
    auto snapshot{MeshSnapshot::Create({Square({0, 0}, 0, 0, std::vector<EdgeConnection>(4u), std::move(links)), Square({1, 0}, 3, 0)})};
    REQUIRE(snapshot.has_value()); auto workspace{Workspace()}; const auto a{Location(*snapshot, {0, 0})}, b{Location(*snapshot, {1, 0})};
    auto reverse{workspace->FindPath(b, a)}; REQUIRE(reverse.has_value()); CHECK(reverse->GetStatus() == PathStatus::Complete);
    QueryFilter filter{}; filter.SetExcludedCategories({1'000'000u});
    auto blocked{workspace->FindPath(a, b, filter)}; REQUIRE(blocked.has_value()); CHECK(blocked->GetTermination() == PathTermination::Unreachable);
    filter.SetExcludedCategories({}); REQUIRE(filter.SetAreaEnabled(4u, false).has_value());
    auto areaBlocked{workspace->FindPath(a, b, filter)}; REQUIRE(areaBlocked.has_value()); CHECK(areaBlocked->GetStatus() == PathStatus::Partial);
}
TEST_CASE("Filters distinguish disabled or excluded surfaces from a valid nearest miss") {
    auto snapshot{MeshSnapshot::Create({Square({0, 0}, 0, 0, AllPortals(), {}, 63u, {65'537u}),
        Square({1, 0}, 1, 0, AllPortals(), {}, 63u, {}, false)})};
    REQUIRE(snapshot.has_value()); auto workspace{Workspace()}; QueryFilter filter{}; filter.SetRequiredCategories({65'537u});
    auto first{workspace->FindNearestPolygon(*snapshot, {0.5f, 0, 0.5f}, {2, 2, 2}, filter)};
    REQUIRE(first.has_value()); REQUIRE(first->has_value()); CHECK((*first)->GetLocation().GetTileKey() == TileKey{0, 0});
    filter.SetExcludedCategories({65'537u}); auto miss{workspace->FindNearestPolygon(*snapshot, {0.5f, 0, 0.5f}, {2, 2, 2}, filter)};
    REQUIRE(miss.has_value()); CHECK_FALSE(miss->has_value());
    auto blocked{workspace->FindPath(Location(*snapshot, {0, 0}), Location(*snapshot, {1, 0}))};
    REQUIRE_FALSE(blocked.has_value()); CHECK(blocked.error() == QueryError::FilteredEndpoint);
}
TEST_CASE("Capacity and cancellation return bounded valid partial corridors with explicit reasons") {
    const auto snapshot{Row()}; const auto start{Location(snapshot, {0, 0})}, goal{Location(snapshot, {2, 0})};
    auto nodes{Workspace(QueryLimits{1, 100, 100})}; auto nodeLimited{nodes->FindPath(start, goal)};
    REQUIRE(nodeLimited.has_value()); CHECK(nodeLimited->GetStatus() == PathStatus::Partial);
    CHECK(nodeLimited->GetTermination() == PathTermination::NodeCapacity); CHECK(nodeLimited->GetPolygons().size() == 1u);
    auto expansions{Workspace(QueryLimits{100, 1, 100})}; auto expansionLimited{expansions->FindPath(start, goal)};
    REQUIRE(expansionLimited.has_value()); CHECK(expansionLimited->GetTermination() == PathTermination::ExpansionCapacity);
    CHECK(expansionLimited->GetPolygons().size() == 2u);
    auto corridor{Workspace(QueryLimits{100, 100, 2})}; auto corridorLimited{corridor->FindPath(start, goal)};
    REQUIRE(corridorLimited.has_value()); CHECK(corridorLimited->GetStatus() == PathStatus::Partial);
    CHECK(corridorLimited->GetTermination() == PathTermination::CorridorCapacity); CHECK(corridorLimited->GetPolygons().size() == 2u);
    CHECK(corridorLimited->GetTransitions().size() == 1u); CHECK(corridorLimited->GetCost() == doctest::Approx(1.0));
    std::stop_source cancellation{}; cancellation.request_stop(); auto workspace{Workspace()};
    auto cancelled{workspace->FindPath(start, goal, {}, SearchAlgorithm::AStar, cancellation.get_token())};
    REQUIRE(cancelled.has_value()); CHECK(cancelled->GetTermination() == PathTermination::Cancelled); CHECK(cancelled->GetPolygons().size() == 1u);
    auto nearest{workspace->FindNearestPolygon(snapshot, {}, {1, 1, 1}, {}, cancellation.get_token())};
    REQUIRE_FALSE(nearest.has_value()); CHECK(nearest.error() == QueryError::Cancelled);
    auto invalid{QueryWorkspace::Create(QueryLimits{0, 1, 1})}; CHECK_FALSE(invalid.has_value());
}
TEST_CASE("A missing portal neighbour yields a reachable partial corridor and snapshot capacity is checked") {
    auto snapshot{MeshSnapshot::Create({Square({0, 0}, 0, 0, AllPortals()), Square({2, 0}, 2, 0, AllPortals())})};
    REQUIRE(snapshot.has_value()); auto workspace{Workspace()};
    auto path{workspace->FindPath(Location(*snapshot, {0, 0}), Location(*snapshot, {2, 0}))};
    REQUIRE(path.has_value()); CHECK(path->GetTermination() == PathTermination::Unreachable); CHECK(path->GetPolygons().size() == 1u);
    auto limited{MeshSnapshot::Create({Square({0, 0}, 0, 0, AllPortals()), Square({1, 0}, 1, 0, AllPortals())}, MeshLimits{2, 2, 1})};
    REQUIRE_FALSE(limited.has_value()); CHECK(limited.error() == MeshError::CapacityExceeded);
}
TEST_CASE("Workspace retains its PMR owner, recovers after allocation failure and excludes recursive use") {
    const auto snapshot{Row()}; const auto start{Location(snapshot, {0, 0})}, goal{Location(snapshot, {2, 0})};
    auto resource{std::make_shared<ControlledResource>()}; std::weak_ptr<std::pmr::memory_resource> weak{resource};
    auto result{QueryWorkspace::CreateWithResource(resource)}; REQUIRE(result.has_value()); auto workspace{std::move(*result)};
    resource->SetFailing(true); auto failed{workspace->FindPath(start, goal)};
    REQUIRE_FALSE(failed.has_value()); CHECK(failed.error() == QueryError::AllocationFailure); resource->SetFailing(false);
    resource->SetCallback([&] {
        auto nested{workspace->FindNearestPolygon(snapshot, {}, {1, 1, 1})};
        REQUIRE_FALSE(nested.has_value()); CHECK(nested.error() == QueryError::WorkspaceBusy);
    });
    auto path{workspace->FindPath(start, goal)}; REQUIRE(path.has_value()); CHECK(path->GetStatus() == PathStatus::Complete);
    resource.reset(); CHECK_FALSE(weak.expired()); workspace.reset(); CHECK(weak.expired());
    CHECK(path->GetPolygons()[2].GetTileKey() == TileKey{2, 0});
    auto missingResource{QueryWorkspace::CreateWithResource({})}; REQUIRE_FALSE(missingResource.has_value());
    CHECK(missingResource.error() == QueryError::InvalidResourceLease);
}
TEST_CASE("Non-finite weighted costs fail explicitly without poisoning workspace reuse") {
    const auto snapshot{Row()}; auto workspace{Workspace()}; QueryFilter filter{};
    REQUIRE(filter.SetAreaCost(63u, std::numeric_limits<double>::max()).has_value());
    auto overflow{workspace->FindPath(Location(snapshot, {0, 0}), Location(snapshot, {2, 0}), filter)};
    REQUIRE_FALSE(overflow.has_value()); CHECK(overflow.error() == QueryError::CostOverflow);
    auto valid{workspace->FindPath(Location(snapshot, {0, 0}), Location(snapshot, {2, 0}))};
    REQUIRE(valid.has_value()); CHECK(valid->GetStatus() == PathStatus::Complete);
}
TEST_CASE("Publication deep-freezes a mutable document alias even if the source is subsequently moved") {
    auto mutableTile{std::make_shared<TileDocument>(*Square({0, 0}, 0, 0))};
    auto snapshot{MeshSnapshot::Create({mutableTile})}; REQUIRE(snapshot.has_value());
    CHECK((*snapshot)->GetTiles()[0].get() != mutableTile.get());
    const auto sourceOwnership{std::move(*mutableTile)}; CHECK(sourceOwnership.GetPolygons().size() == 1u);
    CHECK((*snapshot)->GetTiles()[0]->GetPolygons().size() == 1u);
    auto workspace{Workspace()}; auto nearest{workspace->FindNearestPolygon(*snapshot, {0.5f, 0, 0.5f}, {1, 1, 1})};
    REQUIRE(nearest.has_value()); REQUIRE(nearest->has_value()); CHECK((*nearest)->GetPosition() == Vec3{0.5f, 0, 0.5f});
}
TEST_CASE("Local polygon adjacency uses the validated reversed shared edge") {
    std::vector<Vec3> vertices{{0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}};
    std::vector<Polygon> polygons{};
    polygons.emplace_back(std::vector<VertexIndex>{VertexIndex{0}, VertexIndex{1}, VertexIndex{2}},
        std::vector<EdgeConnection>{std::monostate{}, std::monostate{}, PolygonIndex{1}});
    polygons.emplace_back(std::vector<VertexIndex>{VertexIndex{0}, VertexIndex{2}, VertexIndex{3}},
        std::vector<EdgeConnection>{PolygonIndex{0}, std::monostate{}, std::monostate{}});
    auto tile{TileDocument::Create(TileMetadata{{0, 0}, {{0, -1, 0}, {1, 1, 1}}}, std::move(vertices), std::move(polygons))};
    REQUIRE(tile.has_value()); auto snapshot{MeshSnapshot::Create({std::make_shared<const TileDocument>(std::move(*tile))})};
    REQUIRE(snapshot.has_value()); CHECK((*snapshot)->GetArcCount() == 2u); auto workspace{Workspace()};
    auto path{workspace->FindPath(Location(*snapshot, {0, 0}, 0u), Location(*snapshot, {0, 0}, 1u))};
    REQUIRE(path.has_value()); CHECK(path->GetStatus() == PathStatus::Complete); CHECK(path->GetPolygons().size() == 2u);
    REQUIRE(path->GetTransitions().size() == 1u); CHECK(path->GetTransitions()[0].GetDeparture() == Vec3{0.5f, 0, 0.5f});
}
TEST_CASE("Zero-radius off-mesh endpoints attach vertically to a sloped polygon within climb") {
    std::vector<Vec3> vertices{{0, 0, 1}, {1, 1, 1}, {1, 1, 0}, {0, 0, 0}};
    std::vector<Polygon> polygons{Polygon{{VertexIndex{0}, VertexIndex{1}, VertexIndex{2}, VertexIndex{3}}, std::vector<EdgeConnection>(4u)}};
    std::vector<OffMeshConnection> links{}; links.emplace_back(Vec3{0.5f, 0.7f, 0.5f}, Vec3{3.5f, 0, 0.5f}, 0.0f, 7u);
    auto slope{TileDocument::Create(TileMetadata{{0, 0}, {{0, -1, 0}, {1, 2, 1}}}, std::move(vertices), std::move(polygons), {}, std::move(links))};
    REQUIRE(slope.has_value()); auto snapshot{MeshSnapshot::Create({std::make_shared<const TileDocument>(std::move(*slope)), Square({1, 0}, 3, 0)})};
    REQUIRE(snapshot.has_value()); CHECK((*snapshot)->GetArcCount() == 1u); auto workspace{Workspace()};
    auto path{workspace->FindPath(Location(*snapshot, {0, 0}), Location(*snapshot, {1, 0}))};
    REQUIRE(path.has_value()); CHECK(path->GetStatus() == PathStatus::Complete); REQUIRE(path->GetTransitions().size() == 1u);
    CHECK(path->GetTransitions()[0].GetDeparture() == Vec3{0.5f, 0.5f, 0.5f});
}
TEST_CASE("Portal overlap clips sloped height differences to the traversable climb interval") {
    std::vector<Polygon> firstPolygons{Polygon{{VertexIndex{0}, VertexIndex{1}, VertexIndex{2}, VertexIndex{3}},
        {std::monostate{}, PortalSide::PositiveX, std::monostate{}, std::monostate{}}}};
    std::vector<Polygon> secondPolygons{Polygon{{VertexIndex{0}, VertexIndex{1}, VertexIndex{2}, VertexIndex{3}},
        {std::monostate{}, std::monostate{}, std::monostate{}, PortalSide::NegativeX}}};
    auto flat{TileDocument::Create(TileMetadata{{0, 0}, {{0, -1, 0}, {1, 3, 2}}},
        {{0, 0, 2}, {1, 0, 2}, {1, 0, 0}, {0, 0, 0}}, std::move(firstPolygons))};
    auto slope{TileDocument::Create(TileMetadata{{1, 0}, {{1, -1, 0}, {2, 3, 2}}},
        {{1, 2, 2}, {2, 2, 2}, {2, 0, 0}, {1, 0, 0}}, std::move(secondPolygons))};
    REQUIRE(flat.has_value()); REQUIRE(slope.has_value());
    auto snapshot{MeshSnapshot::Create({std::make_shared<const TileDocument>(std::move(*flat)), std::make_shared<const TileDocument>(std::move(*slope))})};
    REQUIRE(snapshot.has_value()); CHECK((*snapshot)->GetArcCount() == 2u); auto workspace{Workspace()};
    auto path{workspace->FindPath(Location(*snapshot, {0, 0}), Location(*snapshot, {1, 0}))};
    REQUIRE(path.has_value()); CHECK(path->GetStatus() == PathStatus::Complete); REQUIRE(path->GetTransitions().size() == 1u);
    CHECK(path->GetTransitions()[0].GetDeparture().GetZ() == doctest::Approx(0.45));
    CHECK(path->GetTransitions()[0].GetArrival().GetY() == doctest::Approx(0.45));
}
TEST_CASE("Mesh-owned graph read views retain authority and reject unchecked node and arc indices") {
    auto snapshot{Row()}; const auto graph{snapshot->ReadGraph()};
    const auto original{Location(snapshot, {0, 0})};
    const auto index{graph.GetNodeIndex(original)}; REQUIRE(index.has_value()); CHECK(index->GetValue() == 0u);
    const auto node{graph.GetNode(*index)}; REQUIRE(node.has_value()); CHECK(node->GetArcCount() == 1u);
    const auto arc{node->GetArc(MeshArcIndex{0})}; REQUIRE(arc.has_value()); CHECK(arc->GetDestination().GetValue() == 1u);
    CHECK_FALSE(arc->GetOffMeshConnection().has_value());
    const auto invalidNode{graph.GetNode(MeshNodeIndex{graph.GetNodeCount()})};
    REQUIRE_FALSE(invalidNode.has_value()); CHECK(invalidNode.error() == MeshError::InvalidNode);
    const auto invalidArc{node->GetArc(MeshArcIndex{node->GetArcCount()})};
    REQUIRE_FALSE(invalidArc.has_value()); CHECK(invalidArc.error() == MeshError::InvalidArc);
    CHECK_FALSE(graph.LocateNode(MeshNodeIndex{graph.GetNodeCount()}).has_value());
    const auto other{Row()}; const auto mismatch{graph.GetNodeIndex(Location(other, {0, 0}))};
    REQUIRE_FALSE(mismatch.has_value()); CHECK(mismatch.error() == MeshError::SnapshotMismatch);
    snapshot.reset(); CHECK(node->GetTile().GetMetadata().GetKey() == TileKey{0, 0});
    const auto restored{graph.LocateNode(*index)}; REQUIRE(restored.has_value()); CHECK(*restored == original);
}
