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
export module recastnav.crowd;
import std;
export import recastnav.query;

export namespace recastnav {
using CrowdSeconds = std::chrono::duration<double>;
enum class CrowdError {
    InvalidLimits, InvalidPolicy, InvalidMotion, InvalidTimeStep, InvalidObstacle, InvalidResourceLease,
    MissingSnapshot, SnapshotMismatch, OwnerMismatch, RetiredAgent, StaleSnapshot,
    StalePrediction, PendingCapacity, AgentCapacity, CorridorCapacity, ObstacleCapacity,
    WorkCapacity, MemoryCapacity, AllocationFailure, Busy, Cancelled, QueryRejected,
};
enum class SteeringOutcome { Moving, Idle, GoalReached, PartialCorridorEnd, TraversalRequired, OutsideNavigation, CorridorLost, RouteCancelled };
enum class AvoidanceOutcome { Stationary, Clear, Adjusted, Blocked, Overlapping };
enum class NeighbourOutcome { Complete, Truncated };
enum class StuckOutcome { Moving, Stalled, Stuck };
enum class CommitOutcome { Following, GoalReached, PartialCorridorEnd, OutsideNavigation, CorridorLost, TraversalBoundary, Idle };
struct CrowdAgentIndexTag;
using CrowdAgentIndex = Index<CrowdAgentIndexTag>;

class CrowdLimits final {
public:
    constexpr CrowdLimits(std::size_t agents = 1'024u, std::size_t neighbours = 16u,
        std::size_t corridor = 256u, std::size_t obstacles = 256u, std::size_t pending = 4u,
        std::size_t angularSamples = 16u, std::size_t speedSamples = 3u,
        std::size_t work = 4'194'304u, std::size_t bytes = 64u * 1024u * 1024u) noexcept
        : m_agents{agents}, m_neighbours{neighbours}, m_corridor{corridor}, m_obstacles{obstacles},
          m_pending{pending}, m_angles{angularSamples}, m_speeds{speedSamples}, m_work{work}, m_bytes{bytes} {}
    [[nodiscard]] constexpr std::size_t GetMaximumAgents() const noexcept { return m_agents; }
    [[nodiscard]] constexpr std::size_t GetMaximumNeighbours() const noexcept { return m_neighbours; }
    [[nodiscard]] constexpr std::size_t GetMaximumCorridor() const noexcept { return m_corridor; }
    [[nodiscard]] constexpr std::size_t GetMaximumObstacles() const noexcept { return m_obstacles; }
    [[nodiscard]] constexpr std::size_t GetMaximumPendingPerAgent() const noexcept { return m_pending; }
    [[nodiscard]] constexpr std::size_t GetAngularSamples() const noexcept { return m_angles; }
    [[nodiscard]] constexpr std::size_t GetSpeedSamples() const noexcept { return m_speeds; }
    [[nodiscard]] constexpr std::size_t GetMaximumWork() const noexcept { return m_work; }
    [[nodiscard]] constexpr std::size_t GetMaximumPmrBytes() const noexcept { return m_bytes; }
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return m_agents > 0u && m_corridor > 0u && m_pending > 0u && m_angles >= 4u && m_speeds > 0u
            && m_obstacles <= std::numeric_limits<std::size_t>::max() - 6u
            && m_bytes > 0u && m_angles <= (std::numeric_limits<std::size_t>::max() - 3u) / m_speeds;
    }
private:
    std::size_t m_agents{}, m_neighbours{}, m_corridor{}, m_obstacles{}, m_pending{}, m_angles{}, m_speeds{}, m_work{}, m_bytes{};
};
class AgentPolicy final {
public:
    AgentPolicy(float radius = 0.3f, float height = 1.8f, float maximumSpeed = 3.5f,
        float maximumAcceleration = 8.0f, float neighbourRange = 6.0f,
        float arrivalRadius = 0.1f, float surfaceTolerance = 0.1f, float separationWeight = 0.5f,
        CrowdSeconds horizon = CrowdSeconds{2.5}, CrowdSeconds stuckAfter = CrowdSeconds{2.0},
        float minimumProgressSpeed = 0.05f, QueryFilter filter = {}) noexcept
        : m_radius{radius}, m_height{height}, m_speed{maximumSpeed}, m_acceleration{maximumAcceleration},
          m_neighbourRange{neighbourRange}, m_arrival{arrivalRadius}, m_surfaceTolerance{surfaceTolerance},
          m_separation{separationWeight}, m_horizon{horizon}, m_stuckAfter{stuckAfter},
          m_minimumProgress{minimumProgressSpeed}, m_filter{std::move(filter)} {}
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] float GetRadius() const noexcept { return m_radius; }
    [[nodiscard]] float GetHeight() const noexcept { return m_height; }
    [[nodiscard]] float GetMaximumSpeed() const noexcept { return m_speed; }
    [[nodiscard]] float GetMaximumAcceleration() const noexcept { return m_acceleration; }
    [[nodiscard]] float GetNeighbourRange() const noexcept { return m_neighbourRange; }
    [[nodiscard]] float GetArrivalRadius() const noexcept { return m_arrival; }
    [[nodiscard]] float GetSurfaceTolerance() const noexcept { return m_surfaceTolerance; }
    [[nodiscard]] float GetSeparationWeight() const noexcept { return m_separation; }
    [[nodiscard]] CrowdSeconds GetAvoidanceHorizon() const noexcept { return m_horizon; }
    [[nodiscard]] CrowdSeconds GetStuckAfter() const noexcept { return m_stuckAfter; }
    [[nodiscard]] float GetMinimumProgressSpeed() const noexcept { return m_minimumProgress; }
    [[nodiscard]] const QueryFilter& GetFilter() const noexcept { return m_filter; }
