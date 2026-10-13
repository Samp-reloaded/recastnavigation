# SRNT and SRNL version 1

These fork-owned documents use Recast's Y-up, XZ-ground-plane convention.
They contain content geometry and build metadata, never runtime pointers,
allocation state, Session identities, mesh slots, or packed Detour references.
They are not compatible with `DT_NAVMESH_VERSION` or `DT_TILECACHE_VERSION`.

All integers are little-endian. Floats are IEEE-754 binary32 bit patterns.
Signed tile coordinates are signed 64-bit two's-complement values. Counts and
indices are unsigned 64-bit wire values checked before conversion to `size_t`.
Booleans have one byte and accept only 0 or 1. No padding is written.

## Frame

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `SRNT` for a tile, `SRNL` for a layer |
| 4 | 2 | Version, exactly 1 |
| 6 | 2 | Reserved, exactly 0 |
| 8 | 8 | Exact payload byte length |
| 16 | 8 | FNV-1a-64 of the payload |
| 24 | 4 | Coordinate convention, exactly 1 (Y-up/XZ) |
| 28 | 4 | Reserved, exactly 0 |

FNV-1a uses offset basis 14695981039346656037 and prime 1099511628211,
with unsigned 64-bit arithmetic. It detects accidental corruption; it is not
an authentication mechanism. Length, integrity, and reserved fields are checked
before decoding arrays. Trailing bytes are rejected.

## Common metadata (100 payload bytes)

In order: tile X `i64`, tile Z `i64`, layer `u32`, opaque cook fingerprint
`byte[32]`, bounds minimum `float[3]`, bounds maximum `float[3]`, then six floats:
cell size, cell height, agent height, agent radius, maximum climb, maximum slope
in degrees. A zero fingerprint is valid opaque metadata; this codec does not
claim content authority or calculate a cook key.

## Tile payload

After metadata: four `u64` counts (vertices, polygons, detail meshes, off-mesh
connections), followed by the corresponding arrays in that order.

- Vertex: XYZ floats.
- Polygon: `u64` vertex count; that many `u64` indices; `u32` area; enabled
  boolean; category list (`u64` count then `u32` values); one edge per vertex.
- Edge: one-byte kind and `u64` value. Kind 0 requires value 0 (wall), kind 1 is
  a local polygon index, kind 2 is a portal side: +X=0, +Z=1, -X=2, -Z=3.
- Detail mesh: additional-vertex and triangle `u64` counts; XYZ additional
  vertices; triangles containing three `u64` indices and three boundary booleans.
  Triangle indices address the owning polygon's vertices first, then additional
  vertices. Details are either absent or present once per surface polygon.
- Off-mesh connection: start XYZ, end XYZ, radius float, opaque user label
  `u64`, area `u32`, bidirectional boolean, category list.

Surface polygons are convex with upward winding in XZ. Local neighbours must
refer back through the same geometrical edge in reverse order. Portals must lie
on their declared tile bound. Areas retain the upstream 0..63 domain; categories
are ordinary distinct user labels, not a bit mask. Off-mesh endpoints may leave
the tile; runtime endpoint admission is a mesh query's responsibility.

Each detail mesh must triangulate its entire owning polygon. Projected vertices
are unique; interior edges occur exactly twice in opposite directions with both
boundary flags clear. Unpaired edges have the boundary flag set and partition
the entire oriented polygon perimeter exactly once. Split perimeter segments
and collinear polygon vertices are supported. Positive triangle winding plus
this complete boundary proves that the surface has no holes or overlapping
triangles. Admission scratch storage is bounded by the packet byte budget;
allocation failure remains a typed refusal.

## Layer payload

After metadata: width and height `u64`; usable minimum X/Z and maximum X/Z
`u64` (inclusive); minimum and maximum voxel height `u32`; cell count `u64`;
row-major cells, X changing fastest.

A cell contains: has-height boolean, height `u32` (must be zero when absent),
area `u32`, then four pairs of neighbour/portal booleans. Directions retain
upstream order: -X, +Z, +X, -Z. Heights are relative to the declared minimum;
their full-width values must fit the declared height range. Null-area cells may
retain geometry height after an obstacle changes walkability. Absent height
cannot be walkable or connected. Local neighbour connections are reciprocal.
Neighbour and portal states are mutually exclusive for a direction. Portals
between distinct heightfield layers may occur inside the grid.

## Admission and bounds

`DocumentLimits` bounds packet bytes, array counts, per-polygon vertices, and
per-element category counts. Decoders compare every count with its configured
limit and the remaining packet's minimum record size before allocation. Semantic
admission is shared with `TileDocument::Create` and `LayerDocument::Create`.
Document accessors expose read-only views. A document is an owning value that
may be copied, moved, or assigned; a publication owner must take its own const
copy before retaining it. A `shared_ptr<const Document>` converted from a
retained mutable alias is not an immutable publication. All codec entry points
return `expected` failures; allocation and length failures do not publish a
partial document. Empty surface tiles are representable. Layers always have a
nonempty grid, including a grid whose cells are all unavailable.
