# Cooking migration slice

`recastnav.build` accepts borrowed `Vec3` / `Triangle` spans, a Y-up build profile,
explicit world bounds and tile key. `CookTile` returns an immutable `TileDocument`
or a stage-specific typed failure. Inputs are consumed synchronously. Scratch
uses the supplied `std::pmr::memory_resource`; the published document owns its
storage independently. The context bounds scratch bytes and grid cells, and
passes document limits through validation and publication.

The cooker runs slope classification, rasterization, low-obstacle / ledge /
clearance filters, compact-field construction, radius erosion, distance field,
watershed regions, contour simplification, convex polygon mesh and sampled
detail mesh. Input order is preserved; identical input/options produce identical
results on the same floating-point implementation. The metadata fingerprint is
deterministic provenance data, not a cryptographic digest. Output bounds expand
to whole XZ cells and include vertical quantization headroom.

This is an explicit migration boundary. The private algorithm oracle still uses
upstream `rcHeightfield`, `rcSpan`, compact-cell/span bitfields, byte area IDs,
ushort mesh coordinates/adjacency, byte detail indices, raw arrays and upstream
containers. No claim is made that those runtime representations are modernized.
Checked input/compact/output limits reject unsupported legacy representations.
No legacy pointer, sentinel, bitfield or allocator callback is exported through
the modern module. Border size is zero; tile seam cooking and generated layer
documents are not provided by this API.

Each call owns an allocation session. Renamed private oracle allocator symbols
route all legacy allocations through that session without changing the upstream
process-global allocator callbacks. RAII releases completed stage objects;
the allocation ledger additionally releases orphan legacy local allocations
after failure unwinding. Cancellation is cooperative between stages and at
allocations, so a long allocation-free upstream loop is not immediately
interruptible. The caller's memory resource must outlive the synchronous call
and support the caller's chosen concurrency.

Build integration: compile `Build.ixx`, `Build.cpp` and
`LegacyAllocation.cpp`; privately link all `Recast/Source/*.cpp` except
`RecastAlloc.cpp`. Only that private oracle target receives
`rcAlloc=recastnavCookAlloc` and `rcFree=recastnavCookFree`. Enable C++ exception
unwinding on the oracle target, and retain its include directory privately.
The modern module has no game, RW4, Jolt, renderer or OS dependency.
