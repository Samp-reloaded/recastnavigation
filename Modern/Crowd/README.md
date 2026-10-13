# Provider-neutral crowd steering

`recastnav.crowd` implements bounded steering over the independent
`recastnav.query`, `recastnav.mesh`, `recastnav.document` and `recastnav.math`
contracts. It consumes a frozen `MeshSnapshot` and has no RW4, Jolt, game, scene,
transform, network or session dependency. The source is a separate C++23 module;
the existing upstream `DetourCrowd` files remain read-only algorithm references.

## Movement ownership

The sequence is explicit:

1. Create a `Crowd` for one exact immutable mesh snapshot and optional owned PMR
   resource, then `RegisterAgent` with its actual `ResolvedMotion` and policy.
2. `RequestPath` borrows a `QueryWorkspace` synchronously to obtain a checked
   corridor. Its filter is owned by the agent policy; no borrowed filter or
   workspace escapes this call.
3. `CaptureSnapshot` copies the live agents, policies, corridors and any supplied
   static avoidance segments into an immutable owned value.
4. `PredictSteering` returns owned `MovementIntent` values. Each contains a planar
   velocity in world units per second, its `std::chrono` step, outcome diagnostics
   and an unforgeable issued prediction identity. It contains no future position.
5. The caller resolves that intent through its movement/collision provider and
   calls `CommitResolvedMotion` with the actual position, velocity and optional
   exact-snapshot polygon location. This is the only operation that changes an
   existing agent's actual motion or advances its corridor cursor.

An actual location must contain the supplied XZ position; its validated detail
surface height must agree within the policy's surface tolerance. An absent
location represents legitimate motion outside navigation: the actual position
is retained and steering reports `OutsideNavigation` until the caller supplies a
valid location in a later resolved-motion commit. A wrong snapshot or a position
contradicting a supplied location is rejected transactionally.

Normal resolved movement can advance across several corridor polygons or move
back to an earlier one. Movement outside the retained corridor is kept as actual
truth and reports `CorridorLost`. A new `RequestPath` starts at that actual
location. Reaching a partial corridor's final anchor reports
`PartialCorridorEnd`, never `GoalReached`; the original query's partial status and
termination are returned by `RouteAdmission`.

## Local identity and stale results

`AgentLease` is a copyable registration lease. Its full-width `CrowdAgentIndex`
is local diagnostic data. The private registration identity must match both the
live owner and the current slot; an index alone cannot address an agent. A retired
slot can be reused without a salt, packed reference, session epoch, global
generation or process-wide registry. Old leases and intents remain rejected after
that reuse. Dropping all copies of a registration lease permits its slot to be
reclaimed; snapshots and outstanding intents intentionally retain their leases.
`RetireAgent` reliably retires the registration and all pending results without
allocating. Destroying the owner also retires all registrations.

Each prediction is registered by its exact private identity and the exact
committed-state identity from which it was produced. Commits for independent
agents can arrive in any batch order. For multiple predictions based on the same
agent state, the first accepted commit wins and supersedes the others. A replay,
cancelled prediction or superseded prediction returns `StalePrediction` and
cannot rewind actual motion. Changing a route invalidates that agent's pending
results. Mutating any live state invalidates prior crowd snapshots for new
prediction, so neighbour data cannot silently describe an older roster.

Pending identities are weakly retained: dropping all intent copies releases the
pending capacity at the next prediction. `CancelPrediction` explicitly releases
one identity; `CancelRoute` releases the route and pending results and yields a
typed stop outcome. Cancellation tokens abort path requests and prediction
batches without publishing a subset of issued predictions.

## Implemented steering algorithm

Steering follows the next portal's departure point, then the next polygon's
surface anchor when close enough to a normal portal to cross it. The final
complete corridor follows the requested surface target; a partial corridor
follows the last polygon's deterministic interior surface anchor (or its first
vertex when a rounded anchor is outside a very thin polygon).

For a moving agent, neighbour discovery examines the immutable snapshot in local
index order, checks vertical interval overlap and planar range, and retains the
nearest configured number ordered by distance then local index. A truncated
neighbour set is explicitly reported. Separation uses a squared distance falloff
and a deterministic opposite direction for coincident agents.