private:
    float m_radius{}, m_height{}, m_speed{}, m_acceleration{}, m_neighbourRange{}, m_arrival{}, m_surfaceTolerance{}, m_separation{};
    CrowdSeconds m_horizon{}, m_stuckAfter{};
    float m_minimumProgress{};
    QueryFilter m_filter;
};
class Crowd;
class AgentLease final {
public:
    AgentLease(const AgentLease&) noexcept = default;
    AgentLease& operator=(const AgentLease&) noexcept = default;
    [[nodiscard]] CrowdAgentIndex GetIndex() const noexcept;
    [[nodiscard]] bool operator==(const AgentLease&) const noexcept = default;
private:
    friend class Crowd;
    class Identity;
    explicit AgentLease(std::shared_ptr<const Identity> identity) noexcept : m_identity{std::move(identity)} {}
    std::shared_ptr<const Identity> m_identity;
};
class ResolvedMotion final {
public:
    ResolvedMotion(Vec3 position, Vec3 velocity = {}, std::optional<PolygonLocation> location = {}) noexcept
        : m_position{position}, m_velocity{velocity}, m_location{std::move(location)} {}
    [[nodiscard]] const Vec3& GetPosition() const noexcept { return m_position; }
    [[nodiscard]] const Vec3& GetVelocity() const noexcept { return m_velocity; }
    [[nodiscard]] const std::optional<PolygonLocation>& GetLocation() const noexcept { return m_location; }
private:
    Vec3 m_position{}, m_velocity{};
    std::optional<PolygonLocation> m_location{};
};
class AvoidanceSegment final {
public:
    constexpr AvoidanceSegment(Vec3 start, Vec3 end, float height = 2.0f) noexcept : m_start{start}, m_end{end}, m_height{height} {}
    [[nodiscard]] constexpr const Vec3& GetStart() const noexcept { return m_start; }
    [[nodiscard]] constexpr const Vec3& GetEnd() const noexcept { return m_end; }
    [[nodiscard]] constexpr float GetHeight() const noexcept { return m_height; }
private:
    Vec3 m_start{}, m_end{};
    float m_height{};
};
class AgentSnapshot final {
public:
    [[nodiscard]] const AgentLease& GetAgent() const noexcept { return m_agent; }
    [[nodiscard]] const ResolvedMotion& GetMotion() const noexcept { return m_motion; }
    [[nodiscard]] const AgentPolicy& GetPolicy() const noexcept { return m_policy; }
    [[nodiscard]] std::span<const PolygonLocation> GetCorridor() const noexcept { return m_corridor; }
    [[nodiscard]] std::size_t GetCorridorCursor() const noexcept { return m_cursor; }
    [[nodiscard]] StuckOutcome GetStuckOutcome() const noexcept;
private:
    friend class Crowd;
    friend class MovementIntent;
    class Stamp;
    AgentSnapshot(AgentLease agent, ResolvedMotion motion, AgentPolicy policy,
        std::vector<PolygonLocation> corridor, std::vector<MeshTransition> transitions, std::size_t cursor,
        PathStatus routeStatus, Vec3 target, bool cancelled, CrowdSeconds stalled, std::shared_ptr<const Stamp> stamp) noexcept
        : m_agent{std::move(agent)}, m_motion{std::move(motion)}, m_policy{std::move(policy)}, m_corridor{std::move(corridor)},
          m_transitions{std::move(transitions)}, m_cursor{cursor}, m_routeStatus{routeStatus}, m_target{target},
          m_cancelled{cancelled}, m_stalled{stalled}, m_stamp{std::move(stamp)} {}
    AgentLease m_agent;
    ResolvedMotion m_motion;
    AgentPolicy m_policy;
    std::vector<PolygonLocation> m_corridor{};
    std::vector<MeshTransition> m_transitions{};
    std::size_t m_cursor{};
    PathStatus m_routeStatus{};
    Vec3 m_target{};
    bool m_cancelled{};
    CrowdSeconds m_stalled{};
    std::shared_ptr<const Stamp> m_stamp;
};
class CrowdSnapshot final {
public:
    [[nodiscard]] std::span<const AgentSnapshot> GetAgents() const noexcept { return m_agents; }
    [[nodiscard]] std::span<const AvoidanceSegment> GetObstacles() const noexcept { return m_obstacles; }
    [[nodiscard]] const SnapshotLease& GetMesh() const noexcept { return m_mesh; }
private:
    friend class Crowd;
    class Stamp;
    CrowdSnapshot(SnapshotLease mesh, std::weak_ptr<const Crowd> owner, std::vector<AgentSnapshot> agents,
        std::vector<AvoidanceSegment> obstacles, std::shared_ptr<const Stamp> stamp) noexcept
        : m_mesh{std::move(mesh)}, m_owner{std::move(owner)}, m_agents{std::move(agents)}, m_obstacles{std::move(obstacles)}, m_stamp{std::move(stamp)} {}
    SnapshotLease m_mesh;
    std::weak_ptr<const Crowd> m_owner;
    std::vector<AgentSnapshot> m_agents{};
    std::vector<AvoidanceSegment> m_obstacles{};
    std::shared_ptr<const Stamp> m_stamp;
};
class MovementIntent final {
public:
    [[nodiscard]] const AgentLease& GetAgent() const noexcept { return m_agent; }
    [[nodiscard]] const Vec3& GetVelocity() const noexcept { return m_velocity; }
    [[nodiscard]] CrowdSeconds GetStep() const noexcept { return m_step; }
    [[nodiscard]] SteeringOutcome GetOutcome() const noexcept { return m_outcome; }
    [[nodiscard]] AvoidanceOutcome GetAvoidanceOutcome() const noexcept { return m_avoidance; }
    [[nodiscard]] NeighbourOutcome GetNeighbourOutcome() const noexcept { return m_neighbours; }
    [[nodiscard]] std::size_t GetNeighbourCount() const noexcept { return m_neighbourCount; }
    [[nodiscard]] StuckOutcome GetStuckOutcome() const noexcept { return m_stuck; }
    [[nodiscard]] const std::optional<MeshTransition>& GetRequiredTraversal() const noexcept { return m_traversal; }
    [[nodiscard]] bool IsRecoveringSpeed() const noexcept { return m_recoveringSpeed; }
private:
    friend class Crowd;
    class Prediction;
    MovementIntent(AgentLease agent, Vec3 velocity, Vec3 steeringTarget, CrowdSeconds step, SteeringOutcome outcome,
        AvoidanceOutcome avoidance, NeighbourOutcome neighbours, std::size_t neighbourCount, StuckOutcome stuck,
        std::optional<MeshTransition> traversal, bool recoveringSpeed, std::shared_ptr<const Prediction> prediction) noexcept
        : m_agent{std::move(agent)}, m_velocity{velocity}, m_steeringTarget{steeringTarget}, m_step{step}, m_outcome{outcome},
          m_avoidance{avoidance}, m_neighbours{neighbours}, m_neighbourCount{neighbourCount}, m_stuck{stuck},
          m_traversal{std::move(traversal)}, m_recoveringSpeed{recoveringSpeed}, m_prediction{std::move(prediction)} {}
    AgentLease m_agent;
    Vec3 m_velocity{}, m_steeringTarget{};
    CrowdSeconds m_step{};
    SteeringOutcome m_outcome{};
    AvoidanceOutcome m_avoidance{};
    NeighbourOutcome m_neighbours{};
    std::size_t m_neighbourCount{};
    StuckOutcome m_stuck{};
    std::optional<MeshTransition> m_traversal{};
    bool m_recoveringSpeed{};
    std::shared_ptr<const Prediction> m_prediction;
};
class MotionCommit final {
public:
    MotionCommit(CommitOutcome outcome, StuckOutcome stuck, std::size_t cursor, CrowdSeconds stalled) noexcept
        : m_outcome{outcome}, m_stuck{stuck}, m_cursor{cursor}, m_stalled{stalled} {}
    [[nodiscard]] CommitOutcome GetOutcome() const noexcept { return m_outcome; }
    [[nodiscard]] StuckOutcome GetStuckOutcome() const noexcept { return m_stuck; }
    [[nodiscard]] std::size_t GetCorridorCursor() const noexcept { return m_cursor; }
    [[nodiscard]] CrowdSeconds GetStalledDuration() const noexcept { return m_stalled; }
private:
    CommitOutcome m_outcome{};
    StuckOutcome m_stuck{};
    std::size_t m_cursor{};
    CrowdSeconds m_stalled{};
};
class RouteAdmission final {
public:
    RouteAdmission(PathStatus status, PathTermination termination, std::size_t count) noexcept
        : m_status{status}, m_termination{termination}, m_count{count} {}
    [[nodiscard]] PathStatus GetStatus() const noexcept { return m_status; }
    [[nodiscard]] PathTermination GetTermination() const noexcept { return m_termination; }
    [[nodiscard]] std::size_t GetPolygonCount() const noexcept { return m_count; }
private:
    PathStatus m_status{};
    PathTermination m_termination{};
    std::size_t m_count{};
};
class Crowd final : public std::enable_shared_from_this<Crowd> {
    class ConstructionKey final { friend class Crowd; ConstructionKey() = default; };
    class State;
public:
    [[nodiscard]] static std::expected<std::shared_ptr<Crowd>, CrowdError> Create(SnapshotLease mesh, CrowdLimits limits = {}) noexcept;
    [[nodiscard]] static std::expected<std::shared_ptr<Crowd>, CrowdError> CreateWithResource(SnapshotLease mesh,
        std::shared_ptr<std::pmr::memory_resource> resource, CrowdLimits limits = {}) noexcept;
    Crowd(std::unique_ptr<State> state, ConstructionKey) noexcept;
    ~Crowd();
    Crowd(const Crowd&) = delete;
    Crowd& operator=(const Crowd&) = delete;
    [[nodiscard]] std::expected<AgentLease, CrowdError> RegisterAgent(ResolvedMotion motion, const AgentPolicy& policy = {}) noexcept;
    [[nodiscard]] std::expected<void, CrowdError> RetireAgent(const AgentLease& agent) noexcept;
    [[nodiscard]] std::expected<void, CrowdError> CancelRoute(const AgentLease& agent) noexcept;
    [[nodiscard]] std::expected<RouteAdmission, CrowdError> RequestPath(const AgentLease& agent,
        const PolygonLocation& goal, const Vec3& target, QueryWorkspace& query, std::stop_token cancellation = {}) noexcept;
    [[nodiscard]] std::expected<CrowdSnapshot, CrowdError> CaptureSnapshot(std::span<const AvoidanceSegment> obstacles = {}) noexcept;
    [[nodiscard]] std::expected<std::vector<MovementIntent>, CrowdError> PredictSteering(const CrowdSnapshot& snapshot,
        CrowdSeconds step, std::stop_token cancellation = {}) noexcept;
    [[nodiscard]] std::expected<MotionCommit, CrowdError> CommitResolvedMotion(const MovementIntent& intent, ResolvedMotion motion) noexcept;
    [[nodiscard]] std::expected<void, CrowdError> CancelPrediction(const MovementIntent& intent) noexcept;
private:
    [[nodiscard]] std::expected<void, CrowdError> ValidateAgent(const AgentLease& agent) const noexcept;
    [[nodiscard]] std::expected<void, CrowdError> ValidateMotion(const ResolvedMotion& motion, const AgentPolicy& policy) const noexcept;
    [[nodiscard]] std::expected<std::vector<MovementIntent>, CrowdError> Predict(const CrowdSnapshot& snapshot,
        CrowdSeconds step, std::stop_token cancellation);
    std::unique_ptr<State> m_state;
};
}
