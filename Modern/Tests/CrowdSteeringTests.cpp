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
import recastnav.query;
import recastnav.crowd;

namespace {
using namespace recastnav;
[[nodiscard]] TileLease CrowdTile(TileKey key, float x, bool portals = false, std::vector<OffMeshConnection> links = {}) {
    std::vector<EdgeConnection> edges(4u);
    if (portals) { edges[1] = PortalSide::PositiveX; edges[3] = PortalSide::NegativeX; }
    std::vector<Polygon> polygons{Polygon{{VertexIndex{0}, VertexIndex{1}, VertexIndex{2}, VertexIndex{3}}, std::move(edges)}};
    auto tile{TileDocument::Create(TileMetadata{key, {{x, -1, 0}, {x + 10, 2, 10}}},
        {{x, 0, 10}, {x + 10, 0, 10}, {x + 10, 0, 0}, {x, 0, 0}}, std::move(polygons), {}, std::move(links))};
    REQUIRE(tile.has_value()); return std::make_shared<const TileDocument>(std::move(*tile));
}
[[nodiscard]] SnapshotLease CrowdMesh(bool row = false) {
    auto mesh{row ? MeshSnapshot::Create({CrowdTile({0, 0}, 0, true), CrowdTile({1, 0}, 10, true), CrowdTile({2, 0}, 20, true)})
                  : MeshSnapshot::Create({CrowdTile({0, 0}, 0)})};
    REQUIRE(mesh.has_value()); return *mesh;
}
[[nodiscard]] PolygonLocation CrowdLocation(const SnapshotLease& mesh, TileKey key = {}) {
    auto location{mesh->Locate(key, PolygonIndex{0})}; REQUIRE(location.has_value()); REQUIRE(location->has_value()); return **location;
}
[[nodiscard]] std::shared_ptr<Crowd> CrowdOwner(const SnapshotLease& mesh, CrowdLimits limits = {}) {
    auto crowd{Crowd::Create(mesh, limits)}; REQUIRE(crowd.has_value()); return *crowd;
}
[[nodiscard]] std::unique_ptr<QueryWorkspace> CrowdQuery(QueryLimits limits = {}) {
    auto query{QueryWorkspace::Create(limits)}; REQUIRE(query.has_value()); return std::move(*query);
}
[[nodiscard]] AgentLease CrowdAgent(const std::shared_ptr<Crowd>& crowd, const SnapshotLease& mesh, Vec3 position,
    TileKey key = {}, AgentPolicy policy = {}) {
    auto agent{crowd->RegisterAgent(ResolvedMotion{position, {}, CrowdLocation(mesh, key)}, std::move(policy))};
    REQUIRE(agent.has_value()); return *agent;
}
[[nodiscard]] CrowdSnapshot CrowdView(const std::shared_ptr<Crowd>& crowd, std::span<const AvoidanceSegment> walls = {}) {
    auto snapshot{crowd->CaptureSnapshot(walls)}; REQUIRE(snapshot.has_value()); return std::move(*snapshot);
}
[[nodiscard]] std::vector<MovementIntent> CrowdPrediction(const std::shared_ptr<Crowd>& crowd, const CrowdSnapshot& snapshot, CrowdSeconds step = CrowdSeconds{0.1}) {
    auto intents{crowd->PredictSteering(snapshot, step)}; REQUIRE(intents.has_value()); return std::move(*intents);
}
class CrowdFailResource final : public std::pmr::memory_resource {
public:
    void SetFailing(bool failing) noexcept { m_failing = failing; }
    void StopOnNextAllocation(std::stop_source source) noexcept { m_stopOnAllocation = std::move(source); }
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (m_failing) { throw std::bad_alloc{}; }
        if (m_stopOnAllocation) { m_stopOnAllocation->request_stop(); m_stopOnAllocation.reset(); }
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) override { std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment); }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    bool m_failing{};
    std::optional<std::stop_source> m_stopOnAllocation{};
};
}