The velocity sampler evaluates the preferred velocity, current velocity, stop,
and a fixed polar pattern of configurable angular and speed samples. Every sample
is acceleration-limited relative to the actual resolved velocity. An externally
forced velocity above the policy's speed limit is decelerated at the permitted
acceleration and is marked `IsRecoveringSpeed`; it is never silently snapped.

Sampling scores desired/current velocity deviation, deterministic passing-side
preference, and relative circle time of impact. The reciprocal circle velocity
and overlap-exit penalty follow the ideas in upstream
`DetourObstacleAvoidance.cpp::processSample` and `sweepCircleCircle`, evaluated in
double precision. Static obstacles use a radius-expanded segment capsule sweep,
including endpoint circles. Solid edges of the current polygon and filtered-out
local neighbours are also supplied as walls. Portal edges are not assumed solid;
unresolved portal connectivity remains the mesh/query provider's concern.

The lowest score wins; equal scores preserve sample order. Outcomes distinguish
clear, adjusted, blocked and initially overlapping motion. These are steering
diagnostics, not a claim that sampled velocity is collision-free. The external
movement provider remains authoritative. Geometry, speed and acceleration units
are the caller's shared world-unit convention; time fields and APIs use chrono
durations, not frame counts or implicit wall-clock time.

Stuck detection accumulates only after commits that make insufficient progress
toward the issued steering target. It uses the issued chrono step, caps elapsed
stall time at the configured threshold, and reports moving/stalled/stuck. A
nonzero predicted velocity with zero actual motion therefore becomes stuck. A
terminal or cancelled route resets stall accumulation.

## Off-mesh boundaries

Near an off-mesh departure, an intent reports `TraversalRequired` and owns the
exact transition and full user ID. The library performs no traversal, animation,
position interpolation or transform write. If an external provider reports
motion across that transition, the actual motion is retained and the commit
reports `TraversalBoundary` without advancing the corridor through it. The
external task/traversal owner must then request a new corridor from the arrival
location. This preserves a complete steering contract without pretending to
implement the task, physics or traversal systems.

## Bounds, ownership and upstream differences

`CrowdLimits` bounds registrations, retained neighbours, corridor polygons,
external segments, pending identities, sampling pattern, total prediction work
and live PMR bytes. Prediction work charges neighbour scans, sample evaluations
and circle/segment tests. Exceeding work or allocation limits produces a typed
failure for the whole batch. Reserved scratch capacity may change, but no partial
batch is issued. Query work is bounded separately by the borrowed workspace's
`QueryLimits`; document detail geometry is bounded by its admission limits.

Persistent slot storage, corridor buffers, pending identity lists and reusable
neighbour/wall scratch use the retained PMR resource through a live-byte budget
wrapper. Member destruction releases those containers before their resource.
Policies, registration/control objects, snapshots and returned batches use normal
owned standard containers; their record/corridor/obstacle counts are bounded,
while the byte budget specifically covers the PMR allocations. Caller-supplied
resources must retain their own upstream ownership. All allocation failures at
API execution boundaries are translated to typed errors. A workspace-style
exclusive lease rejects concurrent or allocator-recursive use with `Busy`.

Unlike upstream `dtCrowd::update`, this module does not integrate positions,
resolve overlaps by writing positions, constrain a predicted position to the
mesh, animate off-mesh traversal, queue asynchronous paths or apply visibility/
topology optimizations. Path planning is the real existing bounded query;
steering and capsule avoidance are complete within the explicit sampled-velocity
contract above. Path arrival and cursor progression use committed actual motion.

## Source validation status

Focused doctest source is `Modern/Tests/CrowdSteeringTests.cpp`, without its own
main. It covers empty/one/crossing agents, resolved motion different from the
prediction, partial corridors, external traversal, stale/retired identities,
out-of-order commits, blockage/stuck, filters, cancellation, PMR lifetime and
allocation/work/pending budgets. This isolated checkpoint is **GateNotRun**:
no tests, builds or configuration have been executed. The coordinator owns build
integration and the serialized native gate.
