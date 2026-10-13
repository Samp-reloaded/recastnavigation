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
module recastnav.crowd;
import std;

namespace recastnav {
namespace {
struct Planar final { double x{}, z{}; };
[[nodiscard]] Planar Horizontal(const Vec3& value) noexcept { return {value.GetX(), value.GetZ()}; }
[[nodiscard]] Planar Add(Planar a, Planar b) noexcept { return {a.x + b.x, a.z + b.z}; }
[[nodiscard]] Planar Subtract(Planar a, Planar b) noexcept { return {a.x - b.x, a.z - b.z}; }
[[nodiscard]] Planar Scale(Planar a, double scale) noexcept { return {a.x * scale, a.z * scale}; }
[[nodiscard]] double Dot(Planar a, Planar b) noexcept { return a.x * b.x + a.z * b.z; }
[[nodiscard]] double Length(Planar a) noexcept { return std::hypot(a.x, a.z); }
[[nodiscard]] double Distance(const Vec3& a, const Vec3& b) noexcept { return Length(Subtract(Horizontal(a), Horizontal(b))); }
[[nodiscard]] Planar ClampSpeed(Planar velocity, double maximum) noexcept {
    const double speed{Length(velocity)}; return speed > maximum ? Scale(velocity, maximum / speed) : velocity;
}
[[nodiscard]] Planar Accelerate(Planar actual, Planar target, double acceleration, CrowdSeconds step) noexcept {
    const auto change{Subtract(target, actual)}; const double length{Length(change)};
    if (length == 0.0 || step.count() >= length / acceleration) { return target; }
    return Add(actual, Scale(change, acceleration * step.count() / length));
}
[[nodiscard]] Vec3 Velocity(Planar value) noexcept {
    constexpr double maximum{std::numeric_limits<float>::max()};
    return {static_cast<float>(std::clamp(value.x, -maximum, maximum)), 0, static_cast<float>(std::clamp(value.z, -maximum, maximum))};
}
[[nodiscard]] const TileDocument& TileFor(const PolygonLocation& location) noexcept {
    const auto tiles{location.GetSnapshot()->GetTiles()};
    const auto found{std::lower_bound(tiles.begin(), tiles.end(), location.GetTileKey(), [](const TileLease& tile, const TileKey& key) {
        return tile->GetMetadata().GetKey() < key;
    })};
    return **found;
}
[[nodiscard]] std::optional<double> SurfaceHeight(const PolygonLocation& location, const Vec3& point) noexcept {
    const auto& tile{TileFor(location)}; const auto indices{tile.GetPolygons()[location.GetPolygonIndex().GetValue()].GetVertices()};
    std::optional<double> height{};
    const auto triangle = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
        const double denominator{UpwardTurn(a, b, c)};
        if (denominator <= 0.0 || UpwardTurn(a, b, point) < 0.0 || UpwardTurn(b, c, point) < 0.0 || UpwardTurn(c, a, point) < 0.0) { return; }
        const double v{UpwardTurn(c, a, point) / denominator}, w{UpwardTurn(a, b, point) / denominator};
        const double interpolated{static_cast<double>(a.GetY()) + (static_cast<double>(b.GetY()) - a.GetY()) * v + (static_cast<double>(c.GetY()) - a.GetY()) * w};
        height = std::clamp(interpolated, static_cast<double>(std::min({a.GetY(), b.GetY(), c.GetY()})), static_cast<double>(std::max({a.GetY(), b.GetY(), c.GetY()})));
    };
    if (tile.GetDetails().empty()) {
        for (std::size_t i{1}; i + 1u < indices.size(); ++i) {
            triangle(tile.GetVertices()[indices[0].GetValue()], tile.GetVertices()[indices[i].GetValue()], tile.GetVertices()[indices[i + 1u].GetValue()]);
            if (height) { break; }
        }
    } else {
        const auto& detail{tile.GetDetails()[location.GetPolygonIndex().GetValue()]};
        const auto vertex = [&](DetailVertexIndex index) -> const Vec3& {
            return index.GetValue() < indices.size() ? tile.GetVertices()[indices[index.GetValue()].GetValue()]
                : detail.GetAdditionalVertices()[index.GetValue() - indices.size()];
        };
        for (const auto& face : detail.GetTriangles()) {
            const auto& vertices{face.GetVertices()}; triangle(vertex(vertices[0]), vertex(vertices[1]), vertex(vertices[2])); if (height) { break; }
        }
    }
    return height;
}
[[nodiscard]] Vec3 Centre(const PolygonLocation& location) noexcept {
    const auto& tile{TileFor(location)}; const auto indices{tile.GetPolygons()[location.GetPolygonIndex().GetValue()].GetVertices()};
    double x{}, z{};
    for (const auto index : indices) { x += tile.GetVertices()[index.GetValue()].GetX(); z += tile.GetVertices()[index.GetValue()].GetZ(); }
    const double count{static_cast<double>(indices.size())};
    const Vec3 point{static_cast<float>(x / count), 0, static_cast<float>(z / count)};
    const auto height{SurfaceHeight(location, point)};
    if (!height) { return tile.GetVertices()[indices[0].GetValue()]; }
    return {point.GetX(), static_cast<float>(*height), point.GetZ()};
}
class OperationGuard final {
public:
    explicit OperationGuard(std::atomic_flag& flag) noexcept : m_flag{flag} {}
    ~OperationGuard() { m_flag.clear(std::memory_order_release); }
    OperationGuard(const OperationGuard&) = delete;
    OperationGuard& operator=(const OperationGuard&) = delete;
private:
    std::atomic_flag& m_flag;
};
class OwnedNewDeleteResource final : public std::pmr::memory_resource {
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override { return std::pmr::new_delete_resource()->allocate(bytes, alignment); }
    void do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) override { std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment); }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
