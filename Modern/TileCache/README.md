# Immutable affected-tile publication

This slice implements bounded obstacle-change admission, explicit affected-tile
requests, preparation of complete replacement documents, and atomic publication
of geometry plus applied obstacle descriptions. It uses the existing Core
`TileDocument` / `LayerDocument`, versioned SRNT / SRNL codecs, and Detour
`MeshSnapshot` contracts. It has no game, RW4, Jolt, renderer, executor or thread
dependency.

## Public flow

1. `TileCache::Create` deep-freezes initial tile/layer documents and accepts an
   owning `shared_ptr<std::pmr::memory_resource>`. Readers acquire immutable
   `PublicationSnapshot` leases through `AcquireSnapshot`.
2. `QueueObstacleChange` admits a bounded request and marks the obstacle Dirty.
   Its affected keys are canonical full `TileKey`s. An update dirties the union
   of the previous and requested footprints; removal dirties the previous one.
   Footprints include each tile's agent radius and conservatively include all
   vertical layers sharing that XZ footprint.
3. `BeginNextBatch` changes Dirty to Pending and returns an explicit immutable
   `BatchRequest`. Callers perform their own cooking work; this library starts
   no jobs or threads.
4. `PrepareBatch` requires exactly one replacement for every affected key. It
   checks duplicates, missing/unexpected keys, profile/bounds consistency,
   optional matching layer metadata, document limits and the full successor
   mesh. The resulting `BatchCandidate` owns the complete successor snapshot,
   including independent copies of every supplied document.
5. `Publish` requires the admitted request to be Pending and its base snapshot
   to remain current. One atomic shared-lease exchange publishes all tile
   replacements, layer documents, rebuilt mesh connections and the obstacle
   change together. It does no document construction or allocation.

Preparation, allocation, validation, stale-base and cancellation failures leave
the current snapshot untouched. Failed preparation leaves the request Pending
and geometry Dirty; the caller can retry preparation, use `RetryBatch` to rebase
and return it to Dirty, or explicitly `CancelBatch` to discard the requested
change. A stale candidate cannot overwrite a newer publication. No subset can
be published. `GetObstacleStatus` exposes applied/requested bounds, Dirty state
and Pending state. Successful removal makes the obstacle absent.

## Authority and lifetime

`TileLocation` retains its exact publication snapshot. Using it against another
snapshot returns `StaleSnapshot`; old locators remain usable against their old
snapshot. Polygon locations retain the existing `MeshSnapshot` authority. There
is no cache generation counter, pool handle or independent game-session epoch.
Request identity is its immutable owned request object, scoped to the admitting
store; superseded or completed requests return `MissingBatch`.

PMR resources are retained by the store and every publication snapshot; requests
retain their base snapshot until after their PMR key arrays are destroyed. PMR
containers are destroyed before their resource lease. Shared control blocks and
the existing document/mesh implementations use their standard allocators.
Read-side snapshot acquisition is atomic; queue/status changes are serialized by
the store's mutex. The atomic shared-pointer implementation need not be lock-free.
Parallel preparations require the chosen PMR resource to support concurrent use.

Limits bound resident tiles/obstacles, outstanding admitted batches, affected
keys per batch, aggregate admitted request-record/key payload bytes, and the
aggregate SRNT/SRNL document bytes of each snapshot. Existing `MeshLimits` also
bound runtime document and graph construction. Queue byte accounting excludes
allocator/control-block overhead and reserved container slack. Old snapshots,
requests and candidates explicitly retained by callers consume caller-owned
memory; admission limits are not a global process memory quota.
Invalid nested document or mesh limits are rejected before encoding or scratch
allocation as `InvalidLimits`, retaining the nested typed failure. Mesh arc/work
budgets may be zero; exceeding these valid budgets remains `CapacityExceeded`.

## Integration and current boundary

Module: `recastnav.tilecache` (`TileCache.ixx`, `TileCache.cpp`). Interface imports
`recastnav.document` and `recastnav.mesh`; implementation additionally imports
`recastnav.codec.tile` and `recastnav.codec.layer`. Link the Core, Codec and Detour
modern components. `Modern/Tests/TileCachePublicationTests.cpp` uses doctest and
defines no test entry point. This branch intentionally does not edit CMake or
Conan integration.

This slice publishes caller-supplied completed geometry. It does not reconstruct
voxel fields from layer documents, apply obstacle carving, invoke the real
cooker, compress layers, stream new resident tiles, serialize obstacle queues,
or bridge to RW4 scheduling/session policy. The obstacle bounds are publication
metadata and dirty-region inputs. Marking an obstacle Clean means its complete
caller-supplied candidate was published; it is not evidence that an obstacle
voxel rebuild occurred. Connecting the real cooker and supplying correct carved
documents remains a separate higher-level integration.

Validation for this isolated checkpoint: native configure/build/test/install
**NotRun**, as requested. Focused tests cover atomic two-tile replacement,
snapshot/locator authority, stale batch rebasing, cancellation, allocation
rollback, bounded admission, duplicates/missing/metadata rejection, removal,
PMR lifetime and versioned codec ingestion.
Additional regressions cover invalid nested limits with zero resource allocation
attempts and preservation of capacity/invalid-document error categories.
