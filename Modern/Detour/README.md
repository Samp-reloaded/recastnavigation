# Immutable mesh snapshots and exclusive queries

`recastnav.mesh` and `recastnav.query` import the independent RecastNavigation
document and math modules. They have no game, RW4, Jolt, packed `dtPolyRef`, or
process-wide generation dependency.

`MeshSnapshot::Create` accepts validated tile leases, checks aggregate document
payload and graph/work capacities, copies each document into const-owned storage,
and orders tiles by full `TileKey`. Copying prevents a retained mutable source
alias from changing an already published snapshot. The resulting shared snapshot
owns its frozen tile documents, graph nodes and transitions.

`Locate(TileKey, PolygonIndex)` returns a checked `PolygonLocation`. The location
retains the exact snapshot lease. A missing tile is an empty optional; an invalid
polygon is an error. Locations from another snapshot are rejected even when their
tile and polygon indices coincide. No location or session value is serialized as
runtime authority. Locations and completed query results can outlive a workspace.

Local edges use the document's validated reciprocal adjacency. Cross-tile edges
match opposite portals on a shared boundary plane and adjacent tile coordinates;
their common interval is clipped to the profile's permitted climb. Connections
may cross tile layers when their geometry permits it. Directed off-mesh links
attach their start in the owning tile and their end in the complete snapshot,
using horizontal radius and vertical climb. Their full user ID, area and category
data are retained. Unloaded or geometrically unmatched endpoints simply leave the
connection unresolved in that snapshot. Replacing the tile set produces a new
snapshot and a new set of local locations.

`QueryWorkspace::Create` owns a PMR pool whose upstream allocator has static
lifetime. `CreateWithResource` retains a caller-supplied shared resource lease;
the caller must include any resource-specific upstream ownership in that lease.
Scratch containers are destroyed before their resource. One operation exclusively
leases the workspace, including during allocator callbacks, so concurrent or
recursive use returns `WorkspaceBusy`. No PMR scratch or scratch-backed view
escapes a query. Allocation and length failures are translated to typed errors,
and the workspace can be reused after either kind of failure.

Nearest queries search polygons whose actual surface bounds intersect the supplied
position/half-extents box. They compute the Euclidean closest point on validated
detail triangles, or a convex polygon fan when detail data is absent. Normal
misses return an empty optional. Disabled surfaces and filter-excluded areas or
categories are skipped. Equal distances select canonical tile/polygon order.

Path queries run A* or Dijkstra over the immutable centroid/portal graph, using
an indexed binary heap with decrease-key and full-width local indices. This is a
polygon corridor query, not a continuous shortest-path or funnel query. A graph
edge costs source-centroid to departure with the source area multiplier, the
departure/arrival segment with its off-mesh area multiplier (source area for a
normal portal), and arrival to destination-centroid with the destination area
multiplier. A* uses centroid Euclidean distance times the minimum enabled area
cost; Dijkstra uses zero. Positive finite area costs, non-finite accumulated-cost
checks and canonical tie order are enforced.

Every result owns its polygon locations and transition values. A complete result
reaches the goal. Unreachable goals, node/expansion/corridor limits and cancellation
produce a valid bounded partial corridor with an explicit termination reason.
When node capacity prunes alternatives, a partial result may reach the goal;
the search cannot establish optimality after that pruning.
The partial target is the discovered polygon nearest the goal centroid, then the
least graph cost, then canonical polygon order. A corridor-length limit keeps a
prefix starting at the requested source and reports that prefix's actual cost.
Cancellation of a nearest query returns a typed cancellation error because it
cannot establish a correct nearest match from an incomplete scan.

Focused doctest scenarios live in `Modern/Tests/MeshQueryTests.cpp`; the parent
project supplies the shared test main and common build gate.