class BudgetResource final : public std::pmr::memory_resource {
public:
    BudgetResource(std::shared_ptr<std::pmr::memory_resource> upstream, std::size_t limit) noexcept : m_upstream{std::move(upstream)}, m_limit{limit} {}
    void ResetFailure() noexcept { m_budgetFailure = false; }
    [[nodiscard]] CrowdError AllocationError() const noexcept { return m_budgetFailure ? CrowdError::MemoryCapacity : CrowdError::AllocationFailure; }
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (bytes > m_limit - m_used) { m_budgetFailure = true; throw std::bad_alloc{}; }
        void* memory{m_upstream->allocate(bytes, alignment)}; m_used += bytes; return memory;
    }
    void do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) override { m_upstream->deallocate(memory, bytes, alignment); m_used -= bytes; }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    std::shared_ptr<std::pmr::memory_resource> m_upstream;
    std::size_t m_limit{}, m_used{};
    bool m_budgetFailure{};
};
[[nodiscard]] StuckOutcome StuckState(CrowdSeconds stalled, const AgentPolicy& policy) noexcept {
    return stalled >= policy.GetStuckAfter() ? StuckOutcome::Stuck : stalled > CrowdSeconds::zero() ? StuckOutcome::Stalled : StuckOutcome::Moving;
}
// Relative circle sweep from DetourObstacleAvoidance, evaluated in double with
// explicit stationary overlap handling and durations at the algorithm boundary.
[[nodiscard]] CrowdSeconds CircleImpact(Planar offset, Planar relativeVelocity, double radius, CrowdSeconds horizon) noexcept {
    const double c{Dot(offset, offset) - radius * radius}, a{Dot(relativeVelocity, relativeVelocity)};
    if (a == 0.0) { return c < 0.0 ? CrowdSeconds::zero() : horizon; }
    const double b{Dot(relativeVelocity, offset)}, discriminant{b * b - a * c};
    if (discriminant < 0.0) { return horizon; }
    const double root{std::sqrt(discriminant)}, first{(b - root) / a}, last{(b + root) / a};
    const double time{first < 0.0 && last > 0.0 ? -first * 0.5 : first};
    return time >= 0.0 ? std::min(horizon, CrowdSeconds{time}) : horizon;
}
[[nodiscard]] Planar ClosestSegmentPoint(Planar point, Planar start, Planar end) noexcept {
    const auto edge{Subtract(end, start)}; const double length{Dot(edge, edge)};
    const double t{length > 0.0 ? std::clamp(Dot(Subtract(point, start), edge) / length, 0.0, 1.0) : 0.0};
    return Add(start, Scale(edge, t));
}
[[nodiscard]] CrowdSeconds SegmentImpact(Planar position, Planar velocity, double radius, const AvoidanceSegment& segment, CrowdSeconds horizon) noexcept {
    const auto start{Horizontal(segment.GetStart())}, end{Horizontal(segment.GetEnd())};
    const auto closest{ClosestSegmentPoint(position, start, end)}, away{Subtract(position, closest)};
    if (Dot(away, away) <= radius * radius && Dot(away, velocity) > 0.0) { return horizon; }
    if (Dot(away, away) < radius * radius) { return CrowdSeconds::zero(); }
    CrowdSeconds impact{std::min(CircleImpact(Subtract(start, position), velocity, radius, horizon), CircleImpact(Subtract(end, position), velocity, radius, horizon))};
    const auto edge{Subtract(end, start)}; const double length{Length(edge)};
    const Planar direction{Scale(edge, 1.0 / length)}, normal{-direction.z, direction.x};
    const double speed{Dot(velocity, normal)}, distance{Dot(Subtract(position, start), normal)};
    if (speed != 0.0) {
        for (const double side : std::array{-radius, radius}) {
            const double time{(side - distance) / speed};
            if (time < 0.0 || time > impact.count()) { continue; }
            const double along{Dot(Subtract(Add(position, Scale(velocity, time)), start), direction)};
            if (along >= 0.0 && along <= length) { impact = CrowdSeconds{time}; }
        }
    }
    return impact;
}
[[nodiscard]] bool VerticalOverlap(const ResolvedMotion& a, const AgentPolicy& ap, const ResolvedMotion& b, const AgentPolicy& bp) noexcept {
    return static_cast<double>(a.GetPosition().GetY()) < static_cast<double>(b.GetPosition().GetY()) + bp.GetHeight()
        && static_cast<double>(b.GetPosition().GetY()) < static_cast<double>(a.GetPosition().GetY()) + ap.GetHeight();
}
}