TEST_CASE("An empty crowd predicts an empty owned batch and validates cancellation and time") {
    const auto mesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh)}; const auto snapshot{CrowdView(crowd)};
    const auto intents{CrowdPrediction(crowd, snapshot)}; CHECK(intents.empty());
    CHECK_FALSE(crowd->PredictSteering(snapshot, CrowdSeconds::zero()).has_value());
    CHECK_FALSE(crowd->PredictSteering(snapshot, CrowdSeconds{std::numeric_limits<double>::infinity()}).has_value());
    std::stop_source cancellation{}; cancellation.request_stop();
    const auto cancelled{crowd->PredictSteering(snapshot, CrowdSeconds{0.1}, cancellation.get_token())};
    REQUIRE_FALSE(cancelled.has_value()); CHECK(cancelled.error() == CrowdError::Cancelled);
}
TEST_CASE("Prediction emits bounded velocity without moving actual state and commit accepts the actual resolved displacement") {
    const auto mesh{CrowdMesh(true)}; const auto crowd{CrowdOwner(mesh)}; auto query{CrowdQuery()};
    const auto agent{CrowdAgent(crowd, mesh, {1, 0, 5})};
    auto route{crowd->RequestPath(agent, CrowdLocation(mesh, {2, 0}), {29, 0, 5}, *query)}; REQUIRE(route.has_value());
    const auto snapshot{CrowdView(crowd)}; const auto intents{CrowdPrediction(crowd, snapshot)}; REQUIRE(intents.size() == 1u);
    CHECK(intents[0].GetVelocity().GetX() > 0); CHECK(intents[0].GetVelocity().GetX() <= 0.80001f);
    CHECK(intents[0].GetOutcome() == SteeringOutcome::Moving); CHECK(snapshot.GetAgents()[0].GetMotion().GetPosition() == Vec3{1, 0, 5});
    const auto unchanged{CrowdView(crowd)}; CHECK(unchanged.GetAgents()[0].GetMotion().GetPosition() == Vec3{1, 0, 5});
    // The provider resolved a much larger, externally determined displacement.
    auto committed{crowd->CommitResolvedMotion(intents[0], ResolvedMotion{{12, 0, 5}, {2, 0, 0}, CrowdLocation(mesh, {1, 0})})};
    REQUIRE(committed.has_value()); CHECK(committed->GetCorridorCursor() == 1u); CHECK(committed->GetOutcome() == CommitOutcome::Following);
    const auto actual{CrowdView(crowd)}; CHECK(actual.GetAgents()[0].GetMotion().GetPosition() == Vec3{12, 0, 5});
    CHECK(actual.GetAgents()[0].GetCorridorCursor() == 1u);
    const auto replay{crowd->CommitResolvedMotion(intents[0], ResolvedMotion{{13, 0, 5}, {}, CrowdLocation(mesh, {1, 0})})};
    REQUIRE_FALSE(replay.has_value()); CHECK(replay.error() == CrowdError::StalePrediction);
    const auto stale{crowd->PredictSteering(snapshot, CrowdSeconds{0.1})}; REQUIRE_FALSE(stale.has_value()); CHECK(stale.error() == CrowdError::StaleSnapshot);
}
TEST_CASE("Independent agents commit out of batch order while duplicate or superseded prediction identities are rejected") {
    const auto mesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh)};
    const auto first{CrowdAgent(crowd, mesh, {2, 0, 2})}, second{CrowdAgent(crowd, mesh, {8, 0, 8})};
    const auto snapshot{CrowdView(crowd)}; const auto a{CrowdPrediction(crowd, snapshot)}, b{CrowdPrediction(crowd, snapshot)};
    REQUIRE(a.size() == 2u); REQUIRE(b.size() == 2u);
    CHECK(crowd->CommitResolvedMotion(b[1], ResolvedMotion{{8, 0, 8}, {}, CrowdLocation(mesh)}).has_value());
    CHECK(crowd->CommitResolvedMotion(a[0], ResolvedMotion{{2, 0, 2}, {}, CrowdLocation(mesh)}).has_value());
    const auto stale{crowd->CommitResolvedMotion(b[0], ResolvedMotion{{3, 0, 2}, {}, CrowdLocation(mesh)})};
    REQUIRE_FALSE(stale.has_value()); CHECK(stale.error() == CrowdError::StalePrediction);
    CHECK(first.GetIndex() != second.GetIndex());
}
TEST_CASE("Registration leases reject foreign owners and retired results even after local index reuse") {
    const auto mesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh, CrowdLimits{1})}, other{CrowdOwner(mesh)};
    const auto agent{CrowdAgent(crowd, mesh, {2, 0, 2})}; const auto snapshot{CrowdView(crowd)};
    const auto intents{CrowdPrediction(crowd, snapshot)};
    const auto foreign{other->RetireAgent(agent)}; REQUIRE_FALSE(foreign.has_value()); CHECK(foreign.error() == CrowdError::OwnerMismatch);
    REQUIRE(crowd->RetireAgent(agent).has_value()); const auto replacement{CrowdAgent(crowd, mesh, {7, 0, 7})};
    CHECK(replacement.GetIndex() == agent.GetIndex()); CHECK_FALSE(replacement == agent);
    const auto retired{crowd->CommitResolvedMotion(intents[0], ResolvedMotion{{2, 0, 2}, {}, CrowdLocation(mesh)})};
    REQUIRE_FALSE(retired.has_value()); CHECK(retired.error() == CrowdError::RetiredAgent);
    const auto full{crowd->RegisterAgent(ResolvedMotion{{3, 0, 3}, {}, CrowdLocation(mesh)})}; REQUIRE_FALSE(full.has_value()); CHECK(full.error() == CrowdError::AgentCapacity);
}
TEST_CASE("Dropping every registration lease permits bounded slot reuse without a generation or a global ID") {
    const auto mesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh, CrowdLimits{1})}; CrowdAgentIndex index{};
    { const auto agent{CrowdAgent(crowd, mesh, {2, 0, 2})}; index = agent.GetIndex(); }
    const auto replacement{CrowdAgent(crowd, mesh, {3, 0, 3})}; CHECK(replacement.GetIndex() == index);
}
TEST_CASE("Crossing agents use owned neighbours and avoidance while preserving acceleration limits") {
    const auto mesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh)}; auto query{CrowdQuery()};
    const auto a{CrowdAgent(crowd, mesh, {4, 0, 5})}, b{CrowdAgent(crowd, mesh, {6, 0, 5})};
    REQUIRE(crowd->RequestPath(a, CrowdLocation(mesh), {9, 0, 5}, *query).has_value());
    REQUIRE(crowd->RequestPath(b, CrowdLocation(mesh), {1, 0, 5}, *query).has_value());
    const auto snapshot{CrowdView(crowd)}; const auto intents{CrowdPrediction(crowd, snapshot, CrowdSeconds{0.5})};
    REQUIRE(intents.size() == 2u);
    for (const auto& intent : intents) {
        CHECK(intent.GetNeighbourCount() == 1u); CHECK(intent.GetNeighbourOutcome() == NeighbourOutcome::Complete);
        CHECK(intent.GetVelocity().IsFinite()); CHECK(std::hypot(intent.GetVelocity().GetX(), intent.GetVelocity().GetZ()) <= 3.5001f);
        CHECK(intent.GetAvoidanceOutcome() != AvoidanceOutcome::Stationary);
    }
    CHECK(snapshot.GetAgents()[0].GetMotion().GetPosition() == Vec3{4, 0, 5});
}
TEST_CASE("Neighbour truncation is explicit and never changes the immutable source snapshot") {
    const auto mesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh, CrowdLimits{3, 1})}; auto query{CrowdQuery()};
    const auto a{CrowdAgent(crowd, mesh, {2, 0, 5})}, b{CrowdAgent(crowd, mesh, {3, 0, 5})}, c{CrowdAgent(crowd, mesh, {4, 0, 5})};
    REQUIRE(crowd->RequestPath(a, CrowdLocation(mesh), {9, 0, 5}, *query).has_value());
    const auto snapshot{CrowdView(crowd)}; const auto intents{CrowdPrediction(crowd, snapshot)};
    REQUIRE(intents.size() == 3u); CHECK(intents[0].GetNeighbourCount() == 1u); CHECK(intents[0].GetNeighbourOutcome() == NeighbourOutcome::Truncated);
    CHECK(snapshot.GetAgents().size() == 3u); CHECK(b.GetIndex() != c.GetIndex());
}
TEST_CASE("Blockage and stuck outcomes are based on committed motion rather than the predicted velocity") {
    const auto mesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh)}; auto query{CrowdQuery()};
    AgentPolicy policy{0.3f, 1.8f, 3.5f, 8, 6, 0.1f, 0.1f, 0.5f, CrowdSeconds{2.5}, CrowdSeconds{0.2}};
    const auto agent{CrowdAgent(crowd, mesh, {2, 0, 5}, {}, policy)};
    REQUIRE(crowd->RequestPath(agent, CrowdLocation(mesh), {9, 0, 5}, *query).has_value());
    const std::array walls{AvoidanceSegment{{2.4f, 0, 0}, {2.4f, 0, 10}}, AvoidanceSegment{{1.6f, 0, 0}, {1.6f, 0, 10}},
        AvoidanceSegment{{0, 0, 4.6f}, {10, 0, 4.6f}}, AvoidanceSegment{{0, 0, 5.4f}, {10, 0, 5.4f}}};
    for (std::size_t iteration{}; iteration < 2u; ++iteration) {
        const auto snapshot{CrowdView(crowd, walls)}; const auto intents{CrowdPrediction(crowd, snapshot)};
        CHECK(intents[0].GetAvoidanceOutcome() == AvoidanceOutcome::Blocked);
        auto committed{crowd->CommitResolvedMotion(intents[0], ResolvedMotion{{2, 0, 5}, {}, CrowdLocation(mesh)})}; REQUIRE(committed.has_value());
        CHECK(committed->GetStuckOutcome() == (iteration == 0u ? StuckOutcome::Stalled : StuckOutcome::Stuck));
    }
    CHECK(CrowdView(crowd).GetAgents()[0].GetStuckOutcome() == StuckOutcome::Stuck);
}
TEST_CASE("Partial corridor endpoints never report reaching the requested goal") {
    const auto mesh{CrowdMesh(true)}; const auto crowd{CrowdOwner(mesh)}; auto query{CrowdQuery(QueryLimits{100, 1, 100})};
    const auto agent{CrowdAgent(crowd, mesh, {1, 0, 5})};
    const auto route{crowd->RequestPath(agent, CrowdLocation(mesh, {2, 0}), {29, 0, 5}, *query)};
    REQUIRE(route.has_value()); CHECK(route->GetStatus() == PathStatus::Partial);
    const auto snapshot{CrowdView(crowd)}; const auto intents{CrowdPrediction(crowd, snapshot)};
    auto committed{crowd->CommitResolvedMotion(intents[0], ResolvedMotion{{15, 0, 5}, {}, CrowdLocation(mesh, {1, 0})})};
    REQUIRE(committed.has_value()); CHECK(committed->GetOutcome() == CommitOutcome::PartialCorridorEnd);
    const auto next{CrowdPrediction(crowd, CrowdView(crowd))}; CHECK(next[0].GetOutcome() == SteeringOutcome::PartialCorridorEnd);
}
TEST_CASE("Off-mesh traversal is reported to an external executor and is never advanced automatically") {
    std::vector<OffMeshConnection> links{}; links.emplace_back(Vec3{5, 0, 5}, Vec3{25, 0, 5}, 0.0f, 51u);
    auto mesh{MeshSnapshot::Create({CrowdTile({0, 0}, 0, false, std::move(links)), CrowdTile({2, 0}, 20)})}; REQUIRE(mesh.has_value());
    const auto crowd{CrowdOwner(*mesh)}; auto query{CrowdQuery()}; const auto agent{CrowdAgent(crowd, *mesh, {5, 0, 5})};
    REQUIRE(crowd->RequestPath(agent, CrowdLocation(*mesh, {2, 0}), {29, 0, 5}, *query).has_value());
    const auto snapshot{CrowdView(crowd)}; const auto intents{CrowdPrediction(crowd, snapshot)};
    CHECK(intents[0].GetOutcome() == SteeringOutcome::TraversalRequired); REQUIRE(intents[0].GetRequiredTraversal().has_value());
    CHECK(intents[0].GetRequiredTraversal()->GetOffMeshUserId() == std::optional<std::uint64_t>{51u});
    auto committed{crowd->CommitResolvedMotion(intents[0], ResolvedMotion{{25, 0, 5}, {}, CrowdLocation(*mesh, {2, 0})})};
    REQUIRE(committed.has_value()); CHECK(committed->GetOutcome() == CommitOutcome::TraversalBoundary); CHECK(committed->GetCorridorCursor() == 0u);
    CHECK(CrowdPrediction(crowd, CrowdView(crowd))[0].GetOutcome() == SteeringOutcome::CorridorLost);
    REQUIRE(crowd->RequestPath(agent, CrowdLocation(*mesh, {2, 0}), {29, 0, 5}, *query).has_value());
    CHECK(CrowdPrediction(crowd, CrowdView(crowd))[0].GetOutcome() == SteeringOutcome::Moving);
}
TEST_CASE("Snapshot mismatch, invalid surface motion and outside-navigation truth have explicit outcomes") {
    const auto mesh{CrowdMesh()}, otherMesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh)}; const auto agent{CrowdAgent(crowd, mesh, {2, 0, 2})};
    const auto snapshot{CrowdView(crowd)}; const auto intents{CrowdPrediction(crowd, snapshot)};
    auto mismatch{crowd->CommitResolvedMotion(intents[0], ResolvedMotion{{2, 0, 2}, {}, CrowdLocation(otherMesh)})};
    REQUIRE_FALSE(mismatch.has_value()); CHECK(mismatch.error() == CrowdError::SnapshotMismatch);
    auto invalid{crowd->CommitResolvedMotion(intents[0], ResolvedMotion{{20, 0, 20}, {}, CrowdLocation(mesh)})}; CHECK_FALSE(invalid.has_value());
    auto outside{crowd->CommitResolvedMotion(intents[0], ResolvedMotion{{20, 0, 20}})};
    REQUIRE(outside.has_value()); CHECK(outside->GetOutcome() == CommitOutcome::OutsideNavigation);
    const auto next{CrowdPrediction(crowd, CrowdView(crowd))}; CHECK(next[0].GetOutcome() == SteeringOutcome::OutsideNavigation);
    CHECK(CrowdView(crowd).GetAgents()[0].GetMotion().GetPosition() == Vec3{20, 0, 20});
    CHECK(crowd->RetireAgent(agent).has_value());
}
TEST_CASE("Cancellation retires issued identities and owned filters survive source mutation") {
    const auto mesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh)}; auto query{CrowdQuery()}; QueryFilter filter{};
    const auto agent{CrowdAgent(crowd, mesh, {2, 0, 2}, {}, AgentPolicy{0.3f, 1.8f, 3.5f, 8, 6, 0.1f, 0.1f, 0.5f, CrowdSeconds{2.5}, CrowdSeconds{2}, 0.05f, filter})};
    REQUIRE(filter.SetAreaEnabled(63u, false).has_value());
    REQUIRE(crowd->RequestPath(agent, CrowdLocation(mesh), {8, 0, 2}, *query).has_value());
    const auto snapshot{CrowdView(crowd)}; const auto intents{CrowdPrediction(crowd, snapshot)};
    REQUIRE(crowd->CancelPrediction(intents[0]).has_value()); CHECK_FALSE(crowd->CommitResolvedMotion(intents[0], ResolvedMotion{{3, 0, 2}, {}, CrowdLocation(mesh)}).has_value());
    const auto second{CrowdPrediction(crowd, snapshot)}; REQUIRE(crowd->CancelRoute(agent).has_value());
    CHECK_FALSE(crowd->CommitResolvedMotion(second[0], ResolvedMotion{{3, 0, 2}, {}, CrowdLocation(mesh)}).has_value());
    CHECK(CrowdPrediction(crowd, CrowdView(crowd))[0].GetOutcome() == SteeringOutcome::RouteCancelled);
}
TEST_CASE("PMR allocation failure and configured memory budget are typed and allow retry") {
    const auto mesh{CrowdMesh()}; auto resource{std::make_shared<CrowdFailResource>()}; std::weak_ptr<std::pmr::memory_resource> weak{resource};
    auto created{Crowd::CreateWithResource(mesh, resource)}; REQUIRE(created.has_value()); auto crowd{*created}; created->reset();
    resource->SetFailing(true); auto failed{crowd->RegisterAgent(ResolvedMotion{{2, 0, 2}, {}, CrowdLocation(mesh)})};
    REQUIRE_FALSE(failed.has_value()); CHECK(failed.error() == CrowdError::AllocationFailure);
    resource->SetFailing(false); const auto agent{CrowdAgent(crowd, mesh, {2, 0, 2})};
    resource.reset(); CHECK_FALSE(weak.expired()); REQUIRE(crowd->RetireAgent(agent).has_value()); crowd.reset(); CHECK(weak.expired());
    auto limited{Crowd::Create(mesh, CrowdLimits{1, 1, 1, 1, 1, 4, 1, 100, 1})}; REQUIRE(limited.has_value());
    auto budget{(*limited)->RegisterAgent(ResolvedMotion{{2, 0, 2}, {}, CrowdLocation(mesh)})};
    REQUIRE_FALSE(budget.has_value()); CHECK(budget.error() == CrowdError::MemoryCapacity);
}
TEST_CASE("Work and pending budgets fail transactionally without publishing a partial prediction batch") {
    const auto mesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh, CrowdLimits{1, 1, 10, 10, 1, 4, 1, 0})}; auto query{CrowdQuery()};
    const auto agent{CrowdAgent(crowd, mesh, {2, 0, 2})}; REQUIRE(crowd->RequestPath(agent, CrowdLocation(mesh), {8, 0, 2}, *query).has_value());
    const auto snapshot{CrowdView(crowd)};
    for (std::size_t attempt{}; attempt < 2u; ++attempt) {
        const auto result{crowd->PredictSteering(snapshot, CrowdSeconds{0.1})}; REQUIRE_FALSE(result.has_value()); CHECK(result.error() == CrowdError::WorkCapacity);
    }
    const auto other{CrowdOwner(mesh, CrowdLimits{1, 1, 10, 10, 1})}; const auto passive{CrowdAgent(other, mesh, {2, 0, 2})};
    const auto view{CrowdView(other)}; const auto issued{CrowdPrediction(other, view)};
    const auto full{other->PredictSteering(view, CrowdSeconds{0.1})}; REQUIRE_FALSE(full.has_value()); CHECK(full.error() == CrowdError::PendingCapacity);
    REQUIRE(other->CancelPrediction(issued[0]).has_value()); CHECK(CrowdPrediction(other, view).size() == 1u); CHECK(passive.GetIndex().GetValue() == 0u);
}
TEST_CASE("Snapshots own external obstacle values and dropped prediction leases reclaim pending capacity") {
    const auto mesh{CrowdMesh()}; const auto crowd{CrowdOwner(mesh, CrowdLimits{1, 1, 10, 10, 1})};
    const auto agent{CrowdAgent(crowd, mesh, {2, 0, 2})};
    std::vector<AvoidanceSegment> obstacles{AvoidanceSegment{{4, 0, 0}, {4, 0, 10}}};
    const auto snapshot{CrowdView(crowd, obstacles)}; obstacles[0] = AvoidanceSegment{{8, 0, 0}, {8, 0, 10}};
    CHECK(snapshot.GetObstacles()[0].GetStart() == Vec3{4, 0, 0});
    { const auto dropped{CrowdPrediction(crowd, snapshot)}; CHECK(dropped.size() == 1u); }
    CHECK(CrowdPrediction(crowd, snapshot).size() == 1u); CHECK(agent.GetIndex().GetValue() == 0u);
}
TEST_CASE("Cancellation during corridor staging preserves the prior route, snapshot and issued predictions") {
    const auto mesh{CrowdMesh(true)}; const auto resource{std::make_shared<CrowdFailResource>()};
    auto created{Crowd::CreateWithResource(mesh, resource)}; REQUIRE(created.has_value()); const auto crowd{*created};
    auto query{CrowdQuery()}; const auto agent{CrowdAgent(crowd, mesh, {1, 0, 5})};
    auto initial{crowd->RequestPath(agent, CrowdLocation(mesh, {1, 0}), {19, 0, 5}, *query)};
    REQUIRE(initial.has_value()); REQUIRE(initial->GetPolygonCount() == 2u);
    const auto before{CrowdView(crowd)}; const auto issued{CrowdPrediction(crowd, before)}; REQUIRE(issued.size() == 1u);

    // Only the Crowd PMR resource is armed. The independent query workspace
    // finishes its search before this callback requests cancellation in staging.
    std::stop_source cancellation{}; resource->StopOnNextAllocation(cancellation);
    auto cancelled{crowd->RequestPath(agent, CrowdLocation(mesh, {2, 0}), {29, 0, 5}, *query, cancellation.get_token())};
    REQUIRE(cancellation.stop_requested()); REQUIRE_FALSE(cancelled.has_value()); CHECK(cancelled.error() == CrowdError::Cancelled);
    const auto after{CrowdView(crowd)}; REQUIRE(after.GetAgents().size() == 1u);
    REQUIRE(after.GetAgents()[0].GetCorridor().size() == 2u);
    CHECK(after.GetAgents()[0].GetCorridor().back().GetTileKey() == TileKey{1, 0});
    CHECK(after.GetAgents()[0].GetCorridorCursor() == 0u);
    const auto sameSnapshot{crowd->PredictSteering(before, CrowdSeconds{0.1})}; REQUIRE(sameSnapshot.has_value());
    const auto committed{crowd->CommitResolvedMotion(issued[0], ResolvedMotion{{19, 0, 5}, {}, CrowdLocation(mesh, {1, 0})})};
    REQUIRE(committed.has_value()); CHECK(committed->GetOutcome() == CommitOutcome::GoalReached);
    CHECK(committed->GetCorridorCursor() == 1u);
}