class AgentLease::Identity final {
public:
    Identity(std::weak_ptr<const Crowd> owner, CrowdAgentIndex index) noexcept : m_owner{std::move(owner)}, m_index{index} {}
    [[nodiscard]] const std::weak_ptr<const Crowd>& GetOwner() const noexcept { return m_owner; }
    [[nodiscard]] CrowdAgentIndex GetIndex() const noexcept { return m_index; }
private:
    std::weak_ptr<const Crowd> m_owner;
    CrowdAgentIndex m_index{};
};
class AgentSnapshot::Stamp final {};
class CrowdSnapshot::Stamp final {};
class MovementIntent::Prediction final {
public:
    explicit Prediction(std::shared_ptr<const AgentSnapshot::Stamp> stamp) noexcept : m_stamp{std::move(stamp)} {}
    [[nodiscard]] const std::shared_ptr<const AgentSnapshot::Stamp>& GetStamp() const noexcept { return m_stamp; }
private:
    std::shared_ptr<const AgentSnapshot::Stamp> m_stamp;
};
class Crowd::State final {
public:
    struct Agent final {
        Agent(ResolvedMotion actual, AgentPolicy selectedPolicy, std::pmr::memory_resource& resource)
            : motion{std::move(actual)}, policy{std::move(selectedPolicy)}, corridor{&resource}, transitions{&resource}, pending{&resource} {}
        ResolvedMotion motion;
        AgentPolicy policy;
        std::pmr::vector<PolygonLocation> corridor;
        std::pmr::vector<MeshTransition> transitions;
        std::pmr::vector<std::weak_ptr<const MovementIntent::Prediction>> pending;
        std::size_t cursor{};
        PathStatus routeStatus{PathStatus::Partial};
        Vec3 target{};
        bool cancelled{};
        CrowdSeconds stalled{};
        std::shared_ptr<const AgentSnapshot::Stamp> stamp{};
    };
    struct Slot final { std::weak_ptr<const AgentLease::Identity> identity{}; std::unique_ptr<Agent> agent{}; };
    State(SnapshotLease selectedMesh, std::shared_ptr<std::pmr::memory_resource> resource, CrowdLimits selectedLimits)
        : mesh{std::move(selectedMesh)}, limits{selectedLimits}, memory{std::move(resource), limits.GetMaximumPmrBytes()},
          slots{&memory}, neighbours{&memory}, walls{&memory} {}
    SnapshotLease mesh;
    CrowdLimits limits;
    BudgetResource memory;
    std::pmr::vector<Slot> slots;
    std::pmr::vector<std::pair<double, std::size_t>> neighbours;
    std::pmr::vector<AvoidanceSegment> walls;
    std::shared_ptr<const CrowdSnapshot::Stamp> stamp{};
    std::atomic_flag inUse{};
};
bool AgentPolicy::IsValid() const noexcept {
    const std::array values{m_radius, m_height, m_speed, m_acceleration, m_neighbourRange, m_arrival, m_surfaceTolerance, m_separation, m_minimumProgress};
    return std::ranges::all_of(values, [](float value) { return std::isfinite(value) && value >= 0.0f; })
        && m_height > 0.0f && m_speed > 0.0f && m_acceleration > 0.0f && m_neighbourRange > 0.0f
        && m_arrival > 0.0f && m_surfaceTolerance > 0.0f
        && std::isfinite(m_horizon.count()) && m_horizon > CrowdSeconds::zero()
        && std::isfinite(m_stuckAfter.count()) && m_stuckAfter > CrowdSeconds::zero();
}
CrowdAgentIndex AgentLease::GetIndex() const noexcept { return m_identity->GetIndex(); }
StuckOutcome AgentSnapshot::GetStuckOutcome() const noexcept { return StuckState(m_stalled, m_policy); }
Crowd::Crowd(std::unique_ptr<State> state, ConstructionKey) noexcept : m_state{std::move(state)} {}
Crowd::~Crowd() = default;
std::expected<std::shared_ptr<Crowd>, CrowdError> Crowd::Create(SnapshotLease mesh, CrowdLimits limits) noexcept {
    try { return CreateWithResource(std::move(mesh), std::make_shared<OwnedNewDeleteResource>(), limits); }
    catch (const std::bad_alloc&) { return std::unexpected{CrowdError::AllocationFailure}; }
}
std::expected<std::shared_ptr<Crowd>, CrowdError> Crowd::CreateWithResource(SnapshotLease mesh,
    std::shared_ptr<std::pmr::memory_resource> resource, CrowdLimits limits) noexcept {
    if (!mesh) { return std::unexpected{CrowdError::MissingSnapshot}; }
    if (!resource) { return std::unexpected{CrowdError::InvalidResourceLease}; }
    if (!limits.IsValid()) { return std::unexpected{CrowdError::InvalidLimits}; }
    try {
        return std::make_shared<Crowd>(std::make_unique<State>(std::move(mesh), std::move(resource), limits), ConstructionKey{});
    } catch (const std::bad_alloc&) { return std::unexpected{CrowdError::AllocationFailure}; }
}
std::expected<void, CrowdError> Crowd::ValidateAgent(const AgentLease& agent) const noexcept {
    if (!agent.m_identity) { return std::unexpected{CrowdError::RetiredAgent}; }
    const auto owner{agent.m_identity->GetOwner().lock()};
    if (!owner) { return std::unexpected{CrowdError::RetiredAgent}; }
    if (owner.get() != this) { return std::unexpected{CrowdError::OwnerMismatch}; }
    const auto index{agent.GetIndex().GetValue()};
    if (index >= m_state->slots.size() || !m_state->slots[index].agent || m_state->slots[index].identity.lock() != agent.m_identity) {
        return std::unexpected{CrowdError::RetiredAgent};
    }
    return {};
}
std::expected<void, CrowdError> Crowd::ValidateMotion(const ResolvedMotion& motion, const AgentPolicy& policy) const noexcept {
    if (!motion.GetPosition().IsFinite() || !motion.GetVelocity().IsFinite()) { return std::unexpected{CrowdError::InvalidMotion}; }
    if (!motion.GetLocation()) { return {}; }
    const auto& location{*motion.GetLocation()};
    if (location.GetSnapshot() != m_state->mesh) { return std::unexpected{CrowdError::SnapshotMismatch}; }
    const auto polygon{m_state->mesh->GetPolygon(location)};
    if (!polygon || !policy.GetFilter().Accepts(polygon->get())) { return std::unexpected{CrowdError::InvalidMotion}; }
    const auto height{SurfaceHeight(location, motion.GetPosition())};
    if (!height || std::abs(*height - motion.GetPosition().GetY()) > policy.GetSurfaceTolerance()) { return std::unexpected{CrowdError::InvalidMotion}; }
    return {};
}
std::expected<AgentLease, CrowdError> Crowd::RegisterAgent(ResolvedMotion motion, const AgentPolicy& policy) noexcept {
    if (m_state->inUse.test_and_set(std::memory_order_acquire)) { return std::unexpected{CrowdError::Busy}; }
    const OperationGuard guard{m_state->inUse}; m_state->memory.ResetFailure();
    if (!policy.IsValid()) { return std::unexpected{CrowdError::InvalidPolicy}; }
    if (const auto valid{ValidateMotion(motion, policy)}; !valid) { return std::unexpected{valid.error()}; }
    std::size_t index{};
    for (; index < m_state->slots.size(); ++index) { if (!m_state->slots[index].agent || m_state->slots[index].identity.expired()) { break; } }
    if (index == m_state->limits.GetMaximumAgents()) { return std::unexpected{CrowdError::AgentCapacity}; }
    try {
        auto data{std::make_unique<State::Agent>(std::move(motion), policy, m_state->memory)};
        auto identity{std::make_shared<const AgentLease::Identity>(weak_from_this(), CrowdAgentIndex{index})};
        if (index == m_state->slots.size()) { m_state->slots.emplace_back(); }
        auto& slot{m_state->slots[index]}; slot.agent = std::move(data); slot.identity = identity; m_state->stamp.reset();
        return AgentLease{std::move(identity)};
    } catch (const std::bad_alloc&) { return std::unexpected{m_state->memory.AllocationError()}; }
      catch (const std::length_error&) { return std::unexpected{CrowdError::MemoryCapacity}; }
}
std::expected<void, CrowdError> Crowd::RetireAgent(const AgentLease& agent) noexcept {
    if (m_state->inUse.test_and_set(std::memory_order_acquire)) { return std::unexpected{CrowdError::Busy}; }
    const OperationGuard guard{m_state->inUse};
    if (const auto valid{ValidateAgent(agent)}; !valid) { return std::unexpected{valid.error()}; }
    m_state->slots[agent.GetIndex().GetValue()].agent.reset(); m_state->stamp.reset(); return {};
}
std::expected<void, CrowdError> Crowd::CancelRoute(const AgentLease& agent) noexcept {
    if (m_state->inUse.test_and_set(std::memory_order_acquire)) { return std::unexpected{CrowdError::Busy}; }
    const OperationGuard guard{m_state->inUse};
    if (const auto valid{ValidateAgent(agent)}; !valid) { return std::unexpected{valid.error()}; }
    auto& data{*m_state->slots[agent.GetIndex().GetValue()].agent};
    data.corridor.clear(); data.transitions.clear(); data.pending.clear(); data.cursor = 0u;
    data.cancelled = true; data.stalled = CrowdSeconds::zero(); data.stamp.reset(); m_state->stamp.reset(); return {};
}
std::expected<RouteAdmission, CrowdError> Crowd::RequestPath(const AgentLease& agent,
    const PolygonLocation& goal, const Vec3& target, QueryWorkspace& query, std::stop_token cancellation) noexcept {
    if (m_state->inUse.test_and_set(std::memory_order_acquire)) { return std::unexpected{CrowdError::Busy}; }
    const OperationGuard guard{m_state->inUse}; m_state->memory.ResetFailure();
    if (const auto valid{ValidateAgent(agent)}; !valid) { return std::unexpected{valid.error()}; }
    auto& data{*m_state->slots[agent.GetIndex().GetValue()].agent};
    if (!data.motion.GetLocation()) { return std::unexpected{CrowdError::InvalidMotion}; }
    if (const auto valid{ValidateMotion(ResolvedMotion{target, {}, goal}, data.policy)}; !valid) { return std::unexpected{valid.error()}; }
    if (cancellation.stop_requested()) { return std::unexpected{CrowdError::Cancelled}; }
    auto route{query.FindPath(*data.motion.GetLocation(), goal, data.policy.GetFilter(), SearchAlgorithm::AStar, cancellation)};
    if (!route) { return std::unexpected{route.error() == QueryError::AllocationFailure ? CrowdError::AllocationFailure : CrowdError::QueryRejected}; }
    if (cancellation.stop_requested() || route->GetTermination() == PathTermination::Cancelled) { return std::unexpected{CrowdError::Cancelled}; }
    if (route->GetPolygons().size() > m_state->limits.GetMaximumCorridor()) { return std::unexpected{CrowdError::CorridorCapacity}; }
    try {
        std::pmr::vector<PolygonLocation> corridor{route->GetPolygons().begin(), route->GetPolygons().end(), &m_state->memory};
        std::pmr::vector<MeshTransition> transitions{route->GetTransitions().begin(), route->GetTransitions().end(), &m_state->memory};
        const Vec3 steeringTarget{route->GetStatus() == PathStatus::Complete ? target : Centre(corridor.back())};
        if (cancellation.stop_requested()) { return std::unexpected{CrowdError::Cancelled}; }
        data.corridor.swap(corridor); data.transitions.swap(transitions); data.cursor = 0u;
        data.target = steeringTarget; data.routeStatus = route->GetStatus(); data.cancelled = false;
        data.stalled = CrowdSeconds::zero(); data.pending.clear(); data.stamp.reset(); m_state->stamp.reset();
        return RouteAdmission{route->GetStatus(), route->GetTermination(), data.corridor.size()};
    } catch (const std::bad_alloc&) { return std::unexpected{m_state->memory.AllocationError()}; }
      catch (const std::length_error&) { return std::unexpected{CrowdError::MemoryCapacity}; }
}
std::expected<CrowdSnapshot, CrowdError> Crowd::CaptureSnapshot(std::span<const AvoidanceSegment> obstacles) noexcept {
    if (m_state->inUse.test_and_set(std::memory_order_acquire)) { return std::unexpected{CrowdError::Busy}; }
    const OperationGuard guard{m_state->inUse};
    if (obstacles.size() > m_state->limits.GetMaximumObstacles()) { return std::unexpected{CrowdError::ObstacleCapacity}; }
    for (const auto& obstacle : obstacles) {
        if (!obstacle.GetStart().IsFinite() || !obstacle.GetEnd().IsFinite() || !std::isfinite(obstacle.GetHeight())
            || obstacle.GetHeight() < 0.0f || Distance(obstacle.GetStart(), obstacle.GetEnd()) == 0.0) { return std::unexpected{CrowdError::InvalidObstacle}; }
    }
    try {
        std::vector<AgentSnapshot> agents{}; agents.reserve(m_state->slots.size());
        for (std::size_t i{}; i < m_state->slots.size(); ++i) {
            auto& slot{m_state->slots[i]}; auto identity{slot.identity.lock()};
            if (!slot.agent || !identity) { if (slot.agent) { slot.agent.reset(); m_state->stamp.reset(); } continue; }
            auto& data{*slot.agent}; if (!data.stamp) { data.stamp = std::make_shared<const AgentSnapshot::Stamp>(); }
            agents.push_back(AgentSnapshot{AgentLease{std::move(identity)}, data.motion, data.policy,
                std::vector<PolygonLocation>{data.corridor.begin(), data.corridor.end()}, std::vector<MeshTransition>{data.transitions.begin(), data.transitions.end()},
                data.cursor, data.routeStatus, data.target, data.cancelled, data.stalled, data.stamp});
        }
        std::vector<AvoidanceSegment> copiedObstacles{obstacles.begin(), obstacles.end()};
        if (!m_state->stamp) { m_state->stamp = std::make_shared<const CrowdSnapshot::Stamp>(); }
        return CrowdSnapshot{m_state->mesh, weak_from_this(), std::move(agents), std::move(copiedObstacles), m_state->stamp};
    } catch (const std::bad_alloc&) { return std::unexpected{CrowdError::AllocationFailure}; }
      catch (const std::length_error&) { return std::unexpected{CrowdError::MemoryCapacity}; }
}
std::expected<std::vector<MovementIntent>, CrowdError> Crowd::PredictSteering(const CrowdSnapshot& snapshot,
    CrowdSeconds step, std::stop_token cancellation) noexcept {
    if (m_state->inUse.test_and_set(std::memory_order_acquire)) { return std::unexpected{CrowdError::Busy}; }
    const OperationGuard guard{m_state->inUse}; m_state->memory.ResetFailure();
    if (!std::isfinite(step.count()) || step <= CrowdSeconds::zero()) { return std::unexpected{CrowdError::InvalidTimeStep}; }
    if (snapshot.m_owner.lock().get() != this) { return std::unexpected{CrowdError::OwnerMismatch}; }
    if (snapshot.m_mesh != m_state->mesh) { return std::unexpected{CrowdError::SnapshotMismatch}; }
    if (!m_state->stamp || snapshot.m_stamp != m_state->stamp) { return std::unexpected{CrowdError::StaleSnapshot}; }
    try { return Predict(snapshot, step, cancellation); }
    catch (const std::bad_alloc&) { return std::unexpected{m_state->memory.AllocationError()}; }
    catch (const std::length_error&) { return std::unexpected{CrowdError::MemoryCapacity}; }
}
std::expected<std::vector<MovementIntent>, CrowdError> Crowd::Predict(const CrowdSnapshot& snapshot,
    CrowdSeconds step, std::stop_token cancellation) {
    if (cancellation.stop_requested()) { return std::unexpected{CrowdError::Cancelled}; }
    for (const auto& agent : snapshot.m_agents) {
        if (const auto valid{ValidateAgent(agent.m_agent)}; !valid) { return std::unexpected{valid.error()}; }
        auto& data{*m_state->slots[agent.m_agent.GetIndex().GetValue()].agent};
        if (data.stamp != agent.m_stamp) { return std::unexpected{CrowdError::StaleSnapshot}; }
        std::erase_if(data.pending, [](const auto& prediction) { return prediction.expired(); });
        if (data.pending.size() == m_state->limits.GetMaximumPendingPerAgent()) { return std::unexpected{CrowdError::PendingCapacity}; }
    }
    // Reserve every pending append before producing any issued identity. A failed
    // batch can change scratch capacity, but cannot publish half its predictions.
    for (const auto& agent : snapshot.m_agents) {
        auto& pending{m_state->slots[agent.m_agent.GetIndex().GetValue()].agent->pending}; pending.reserve(pending.size() + 1u);
    }
    std::vector<MovementIntent> intents{}; intents.reserve(snapshot.m_agents.size());
    std::size_t work{};
    const auto chargeWork = [&]() { if (work == m_state->limits.GetMaximumWork()) { return false; } ++work; return true; };
    const auto& limits{m_state->limits};
    for (std::size_t agentIndex{}; agentIndex < snapshot.m_agents.size(); ++agentIndex) {
        if (cancellation.stop_requested()) { return std::unexpected{CrowdError::Cancelled}; }
        const auto& agent{snapshot.m_agents[agentIndex]}; const auto& policy{agent.m_policy};
        const auto& actual{agent.m_motion}; const auto position{Horizontal(actual.GetPosition())}, currentVelocity{Horizontal(actual.GetVelocity())};
        SteeringOutcome outcome{SteeringOutcome::Moving}; Vec3 target{agent.m_target};
        std::optional<MeshTransition> traversal{};
        if (!actual.GetLocation()) { outcome = SteeringOutcome::OutsideNavigation; }
        else if (agent.m_corridor.empty()) { outcome = agent.m_cancelled ? SteeringOutcome::RouteCancelled : SteeringOutcome::Idle; }
        else if (*actual.GetLocation() != agent.m_corridor[agent.m_cursor]) { outcome = SteeringOutcome::CorridorLost; }
        else if (agent.m_cursor + 1u < agent.m_corridor.size()) {
            const auto& transition{agent.m_transitions[agent.m_cursor]}; target = transition.GetDeparture();
            if (Distance(actual.GetPosition(), target) <= policy.GetArrivalRadius()) {
                if (transition.GetOffMeshUserId()) { outcome = SteeringOutcome::TraversalRequired; traversal = transition; }
                else { target = Centre(agent.m_corridor[agent.m_cursor + 1u]); }
            }
        } else if (Distance(actual.GetPosition(), target) <= policy.GetArrivalRadius()
            && std::abs(static_cast<double>(actual.GetPosition().GetY()) - target.GetY()) <= policy.GetSurfaceTolerance()) {
            outcome = agent.m_routeStatus == PathStatus::Complete ? SteeringOutcome::GoalReached : SteeringOutcome::PartialCorridorEnd;
        }
        Planar preferred{}; AvoidanceOutcome avoidance{AvoidanceOutcome::Stationary};
        NeighbourOutcome neighbourOutcome{NeighbourOutcome::Complete};
        m_state->neighbours.clear();
        if (outcome == SteeringOutcome::Moving) {
            const auto delta{Subtract(Horizontal(target), position)}; const double distance{Length(delta)};
            const double braking{std::sqrt(2.0 * policy.GetMaximumAcceleration() * distance)};
            const double speed{std::min({static_cast<double>(policy.GetMaximumSpeed()), braking, distance / step.count()})};
            if (distance > 0.0) { preferred = Scale(delta, speed / distance); }
            const double range{policy.GetNeighbourRange()};
            for (std::size_t otherIndex{}; otherIndex < snapshot.m_agents.size(); ++otherIndex) {
                if (!chargeWork()) { return std::unexpected{CrowdError::WorkCapacity}; }
                if (cancellation.stop_requested()) { return std::unexpected{CrowdError::Cancelled}; }
                if (otherIndex == agentIndex) { continue; }
                const auto& other{snapshot.m_agents[otherIndex]};
                if (!VerticalOverlap(actual, policy, other.m_motion, other.m_policy)) { continue; }
                const auto offset{Subtract(Horizontal(other.m_motion.GetPosition()), position)}; const double squared{Dot(offset, offset)};
                if (squared > range * range) { continue; }
                const std::pair key{squared, otherIndex};
                const auto found{std::lower_bound(m_state->neighbours.begin(), m_state->neighbours.end(), key)};
                if (m_state->neighbours.size() == limits.GetMaximumNeighbours()) {
                    neighbourOutcome = NeighbourOutcome::Truncated;
                    if (found == m_state->neighbours.end()) { continue; }
                }
                m_state->neighbours.insert(found, key);
                if (m_state->neighbours.size() > limits.GetMaximumNeighbours()) { m_state->neighbours.pop_back(); }
            }
            Planar separation{};
            for (const auto& [squared, otherIndex] : m_state->neighbours) {
                const auto& other{snapshot.m_agents[otherIndex]};
                const double distanceToOther{std::sqrt(squared)};
                auto away{Subtract(position, Horizontal(other.m_motion.GetPosition()))};
                if (distanceToOther == 0.0) { away = agent.m_agent.GetIndex() < other.m_agent.GetIndex() ? Planar{-1.0, 0.0} : Planar{1.0, 0.0}; }
                else { away = Scale(away, 1.0 / distanceToOther); }
                const double weight{1.0 - distanceToOther / range}; separation = Add(separation, Scale(away, weight * weight));
            }
            if (!m_state->neighbours.empty()) {
                preferred = ClampSpeed(Add(preferred, Scale(separation, static_cast<double>(policy.GetSeparationWeight()) * policy.GetMaximumSpeed()
                    / static_cast<double>(m_state->neighbours.size()))), policy.GetMaximumSpeed());
            }
        }
        Planar selected{Accelerate(currentVelocity, {}, policy.GetMaximumAcceleration(), step)};
        if (outcome == SteeringOutcome::Moving) {
            m_state->walls.assign(snapshot.m_obstacles.begin(), snapshot.m_obstacles.end());
            const auto& location{*actual.GetLocation()}; const auto& tile{TileFor(location)};
            const auto& polygon{tile.GetPolygons()[location.GetPolygonIndex().GetValue()]};
            for (std::size_t edge{}; edge < polygon.GetEdges().size(); ++edge) {
                bool wall{std::holds_alternative<std::monostate>(polygon.GetEdges()[edge])};
                if (const auto* local{std::get_if<PolygonIndex>(&polygon.GetEdges()[edge])}) { wall = !policy.GetFilter().Accepts(tile.GetPolygons()[local->GetValue()]); }
                if (!wall) { continue; }
                const auto& start{tile.GetVertices()[polygon.GetVertices()[edge].GetValue()]};
                const auto& end{tile.GetVertices()[polygon.GetVertices()[(edge + 1u) % polygon.GetVertices().size()].GetValue()]};
                if (Distance(start, end) > 0.0) { m_state->walls.emplace_back(start, end, policy.GetHeight()); }
            }
            const auto horizon{policy.GetAvoidanceHorizon()}; double bestScore{std::numeric_limits<double>::infinity()};
            CrowdSeconds bestImpact{horizon}; bool overlapping{};
            for (const auto& [squared, otherIndex] : m_state->neighbours) {
                const double radius{static_cast<double>(policy.GetRadius()) + snapshot.m_agents[otherIndex].m_policy.GetRadius()};
                overlapping = overlapping || squared < radius * radius;
            }
            const std::size_t samples{3u + limits.GetAngularSamples() * limits.GetSpeedSamples()};
            for (std::size_t sample{}; sample < samples; ++sample) {
                if (!chargeWork()) { return std::unexpected{CrowdError::WorkCapacity}; }
                if (cancellation.stop_requested()) { return std::unexpected{CrowdError::Cancelled}; }
                Planar candidate{};
                if (sample == 0u) { candidate = preferred; }
                else if (sample == 1u) { candidate = ClampSpeed(currentVelocity, policy.GetMaximumSpeed()); }
                else if (sample >= 3u) {
                    const std::size_t pattern{sample - 3u}, ring{pattern / limits.GetAngularSamples()}, angleIndex{pattern % limits.GetAngularSamples()};
                    const double heading{std::atan2(preferred.z, preferred.x)};
                    const double angle{heading + 2.0 * std::numbers::pi * static_cast<double>(angleIndex) / static_cast<double>(limits.GetAngularSamples())};
                    const double speed{static_cast<double>(policy.GetMaximumSpeed()) * static_cast<double>(ring + 1u) / static_cast<double>(limits.GetSpeedSamples())};
                    candidate = {std::cos(angle) * speed, std::sin(angle) * speed};
                }
                candidate = Accelerate(currentVelocity, candidate, policy.GetMaximumAcceleration(), step);
                CrowdSeconds impact{horizon}; double sidePenalty{};
                for (const auto& [squared, otherIndex] : m_state->neighbours) {
                    if (!chargeWork()) { return std::unexpected{CrowdError::WorkCapacity}; }
                    const auto& other{snapshot.m_agents[otherIndex]}; const auto offset{Subtract(Horizontal(other.m_motion.GetPosition()), position)};
                    const auto relative{Subtract(Subtract(Scale(candidate, 2.0), currentVelocity), Horizontal(other.m_motion.GetVelocity()))};
                    impact = std::min(impact, CircleImpact(offset, relative, static_cast<double>(policy.GetRadius()) + other.m_policy.GetRadius(), horizon));
                    const double length{std::sqrt(squared)};
                    const auto direction{length > 0.0 ? Scale(offset, 1.0 / length) : Planar{1.0, 0.0}};
                    // A deterministic right-hand side preference breaks head-on symmetry.
                    const Planar side{-direction.z, direction.x};
                    sidePenalty += std::clamp(std::min(Dot(direction, relative) * 0.5 + 0.5, Dot(side, relative) * 2.0), 0.0, 1.0);
                }
                for (const auto& wall : m_state->walls) {
                    if (!chargeWork()) { return std::unexpected{CrowdError::WorkCapacity}; }
                    const double low{std::min(wall.GetStart().GetY(), wall.GetEnd().GetY())}, high{static_cast<double>(std::max(wall.GetStart().GetY(), wall.GetEnd().GetY())) + wall.GetHeight()};
                    if (static_cast<double>(actual.GetPosition().GetY()) + policy.GetHeight() < low || actual.GetPosition().GetY() > high) { continue; }
                    const auto away{Subtract(position, ClosestSegmentPoint(position, Horizontal(wall.GetStart()), Horizontal(wall.GetEnd())))};
                    overlapping = overlapping || Dot(away, away) < static_cast<double>(policy.GetRadius()) * policy.GetRadius();
                    impact = std::min(impact, SegmentImpact(position, candidate, policy.GetRadius(), wall, horizon));
                }
                if (!m_state->neighbours.empty()) { sidePenalty /= static_cast<double>(m_state->neighbours.size()); }
                const double score{2.0 * Length(Subtract(candidate, preferred)) / policy.GetMaximumSpeed()
                    + 0.75 * Length(Subtract(candidate, currentVelocity)) / policy.GetMaximumSpeed() + 0.75 * sidePenalty
                    + 2.5 / (0.1 + impact.count() / horizon.count())};
                if (score < bestScore) { bestScore = score; selected = candidate; bestImpact = impact; }
            }
            avoidance = overlapping ? AvoidanceOutcome::Overlapping
                : bestImpact < step || Length(selected) < policy.GetMinimumProgressSpeed() ? AvoidanceOutcome::Blocked
                : Length(Subtract(selected, Accelerate(currentVelocity, preferred, policy.GetMaximumAcceleration(), step)))
                    > static_cast<double>(policy.GetMaximumSpeed()) * 0.00001 ? AvoidanceOutcome::Adjusted : AvoidanceOutcome::Clear;
        }
        auto prediction{std::make_shared<const MovementIntent::Prediction>(agent.m_stamp)};
        intents.push_back(MovementIntent{agent.m_agent, Velocity(selected), target, step, outcome, avoidance,
            neighbourOutcome, m_state->neighbours.size(), agent.GetStuckOutcome(), std::move(traversal),
            Length(selected) > policy.GetMaximumSpeed(), std::move(prediction)});
    }
    if (cancellation.stop_requested()) { return std::unexpected{CrowdError::Cancelled}; }
    for (const auto& intent : intents) { m_state->slots[intent.m_agent.GetIndex().GetValue()].agent->pending.push_back(intent.m_prediction); }
    return intents;
}
std::expected<MotionCommit, CrowdError> Crowd::CommitResolvedMotion(const MovementIntent& intent, ResolvedMotion motion) noexcept {
    if (m_state->inUse.test_and_set(std::memory_order_acquire)) { return std::unexpected{CrowdError::Busy}; }
    const OperationGuard guard{m_state->inUse};
    if (const auto valid{ValidateAgent(intent.m_agent)}; !valid) { return std::unexpected{valid.error()}; }
    auto& data{*m_state->slots[intent.m_agent.GetIndex().GetValue()].agent};
    const auto pending{std::ranges::find_if(data.pending, [&](const auto& prediction) { return prediction.lock() == intent.m_prediction; })};
    if (!intent.m_prediction || pending == data.pending.end() || intent.m_prediction->GetStamp() != data.stamp) { return std::unexpected{CrowdError::StalePrediction}; }
    if (const auto valid{ValidateMotion(motion, data.policy)}; !valid) { return std::unexpected{valid.error()}; }
    CommitOutcome outcome{CommitOutcome::Idle}; std::size_t cursor{data.cursor};
    if (!motion.GetLocation()) { outcome = CommitOutcome::OutsideNavigation; }
    else if (!data.corridor.empty()) {
        const auto found{std::ranges::find(data.corridor, *motion.GetLocation())};
        if (found == data.corridor.end()) { outcome = CommitOutcome::CorridorLost; }
        else {
            const std::size_t next{static_cast<std::size_t>(found - data.corridor.begin())};
            bool traversal{};
            for (std::size_t edge{std::min(cursor, next)}; edge < std::max(cursor, next); ++edge) {
                traversal = traversal || data.transitions[edge].GetOffMeshUserId().has_value();
            }
            if (traversal) { outcome = CommitOutcome::TraversalBoundary; }
            else {
                cursor = next; outcome = CommitOutcome::Following;
                if (cursor + 1u == data.corridor.size() && Distance(motion.GetPosition(), data.target) <= data.policy.GetArrivalRadius()
                    && std::abs(static_cast<double>(motion.GetPosition().GetY()) - data.target.GetY()) <= data.policy.GetSurfaceTolerance()) {
                    outcome = data.routeStatus == PathStatus::Complete ? CommitOutcome::GoalReached : CommitOutcome::PartialCorridorEnd;
                }
            }
        }
    }
    const double progress{Distance(data.motion.GetPosition(), intent.m_steeringTarget) - Distance(motion.GetPosition(), intent.m_steeringTarget)};
    if (intent.m_outcome == SteeringOutcome::Moving && outcome == CommitOutcome::Following && cursor <= data.cursor
        && progress <= static_cast<double>(data.policy.GetMinimumProgressSpeed()) * intent.m_step.count()) {
        const auto remaining{data.policy.GetStuckAfter() - data.stalled};
        data.stalled = intent.m_step >= remaining ? data.policy.GetStuckAfter() : data.stalled + intent.m_step;
    } else { data.stalled = CrowdSeconds::zero(); }
    data.motion = std::move(motion); data.cursor = cursor; data.pending.clear(); data.stamp.reset(); m_state->stamp.reset();
    return MotionCommit{outcome, StuckState(data.stalled, data.policy), data.cursor, data.stalled};
}
std::expected<void, CrowdError> Crowd::CancelPrediction(const MovementIntent& intent) noexcept {
    if (m_state->inUse.test_and_set(std::memory_order_acquire)) { return std::unexpected{CrowdError::Busy}; }
    const OperationGuard guard{m_state->inUse};
    if (const auto valid{ValidateAgent(intent.m_agent)}; !valid) { return std::unexpected{valid.error()}; }
    auto& pending{m_state->slots[intent.m_agent.GetIndex().GetValue()].agent->pending};
    const auto found{std::ranges::find_if(pending, [&](const auto& prediction) { return prediction.lock() == intent.m_prediction; })};
    if (!intent.m_prediction || found == pending.end()) { return std::unexpected{CrowdError::StalePrediction}; }
    pending.erase(found); return {};
}
}
